// Web-audio smoke test (P7.2 client): the real Console UI, the mock engine
// for state, and a REAL gateway container for audio.
//   docker run -d --name xp-rtc -e RTC_PUBLIC_IP=127.0.0.1 -p 127.0.0.1:8090:8090 \
//     -p 127.0.0.1:40000-40019:40000-40019/udp <gateway image>
//   RTC_PROXY=http://127.0.0.1:8090 node scripts/serve.mjs &
//   node scripts/smoke-rtc.mjs [--container xp-rtc]
import { spawn, execFileSync } from 'node:child_process';
import { mkdtempSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const arg = (n, d) => { const i = process.argv.indexOf('--' + n); return i > 0 ? process.argv[i + 1] : d; };
const CONTAINER = arg('container', 'xp-rtc');
const UI = process.env.UI ?? 'http://localhost:5173';
const CHROME = process.env.CHROME ?? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const sleep = ms => new Promise(r => setTimeout(r, ms));
let failures = 0;
const check = (ok, what) => { console.log(`${ok ? '✔' : '✖'} ${what}`); if (!ok) failures++; };

// 660 Hz fake mic (stereo 16-bit 44.1 kHz, what Chrome's fake capture reads)
const dir = mkdtempSync(join(tmpdir(), 'xp-rtc-'));
const rate = 44100, secs = 6, n = rate * secs, wav = Buffer.alloc(44 + n * 4);
wav.write('RIFF', 0); wav.writeUInt32LE(36 + n * 4, 4); wav.write('WAVEfmt ', 8); wav.writeUInt32LE(16, 16);
wav.writeUInt16LE(1, 20); wav.writeUInt16LE(2, 22); wav.writeUInt32LE(rate, 24); wav.writeUInt32LE(rate * 4, 28);
wav.writeUInt16LE(4, 32); wav.writeUInt16LE(16, 34); wav.write('data', 36); wav.writeUInt32LE(n * 4, 40);
for (let i = 0; i < n; i++) { const v = Math.round(Math.sin(2 * Math.PI * 660 * i / rate) * 0.5 * 32767); wav.writeInt16LE(v, 44 + i * 4); wav.writeInt16LE(v, 46 + i * 4); }
writeFileSync(join(dir, 'mic.wav'), wav);

const port = 9800 + Math.floor(Math.random() * 300);
const chrome = spawn(CHROME, ['--headless=new', '--disable-gpu', `--remote-debugging-port=${port}`, `--user-data-dir=${dir}/prof`,
  '--use-fake-ui-for-media-stream', '--use-fake-device-for-media-stream', `--use-file-for-fake-audio-capture=${dir}/mic.wav`,
  '--disable-features=AudioServiceSandbox', '--autoplay-policy=no-user-gesture-required', 'about:blank'], { stdio: 'ignore' });
let wsUrl;
for (let i = 0; i < 50 && !wsUrl; i++) { try { wsUrl = (await (await fetch(`http://127.0.0.1:${port}/json`)).json()).find(t => t.type === 'page')?.webSocketDebuggerUrl; } catch {} await sleep(100); }
const ws = new WebSocket(wsUrl); await new Promise(r => ws.addEventListener('open', r, { once: true }));
let seq = 0; const waiting = new Map();
ws.addEventListener('message', ev => { const m = JSON.parse(ev.data); if (m.id && waiting.has(m.id)) { waiting.get(m.id)(m); waiting.delete(m.id); } });
const send = (method, params = {}) => new Promise(r => { const id = ++seq; waiting.set(id, r); ws.send(JSON.stringify({ id, method, params })); });
const js = async (expr, gesture = false) => (await send('Runtime.evaluate', { expression: expr, awaitPromise: true, returnByValue: true, userGesture: gesture })).result?.result?.value;

// rawmic: browser noise suppression would remove the steady 660 Hz test tone
await send('Page.navigate', { url: `${UI}/?mock=everyday&rtc=1&rawmic=1` }); await sleep(1500);
check(await js(`!document.querySelector('.audiolink').hidden && document.querySelector('.audiolink').textContent.includes("Audio isn't connected")`), 'web Console offers "Start audio"');
await js(`document.querySelector('.audiolink .btn').click()`, true);
let state = '';
for (let i = 0; i < 40 && state !== 'connected'; i++) { await sleep(250); state = await js(`__rtcLinkState()`); }
check(state === 'connected', `WebRTC connects through the proxy (${state})`);
check(await js(`document.querySelector('.audiolink').hidden`), 'strip hides once audio is connected');

// downlink: a tone into the engine output reaches the browser
const tone = spawn('docker', ['exec', CONTAINER, 'sh', '-c', 'gst-launch-1.0 -q audiotestsrc freq=440 volume=0.5 num-buffers=300 ! pulsesink device=engine_out'], { stdio: 'ignore' });
await sleep(1800);
const lvl = await js(`__rtcLink.pc.getStats().then(s => { let a = 0; s.forEach(r => { if (r.type === 'inbound-rtp' && r.kind === 'audio') a = r.audioLevel ?? 0; }); return a; })`);
check(lvl > 0.05, `engine output tone reaches the browser (audioLevel ${lvl?.toFixed?.(3)})`);
tone.kill();

// uplink: the browser mic reaches the engine input
const rec = execFileSync('docker', ['exec', CONTAINER, 'sh', '-c', 'timeout 2 parec -d engine_in.monitor --format=s16le --channels=1 --rate=48000 2>/dev/null; true'], { maxBuffer: 1 << 24 });
let sum = 0; for (let i = 0; i + 1 < rec.length; i += 2) { const v = rec.readInt16LE(i) / 32768; sum += v * v; }
const rms = Math.sqrt(sum / (rec.length / 2));
check(rms > 0.05, `browser mic reaches the engine input (rms ${rms.toFixed(3)})`);

ws.close(); chrome.kill();
console.log(failures ? `\n${failures} failed` : '\nall passed');
process.exit(failures ? 1 : 0);
