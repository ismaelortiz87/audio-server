#!/usr/bin/env node
// End-to-end audio test through the REAL engine (P7.1). Node >= 22, no deps.
//
//   node docker/web-console/test/e2e-engine.mjs --console cp-t-console --vdi cp-t-vdi [--chrome PATH]
//
// Setup (see docker/web-console/README.md "Test"): an aooserver, the Console
// container (this image, published on 127.0.0.1 with RTC_PUBLIC_IP=127.0.0.1)
// and a second container of the same image running a VDI engine
//   crosspoint --headless --config vdi.yaml --api-port 0
// in the SAME group (the VDI container's gateway is not used).
//
//  downlink: a 440 Hz tone is played into the VDI's engine_in (the VDI engine
//            captures engine_in.monitor) -> AOO -> Console engine -> engine_out
//            -> gateway -> WebRTC -> headless Chrome: window.__level must rise.
//  uplink:   Chrome's fake mic (660 Hz) -> WebRTC -> gateway -> engine_in ->
//            Console engine -> AOO -> VDI engine -> VDI engine_out; we record
//            the VDI's engine_out.monitor and check RMS and dominant frequency.
import { spawn, execFileSync } from 'node:child_process';
import { writeFileSync, mkdtempSync, realpathSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const arg = (n, d) => { const i = process.argv.indexOf('--' + n); return i > 0 ? process.argv[i + 1] : d; };
const CON = arg('console', 'cp-t-console');
const VDI = arg('vdi', 'cp-t-vdi');
const BASE = arg('url', 'http://127.0.0.1:8090');
const CHROME = arg('chrome', '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome');
const PORT = 9333 + Math.floor(Math.random() * 500);
const sleep = ms => new Promise(r => setTimeout(r, ms));
let failed = false;
const check = (name, ok, info) => { console.log(`${ok ? 'PASS' : 'FAIL'} ${name} ${info ?? ''}`); if (!ok) failed = true; };
const dx = (c, args) => execFileSync('docker', ['exec', c, ...args], { maxBuffer: 1 << 28 });

function wav(freq, secs, rate = 44100) {
  const n = rate * secs, b = Buffer.alloc(44 + n * 4);
  b.write('RIFF', 0); b.writeUInt32LE(36 + n * 4, 4); b.write('WAVEfmt ', 8); b.writeUInt32LE(16, 16);
  b.writeUInt16LE(1, 20); b.writeUInt16LE(2, 22); b.writeUInt32LE(rate, 24); b.writeUInt32LE(rate * 4, 28);
  b.writeUInt16LE(4, 32); b.writeUInt16LE(16, 34); b.write('data', 36); b.writeUInt32LE(n * 4, 40);
  for (let i = 0; i < n; i++) { const v = Math.round(Math.sin(2 * Math.PI * freq * i / rate) * 0.5 * 32767); b.writeInt16LE(v, 44 + i * 4); b.writeInt16LE(v, 46 + i * 4); }
  return b;
}
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
const record = (c, dev, secs) => new Promise(res => {
  const p = spawn('docker', ['exec', c, 'timeout', String(secs), 'parec', '-d', dev, '--format=s16le', '--rate=48000', '--channels=1', '--raw']);
  const chunks = []; p.stdout.on('data', d => chunks.push(d)); p.on('close', () => res(Buffer.concat(chunks)));
});

const tmp = realpathSync(mkdtempSync(join(tmpdir(), 'rtc-e2e-engine-')));
const micWav = join(tmp, 'mic660.wav');
writeFileSync(micWav, wav(660, 120));
const chrome = spawn(CHROME, ['--headless=new', `--remote-debugging-port=${PORT}`, `--user-data-dir=${join(tmp, 'profile')}`,
  '--use-fake-ui-for-media-stream', '--use-fake-device-for-media-stream', `--use-file-for-fake-audio-capture=${micWav}`,
  '--autoplay-policy=no-user-gesture-required', '--no-first-run', '--disable-features=AudioServiceSandbox', 'about:blank'], { stdio: 'ignore' });

let ws, id = 0; const pending = new Map();
const cdp = (method, params = {}) => new Promise((res, rej) => { const i = ++id; pending.set(i, { res, rej }); ws.send(JSON.stringify({ id: i, method, params })); });
const ev = async expr => { const r = await cdp('Runtime.evaluate', { expression: expr, awaitPromise: true, returnByValue: true }); if (r.exceptionDetails) throw new Error(JSON.stringify(r.exceptionDetails)); return r.result.value; };
const startTone = () => spawn('docker', ['exec', VDI, 'sh', '-c',
  'echo $$ > /tmp/tone.pid; exec gst-launch-1.0 -q audiotestsrc wave=sine freq=440 volume=0.5 is-live=true ! audio/x-raw,rate=48000,channels=2 ! pulsesink device=engine_in'], { stdio: 'ignore' });
const stopTone = () => { try { dx(VDI, ['sh', '-c', '[ -f /tmp/tone.pid ] && kill $(cat /tmp/tone.pid) 2>/dev/null; rm -f /tmp/tone.pid']); } catch { /* gone */ } };

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
  check('peer connection connected', (await ev('window.__state')) === 'connected');

  // The uplink is recorded while the browser mic plays; the downlink with the VDI tone on.
  await sleep(1500);
  const quiet = await ev('window.__level');
  check('browser silent before the VDI tone', quiet < 0.005, quiet.toFixed(5));
  startTone();
  const tS = Date.now();
  while (Date.now() - tS < 15000) { try { dx(VDI, ['test', '-s', '/tmp/tone.pid']); break; } catch { await sleep(100); } }
  // The engine pair adds ~50-85 ms (L1, README "Latency"; measured by
  // tests/latency/run.sh). The generous 30 s wait only keeps a regression visible
  // as a large onset_delay_ms instead of a bare failure.
  let peak = 0, tRise = null; const t0 = Date.now();
  while (Date.now() - t0 < 30000) { const l = await ev('window.__level'); peak = Math.max(peak, l); if (tRise === null && l > 0.05) { tRise = Date.now() - t0; break; } await sleep(50); }
  check('VDI tone reaches the browser through engine + gateway', peak > 0.05, `peak=${peak.toFixed(3)} onset_delay_ms=${tRise}`);

  const pcm = await record(VDI, 'engine_out.monitor', 6);
  const a = analyse(pcm.subarray(48000));
  check('browser mic reaches the VDI output (RMS)', a.rms > 0.02, `rms=${a.rms.toFixed(4)} (${(20 * Math.log10(a.rms)).toFixed(1)} dBFS)`);
  check('browser mic reaches the VDI output (660 Hz)', Math.abs(a.freq - 660) <= 15, `${a.freq} Hz`);
  stopTone();
  let after = 1; const t1 = Date.now();
  while (Date.now() - t1 < 40000 && after >= 0.01) { await sleep(250); after = await ev('window.__level'); }
  check('browser level falls after the VDI tone stops', after < 0.01, `${after.toFixed(5)} after ${Date.now() - t1} ms`);
} catch (e) { check('test run', false, e.stack || e); }
finally {
  stopTone();
  try { ws?.close(); } catch { /* ignore */ }
  chrome.kill();
}
console.log(failed ? 'E2E FAILED' : 'E2E OK');
process.exit(failed ? 1 : 0);
