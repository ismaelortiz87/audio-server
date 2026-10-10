#!/usr/bin/env node
// End-to-end test for the WebRTC gateway container (no npm deps; Node >= 22).
//
//   node test/e2e.mjs [--container NAME] [--url http://127.0.0.1:8090] [--chrome PATH]
//
// Requires the container running with RTC_PUBLIC_IP=127.0.0.1 and the UDP
// range published on 127.0.0.1. Downlink: a 440 Hz tone is played into
// engine_out inside the container; headless Chrome must report remote level
// above a threshold, then below it after the tone stops. Uplink: Chrome's fake
// mic plays a 660 Hz wav; we record engine_in.monitor in the container and
// check RMS and dominant frequency.
import { spawn, execFileSync } from 'node:child_process';
import { writeFileSync, mkdtempSync, realpathSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const arg = (n, d) => { const i = process.argv.indexOf('--' + n); return i > 0 ? process.argv[i + 1] : d; };
const CONTAINER = arg('container', 'cp-rtc-gw');
const BASE = arg('url', 'http://127.0.0.1:8090');
const CHROME = arg('chrome', '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome');
// Run the container on another host (e.g. the VDI, which has rootless podman and
// is where the Console actually runs). `--remote user@host` prefixes the exec
// with ssh and uses `podman` instead of `docker`; quoting is handled by passing
// the container command through a single sh -c on the far side.
const REMOTE = arg('remote', process.env.CROSSPOINT_CONTAINER_SSH ?? '');
const RUNTIME = arg('runtime', REMOTE ? 'podman' : 'docker');
const PORT = 9333 + Math.floor(Math.random() * 500);
const sleep = ms => new Promise(r => setTimeout(r, ms));
let failed = false;
const check = (name, ok, info) => { console.log(`${ok ? 'PASS' : 'FAIL'} ${name} ${info ?? ''}`); if (!ok) failed = true; };

const shellQuote = s => `'${String(s).replace(/'/g, `'\\''`)}'`;
const dx = args => {
  if (!REMOTE) return execFileSync(RUNTIME, ['exec', CONTAINER, ...args], { maxBuffer: 1 << 28 });
  const inner = [RUNTIME, 'exec', CONTAINER, ...args].map(shellQuote).join(' ');
  return execFileSync('ssh', [REMOTE, inner], { maxBuffer: 1 << 28 });
};
// For fire-and-forget helpers (a tone, a recorder) that we must not wait on.
const dspawn = (args, opts = {}) => {
  if (!REMOTE) return spawn(RUNTIME, ['exec', CONTAINER, ...args], opts);
  const inner = [RUNTIME, 'exec', CONTAINER, ...args].map(shellQuote).join(' ');
  return spawn('ssh', [REMOTE, inner], opts);
};

// stereo 16-bit 48 kHz: the format Chrome's fake-capture file reader handles reliably
function wav(freq, secs, rate = 44100) {
  const n = rate * secs, b = Buffer.alloc(44 + n * 4);
  b.write('RIFF', 0); b.writeUInt32LE(36 + n * 4, 4); b.write('WAVEfmt ', 8); b.writeUInt32LE(16, 16);
  b.writeUInt16LE(1, 20); b.writeUInt16LE(2, 22); b.writeUInt32LE(rate, 24); b.writeUInt32LE(rate * 4, 28);
  b.writeUInt16LE(4, 32); b.writeUInt16LE(16, 34); b.write('data', 36); b.writeUInt32LE(n * 4, 40);
  for (let i = 0; i < n; i++) { const v = Math.round(Math.sin(2 * Math.PI * freq * i / rate) * 0.5 * 32767); b.writeInt16LE(v, 44 + i * 4); b.writeInt16LE(v, 46 + i * 4); }
  return b;
}
// RMS and dominant frequency (Goertzel scan 100..2000 Hz, last 1 s) of mono s16le @48k
function analyse(buf, rate = 48000) {
  const n = buf.length >> 1, x = new Float64Array(n);
  for (let i = 0; i < n; i++) x[i] = buf.readInt16LE(i * 2) / 32768;
  const rms = Math.sqrt(x.reduce((s, v) => s + v * v, 0) / n);
  let best = 0, bestP = 0; const m = Math.min(n, rate), off = n - m;
  for (let f = 100; f <= 2000; f += 5) {
    const c = 2 * Math.cos(2 * Math.PI * f / rate); let s1 = 0, s2 = 0;
    for (let i = 0; i < m; i++) { const s = x[off + i] + c * s1 - s2; s2 = s1; s1 = s; }
    const p = s1 * s1 + s2 * s2 - c * s1 * s2;
    if (p > bestP) { bestP = p; best = f; }
  }
  return { rms, freq: best };
}
function record(secs) {
  return new Promise(res => {
    const p = dspawn(['timeout', String(secs), 'parec', '-d', 'engine_in.monitor',
      '--format=s16le', '--rate=48000', '--channels=1', '--raw']);
    const chunks = []; p.stdout.on('data', d => chunks.push(d)); p.on('close', () => res(Buffer.concat(chunks)));
  });
}

const tmp = realpathSync(mkdtempSync(join(tmpdir(), 'rtc-e2e-')));
const micWav = join(tmp, 'mic660.wav');
writeFileSync(micWav, wav(660, 60));

const chrome = spawn(CHROME, [
  ...(process.env.HEADED ? ['--window-position=-2000,0'] : ['--headless=new']), `--remote-debugging-port=${PORT}`, `--user-data-dir=${join(tmp, 'profile')}`,
  '--use-fake-ui-for-media-stream', '--use-fake-device-for-media-stream',
  ...(process.env.NOFILE ? [] : [`--use-file-for-fake-audio-capture=${micWav}`]), '--autoplay-policy=no-user-gesture-required',
  '--no-first-run', '--disable-features=AudioServiceSandbox', 'about:blank'], { stdio: 'ignore' });

let ws, id = 0; const pending = new Map();
const cdp = (method, params = {}) => new Promise((res, rej) => { const i = ++id; pending.set(i, { res, rej }); ws.send(JSON.stringify({ id: i, method, params })); });
const ev = async expr => { const r = await cdp('Runtime.evaluate', { expression: expr, awaitPromise: true, returnByValue: true }); if (r.exceptionDetails) throw new Error(JSON.stringify(r.exceptionDetails)); return r.result.value; };

// The tone generator records its PID in the container so only it is killed.
// `pkill -f gst-launch` as well: over `--remote` the whole command runs through
// an ssh + sh -c wrapper, so `$$` is the wrapper's PID and would die with the
// ssh session while the tone kept playing (which is exactly the bug that made
// "downlink level falls" fail). Matching the process is robust either way.
const startTone = () => dspawn(['sh', '-c',
  'echo $$ > /tmp/tone.pid; exec gst-launch-1.0 -q audiotestsrc wave=sine freq=440 volume=0.5 is-live=true ! audio/x-raw,rate=48000,channels=2 ! pulsesink device=engine_out'], { stdio: 'ignore' });
const stopTone = () => {
  try { dx(['sh', '-c', 'kill $(cat /tmp/tone.pid) 2>/dev/null; pkill -f gst-launch 2>/dev/null; true']); }
  catch { /* already gone */ }
  try { dx(['sh', '-c', 'pkill -f audiotestsrc 2>/dev/null; true']); } catch { /* ignore */ }
};

try {
  for (let i = 0; i < 50; i++) { try { await fetch(`http://127.0.0.1:${PORT}/json/version`); break; } catch { await sleep(200); } }
  const tabs = await (await fetch(`http://127.0.0.1:${PORT}/json/list`)).json();
  ws = new WebSocket(tabs.find(t => t.type === 'page').webSocketDebuggerUrl);
  await new Promise(r => ws.addEventListener('open', r));
  ws.addEventListener('message', m => { const d = JSON.parse(m.data); const p = pending.get(d.id); if (p) { pending.delete(d.id); d.error ? p.rej(new Error(d.error.message)) : p.res(d.result); } });
  await cdp('Page.enable'); await cdp('Runtime.enable');
  await cdp('Page.navigate', { url: `${BASE}/rtc/test` });
  await sleep(1000);
  await ev('window.__connect()');
  for (let i = 0; i < 100 && (await ev('window.__state')) !== 'connected'; i++) await sleep(100);
  const st = await ev('window.__state');
  check('peer connection connected', st === 'connected', st);

  // ---- downlink
  await sleep(1000);
  const quiet = await ev('window.__level');
  check('silent before tone', quiet < 0.005, quiet.toFixed(5));
  startTone();
  let peak = 0, tRise = null; const t0 = Date.now();
  while (Date.now() - t0 < 4000) { const l = await ev('window.__level'); peak = Math.max(peak, l); if (tRise === null && l > 0.05) tRise = Date.now() - t0; await sleep(20); }
  check('downlink level rises with 440 Hz tone', peak > 0.05, `peak=${peak.toFixed(3)} (expect ~0.35) rise_after_ms=${tRise} (includes docker exec start-up)`);
  stopTone();
  await sleep(1500);
  const after = await ev('window.__level');
  check('downlink level falls after tone stops', after < 0.01, after.toFixed(5));

  // ---- uplink: record engine_in.monitor while Chrome's fake mic (660 Hz) plays
  const pcm = await record(3);
  console.log('recorded bytes', pcm.length, 'whole-buffer', JSON.stringify(analyse(pcm)));
  const a = analyse(pcm.subarray(48000));
  check('uplink RMS well above silence', a.rms > 0.05, `rms=${a.rms.toFixed(4)} (${(20 * Math.log10(a.rms)).toFixed(1)} dBFS)`);
  check('uplink dominant frequency ~660 Hz', Math.abs(a.freq - 660) <= 15, `${a.freq} Hz`);

  console.log('micLevel', await ev('window.__micLevel'));
  console.log('PAGE LOG:', JSON.stringify(await ev("document.getElementById('log').textContent")));
  // ---- stats
  const stats = await (await fetch(`${BASE}/rtc/stats`)).json();
  console.log('STATS gateway rtt_s=', stats.rtt_s, 'state=', stats.state);
  const pick = o => ({ ssrc: o.ssrc, packets: o['packets-sent'] ?? o['packets-received'], bytes: o['bytes-sent'] ?? o['bytes-received'], lost: o['packets-lost'], jitter: o.jitter, rtt: o['round-trip-time'] });
  console.log('STATS gateway inbound ', JSON.stringify(stats.inbound.map(pick)));
  console.log('STATS gateway outbound', JSON.stringify(stats.outbound.map(pick)));
  console.log('STATS gateway remote_inbound', JSON.stringify(stats.remote_inbound.map(pick)));
  console.log('STATS browser', JSON.stringify(await ev(`(async()=>{const o=[];(await window.__pc().getStats()).forEach(r=>{if(['inbound-rtp','outbound-rtp','candidate-pair','media-source'].includes(r.type)&&(r.type!=='candidate-pair'||r.nominated))o.push({type:r.type,bytes:r.bytesReceived??r.bytesSent,packets:r.packetsReceived??r.packetsSent,lost:r.packetsLost,jitter:r.jitter,jbDelayMs:r.jitterBufferDelay&&r.jitterBufferEmittedCount?1000*r.jitterBufferDelay/r.jitterBufferEmittedCount:undefined,rtt:r.currentRoundTripTime,audioLevel:r.audioLevel,mic:r.type==='media-source'?r.totalAudioEnergy:undefined})});return o})()`)));
  check('stats endpoint reports traffic both ways', stats.active && stats.inbound.length > 0 && stats.outbound.length > 0);
} catch (e) { check('test run', false, e.stack || e); }
finally {
  stopTone();
  try { ws?.close(); } catch { /* ignore */ }
  chrome.kill();
}
console.log(failed ? 'E2E FAILED' : 'E2E OK');
process.exit(failed ? 1 : 0);
