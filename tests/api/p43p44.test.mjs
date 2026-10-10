// P4.3 + P4.4 integration test: meters stream and commands over the control API.
//   node --test tests/api/p43p44.test.mjs
// aooserver + 1 console (serving console-ui via --ui-dir) + 2 VDIs with
// SONOBUS_TEST_TONE_HZ (VDI-A runs with --config on a temp YAML), a third VDI
// is started for the offline-station cases. The last test drives the REAL
// console-ui in headless Chrome (CDP) against the REAL engine (set CHROME=...;
// skipped when Chrome is missing).

import test, { after, before } from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import dgram from 'node:dgram';
import fs from 'node:fs';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const APP = process.env.CROSSPOINT_APP ||
  path.join(ROOT, 'build/desktop-release/SonoBus_artefacts/Release/Standalone/Crosspoint.app/Contents/MacOS/Crosspoint');
const SERVER = process.env.AOOSERVER || path.join(ROOT, 'build/aooserver-release/aooserver');
const CHROME = process.env.CHROME ?? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const { Client, ApiError } = await import(pathToFileURL(path.join(ROOT, 'console-ui/src/api/client.js')).href);

const tmpRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'p43-'));
const procs = new Set();
const sleep = ms => new Promise(r => setTimeout(r, ms));

//== helpers ====================================================================
async function freePort() {
  for (let i = 0; i < 50; i++) {
    const port = await new Promise((resolve, reject) => {
      const s = net.createServer();
      s.listen(0, '127.0.0.1', () => { const { port } = s.address(); s.close(() => resolve(port)); });
      s.on('error', reject);
    });
    const ok = await new Promise(resolve => {
      const u = dgram.createSocket('udp4');
      u.once('error', () => resolve(false));
      u.bind(port, '127.0.0.1', () => u.close(() => resolve(true)));
    });
    if (ok) return port;
  }
  throw new Error('no free TCP+UDP port');
}

function launch(cmd, args, { home, cwd, env } = {}) {
  const proc = spawn(cmd, args, {
    env: { ...process.env, ...(home ? { CFFIXED_USER_HOME: home } : {}), ...env },
    cwd: cwd ?? home ?? tmpRoot,
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  procs.add(proc);
  const p = { proc, output: '', exited: null, pid: proc.pid };
  p.exit = new Promise(resolve => proc.on('exit', (code, signal) => { p.exited = { code, signal }; procs.delete(proc); resolve(p.exited); }));
  const collect = d => { p.output += d.toString(); };
  proc.stdout.on('data', collect);
  proc.stderr.on('data', collect);
  return p;
}

async function waitUntil(fn, ms, what) {
  const end = Date.now() + ms;
  for (;;) {
    let v;
    try { v = fn(); } catch { v = false; }
    if (v) return v;
    if (Date.now() > end) throw new Error('timeout waiting for ' + what);
    await sleep(30);
  }
}

async function stopProc(p, signal = 'SIGTERM') {
  if (p.exited) return p.exited;
  p.proc.kill(signal);
  const r = await Promise.race([p.exit, sleep(8000).then(() => null)]);
  if (!r) { p.proc.kill('SIGKILL'); throw new Error('process did not exit after ' + signal); }
  return r;
}

async function startPeer({ name, role, port, apiPort, home, env, extra = [], viaConfig }) {
  home ??= fs.mkdtempSync(path.join(tmpRoot, `home-${name}-`));
  const args = ['-q'];
  if (viaConfig) args.push('--config', viaConfig);
  else args.push('-c', `127.0.0.1:${port}`, '-g', 'p43', '-n', name, '--role', role);
  args.push('--api-port', String(apiPort), ...extra);
  const p = launch(APP, args, { home, env });
  p.name = name; p.home = home; p.apiPort = apiPort;
  await waitUntil(() => p.output.includes('Control API listening') || p.exited, 20000, `${name} API`);
  assert.ok(!p.exited, `${name} exited early: ${p.output}`);
  return p;
}

function connectClient(apiPort, expectRole) {
  const log = { msgs: [], sent: [] };
  const url = `ws://127.0.0.1:${apiPort}/api/v1/ws`;
  let ws;
  const open = () => {
    ws = new WebSocket(url);
    const t = {
      send: msg => { log.sent.push(msg); if (ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(msg)); },
      close: () => ws.close(1000),
      onmessage: () => {}, onclose: () => {},
    };
    ws.onmessage = ev => { const m = JSON.parse(ev.data); log.msgs.push(m); t.onmessage(m); };
    ws.onclose = ev => t.onclose(ev.code);
    return t;
  };
  const client = new Client(open, { expectRole });
  client.start();
  client.log = log;
  client.dropSocket = () => ws.close(1000);
  return client;
}

async function ready(c) {
  await waitUntil(() => c.status === 'ready', 10000, 'ready');
  await waitUntil(() => c.store.state?.mic || c.store.state?.sending, 10000, 'engine state');
  return c;
}

async function expectErr(promise, code) {
  try { await promise; } catch (e) {
    assert.ok(e instanceof ApiError, 'ApiError, got ' + e);
    assert.equal(e.code, code, `expected ${code}, got ${e.code}: ${e.message}`);
    return e;
  }
  assert.fail(`expected error ${code}`);
}

/** Raw WebSocket collecting meter frames. */
function rawMeters(apiPort) {
  const r = { frames: [], ws: new WebSocket(`ws://127.0.0.1:${apiPort}/api/v1/ws`), msgs: [] };
  r.ws.onmessage = ev => { const m = JSON.parse(ev.data); r.msgs.push(m); if (m.t === 'meters') r.frames.push({ ...m, at: Date.now() }); };
  r.send = m => r.ws.send(JSON.stringify(m));
  r.opened = new Promise(res => r.ws.addEventListener('open', res, { once: true }));
  return r;
}

const peakOf = v => (Array.isArray(v) ? v[0] : null);

//== fixture ====================================================================
let serverProc, port, consoleApp, vdiA, vdiB, vdiC, apiPorts, consoleClient, yamlPath, yamlOriginal;
const A = 'VDI-A', B = 'VDI-B', C = 'VDI-C';
const TONE = { SONOBUS_TEST_TONE_HZ: '440' };
const stations = () => consoleClient.store.state.stations;
const waitState = (fn, ms, what) => waitUntil(() => fn(consoleClient.store.state), ms, what);
const cmd = (name, args) => consoleClient.cmd(name, args);

before(async () => {
  assert.ok(fs.existsSync(APP), `app not built: ${APP}`);
  assert.ok(fs.existsSync(SERVER), `aooserver not built: ${SERVER}`);
  port = await freePort();
  const logDir = path.join(tmpRoot, 'serverlog');
  fs.mkdirSync(logDir);
  serverProc = launch(SERVER, ['-p', String(port), '-l', logDir], { cwd: tmpRoot });
  for (let i = 0; i < 100; i++) {
    const ok = await new Promise(r => { const s = net.connect(port, '127.0.0.1', () => { s.destroy(); r(true); }); s.on('error', () => r(false)); });
    if (ok) break;
    assert.ok(!serverProc.exited, 'aooserver exited: ' + serverProc.output);
    await sleep(100);
  }
  apiPorts = { console: await freePort(), A: await freePort(), B: await freePort(), C: await freePort() };
  consoleApp = await startPeer({ name: 'console-1', role: 'console', port, apiPort: apiPorts.console,
    extra: ['--ui-dir', path.join(ROOT, 'console-ui')] });
  yamlPath = path.join(tmpRoot, 'vdi-a.yaml');
  yamlOriginal = `# VDI-A agent config (test)\nserver: 127.0.0.1:${port}   # the server\ngroup: p43\nusername: ${A}\nrole: vdi\n\n# codec stays default\ncodec: opus\n`;
  fs.writeFileSync(yamlPath, yamlOriginal);
  vdiA = await startPeer({ name: A, role: 'vdi', port, apiPort: apiPorts.A, env: TONE, viaConfig: yamlPath });
  vdiB = await startPeer({ name: B, role: 'vdi', port, apiPort: apiPorts.B, env: TONE });
  consoleClient = await ready(connectClient(apiPorts.console, 'console'));
  for (const id of [A, B]) await waitState(s => s.stations[id]?.presence === 'online', 30000, `${id} online`);
});

after(async () => {
  consoleClient?.stop();
  for (const p of [...procs]) { try { p.kill('SIGKILL'); } catch {} }
  await sleep(300);
  try { fs.rmSync(tmpRoot, { recursive: true, force: true, maxRetries: 5 }); } catch {}
});

//== P4.3 meters ================================================================
test('meters: hello advertises meters/ptt/commands; no frames before sub; ~30 fps after', async () => {
  assert.ok(['meters', 'ptt', 'commands'].every(f => consoleClient.hello.features.includes(f)), JSON.stringify(consoleClient.hello.features));
  const r = rawMeters(apiPorts.console);
  await r.opened;
  await sleep(1200);
  assert.equal(r.frames.length, 0, 'no meters before sub');
  r.send({ t: 'sub', topics: ['meters'] });
  await waitUntil(() => r.frames.length > 0, 3000, 'first frame');
  await sleep(300);
  const t0 = Date.now(), n0 = r.frames.length;
  await sleep(2000);
  const fps = (r.frames.length - n0) / ((Date.now() - t0) / 1000);
  assert.ok(fps > 24 && fps < 40, `fps ${fps.toFixed(1)}`);
  const f = r.frames.at(-1);
  assert.equal(typeof f.ts, 'number');
  for (const id of [A, B]) {
    const p = peakOf(f.stations[id]);
    assert.ok(typeof p === 'number' && p > -16 && p < -8, `${id} pre-fader peak ${p}`);
    assert.equal(f.stations[id].length, 2);
    assert.ok(Math.abs(p * 10 - Math.round(p * 10)) < 1e-6, 'rounded to 0.1');
  }
  assert.ok(Array.isArray(f.mic) && f.mic.length === 2 && Array.isArray(f.output) && f.output.length === 2);
  for (const v of [...f.mic, ...f.output]) assert.ok(v === null || (typeof v === 'number' && v >= -90 && v <= 6), 'level ' + v);

  // visibility hidden pauses, false resumes, unsub stops
  r.send({ t: 'visibility', hidden: true });
  await sleep(300);
  const h0 = r.frames.length;
  await sleep(700);
  assert.equal(r.frames.length, h0, 'hidden: no frames');
  r.send({ t: 'visibility', hidden: false });
  await waitUntil(() => r.frames.length > h0, 2000, 'resumed');
  r.send({ t: 'unsub', topics: ['meters'] });
  await sleep(300);
  const u0 = r.frames.length;
  await sleep(500);
  assert.equal(r.frames.length, u0, 'unsub: no frames');
  r.ws.close();
});

test('meters are pre-fader: station.setMute and setLevel keep the meter, state changes', async () => {
  const r = rawMeters(apiPort_console());
  await r.opened;
  r.send({ t: 'sub', topics: ['meters'] });
  await cmd('station.setMute', { station: A, on: true });
  await cmd('station.setLevel', { station: A, db: -40 });
  await waitState(s => s.stations[A].mute === true && s.stations[A].level === -40, 3000, 'mute patch');
  await sleep(800);
  const p = peakOf(r.frames.at(-1).stations[A]);
  assert.ok(typeof p === 'number' && p > -16 && p < -8, `muted/-40 dB station still shows ${p}`);
  assert.equal(stations()[A].hearsYou, true, 'mute does not change hearsYou');
  await cmd('station.setMute', { station: A, on: false });
  await cmd('station.setLevel', { station: A, db: 0 });
  r.ws.close();
});
function apiPort_console() { return apiPorts.console; }

//== P4.4 console commands ======================================================
test('station.* setters: effects in the next patch, clamping, errors', async () => {
  await cmd('station.setLevel', { station: A, db: -6 });
  await waitState(s => s.stations[A].level === -6, 3000, 'level -6');
  await cmd('station.setLevel', { station: A, db: 99 });
  await waitState(s => s.stations[A].level === 6, 3000, 'level clamp +6');
  await cmd('station.setLevel', { station: A, db: -99 });
  await waitState(s => s.stations[A].level === -40, 3000, 'level clamp -40');
  await cmd('station.setLevel', { station: A, db: 0 });
  await cmd('station.setPan', { station: A, pan: 0.5 });
  await waitState(s => s.stations[A].pan === 0.5, 3000, 'pan 0.5');
  await cmd('station.setPan', { station: A, pan: -7 });
  await waitState(s => s.stations[A].pan === -1, 3000, 'pan clamp');
  await cmd('station.setPan', { station: A, pan: 0 });

  await cmd('station.setTalk', { station: B, on: false });
  await waitState(s => s.stations[B].talk === false && s.stations[B].hearsYou === false && s.stations[A].hearsYou === true, 3000, 'talk off');
  await cmd('station.setTalk', { station: B, on: true });
  await waitState(s => s.stations[B].hearsYou === true, 3000, 'talk on');

  await cmd('station.setSolo', { station: A, on: true });
  await waitState(s => s.stations[A].solo && s.stations[B].hearsYou === false && s.stations[A].hearsYou, 3000, 'solo narrows');
  await cmd('station.setSolo', { station: A, on: false });
  await waitState(s => !s.stations[A].solo && s.stations[B].hearsYou, 3000, 'unsolo');

  await expectErr(cmd('station.setLevel', { station: A, db: 'loud' }), 'bad_request');
  await expectErr(cmd('station.setPan', { station: A }), 'bad_request');
  await expectErr(cmd('station.setMute', { station: A, on: 'yes' }), 'bad_request');
  await expectErr(cmd('station.setSolo', { station: A, on: 1 }), 'bad_request');
  await expectErr(cmd('station.setTalk', { station: A }), 'bad_request');
  await expectErr(cmd('station.setLevel', { station: 7, db: 0 }), 'bad_request');
  await expectErr(cmd('station.setLevel', { station: 'VDI-NOPE', db: 0 }), 'not_found');
  await expectErr(cmd('station.setSolo', { station: 'VDI-NOPE', on: true }), 'not_found');
  await expectErr(cmd('station.forget', { station: A }), 'busy');
  await expectErr(cmd('station.forget', { station: 'VDI-NOPE' }), 'not_found');
  await expectErr(cmd('nope.nothing', {}), 'bad_request');
  await expectErr(cmd('agent.pause', {}), 'bad_request');
});

test('stations.talkToAll / spread / centerAll and an offline station (setLevel remembered, solo busy, forget)', async () => {
  vdiC = await startPeer({ name: C, role: 'vdi', port, apiPort: apiPorts.C });
  await waitState(s => s.stations[C]?.presence === 'online', 30000, 'C online');

  await cmd('station.setTalk', { station: A, on: false });
  await cmd('station.setTalk', { station: B, on: false });
  await cmd('stations.talkToAll', {});
  await waitState(s => Object.values(s.stations).every(x => x.talk), 3000, 'talkToAll');

  await cmd('stations.spread', {});
  await waitState(s => s.stationOrder.length === 3 && s.stationOrder.every((id, i) => s.stations[id].pan === [-1, 0, 1][i]), 3000, 'spread of 3');
  await cmd('stations.centerAll', {});
  await waitState(s => Object.values(s.stations).every(x => x.pan === 0), 3000, 'centerAll');

  await stopProc(vdiC);
  await waitState(s => s.stations[C].presence === 'offline', 30000, 'C offline');
  await cmd('station.setLevel', { station: C, db: -12 });
  await cmd('station.setMute', { station: C, on: true });
  await waitState(s => s.stations[C].level === -12 && s.stations[C].mute === true, 3000, 'offline mix remembered');
  await expectErr(cmd('station.setSolo', { station: C, on: true }), 'busy');
  await cmd('stations.spread', {});            // offline stations are in stationOrder
  await waitState(s => s.stationOrder.length === 3 && s.stationOrder.every((id, i) => s.stations[id].pan === [-1, 0, 1][i]), 3000, 'spread incl. offline');
  await cmd('stations.centerAll', {});
  await cmd('station.forget', { station: C });
  await waitState(s => !s.stations[C] && !s.stationOrder.includes(C), 3000, 'forgotten');
  await expectErr(cmd('station.forget', { station: C }), 'not_found');
});

test('mic.*: setMode, setOn (wrong_mode in ptt), output.setLevel, settings.set, devices.*', async () => {
  await expectErr(cmd('mic.setMode', { mode: 'loud' }), 'bad_request');
  await expectErr(cmd('mic.setOn', { on: 'x' }), 'bad_request');
  await cmd('mic.setOn', { on: false });
  await waitState(s => !s.mic.on && !s.mic.transmitting && Object.values(s.stations).every(x => !x.hearsYou), 3000, 'mic off');
  await cmd('mic.setOn', { on: true });
  await waitState(s => s.mic.transmitting && s.stations[A].hearsYou, 3000, 'mic on');
  await cmd('mic.setMode', { mode: 'ptt' });
  await waitState(s => s.mic.mode === 'ptt' && !s.mic.transmitting && !s.stations[A].hearsYou, 3000, 'ptt mode');
  await expectErr(cmd('mic.setOn', { on: false }), 'wrong_mode');
  await cmd('mic.setMode', { mode: 'open' });
  await waitState(s => s.mic.mode === 'open' && s.mic.transmitting, 3000, 'open mode');

  await cmd('output.setLevel', { db: -10 });
  await waitState(s => s.output.level === -10, 3000, 'output -10');
  await cmd('output.setLevel', { db: 40 });
  await waitState(s => s.output.level === 6, 3000, 'output clamp');
  await cmd('output.setLevel', { db: 0 });
  await waitState(s => s.output.level === 0, 3000, 'output 0');
  await expectErr(cmd('output.setLevel', { db: 'x' }), 'bad_request');

  await cmd('settings.set', { soloDimDb: -12 });
  await waitState(s => s.settings.soloDimDb === -12, 3000, 'soloDim');
  await cmd('settings.set', { soloDimDb: -18 });
  await expectErr(cmd('settings.set', { soloDimDb: 'x' }), 'bad_request');
  await expectErr(cmd('settings.set', { pttHotkey: 'F5' }), 'not_supported');
  await expectErr(cmd('settings.set', { networkBuffer: { mode: 'auto' } }), 'not_supported');
  await expectErr(cmd('settings.set', { bitrateKbps: 77 }), 'bad_request');
  await expectErr(cmd('settings.set', { colour: 'red' }), 'bad_request');
  await cmd('settings.set', { bitrateKbps: 64 });
  await waitState(s => s.settings.bitrateKbps === 64 && s.settings.codec === 'opus', 3000, 'bitrate 64');
  await cmd('settings.set', { codec: 'pcm' });
  await waitState(s => s.settings.codec === 'pcm', 3000, 'pcm');
  await cmd('settings.set', { codec: 'opus', bitrateKbps: 96 });
  await waitState(s => s.settings.codec === 'opus' && s.settings.bitrateKbps === 96, 3000, 'opus 96');

  const st = consoleClient.store.state;
  for (const [c, list, cur] of [['devices.setInput', st.devices.inputs, st.mic.device], ['devices.setOutput', st.devices.outputs, st.output.device]]) {
    const field = s => (c === 'devices.setInput' ? s.mic.device : s.output.device);
    // Not every device pair on a dev machine can run together (sample rates): take the first that opens.
    let used = cur;
    for (const d of list.filter(d => d.id !== cur)) {
      try { await cmd(c, { id: d.id }); used = d.id; break; } catch (e) { assert.equal(e.code, 'internal', e.message); }
    }
    await waitState(s => field(s) === used, 8000, c);
    if (used !== cur) { await cmd(c, { id: cur }); await waitState(s => field(s) === cur, 8000, c + ' back'); }
    else await cmd(c, { id: cur });
    await expectErr(cmd(c, { id: 'No Such Device' }), 'not_found');
    await expectErr(cmd(c, {}), 'bad_request');
  }
});

//== PTT ========================================================================
test('mic.ptt: held while any client holds it; released when the holder disconnects', async () => {
  const a = await ready(connectClient(apiPorts.console, 'console'));
  const b = await ready(connectClient(apiPorts.console, 'console'));
  try {
    await expectErr(a.cmd('mic.ptt', { down: 'yes' }), 'bad_request');
    await a.cmd('mic.setMode', { mode: 'ptt' });
    await waitUntil(() => b.store.state.mic.mode === 'ptt' && !b.store.state.mic.transmitting, 3000, 'ptt mode');
    await a.cmd('mic.ptt', { down: true });
    await waitUntil(() => b.store.state.mic.pttHeld && b.store.state.mic.transmitting && b.store.state.stations[A].hearsYou, 3000, 'B sees held');
    await b.cmd('mic.ptt', { down: true });
    await a.cmd('mic.ptt', { down: false });
    await sleep(500);
    assert.equal(b.store.state.mic.pttHeld, true, 'still held by B');
    await b.cmd('mic.ptt', { down: false });
    await waitUntil(() => !a.store.state.mic.pttHeld, 3000, 'released');
    await a.cmd('mic.ptt', { down: true });
    await waitUntil(() => b.store.state.mic.pttHeld, 3000, 'A holds again');
    a.dropSocket(); a.stop();
    await waitUntil(() => !b.store.state.mic.pttHeld && !b.store.state.mic.transmitting, 5000, 'released on disconnect');
    await b.cmd('mic.setMode', { mode: 'open' });
    await waitUntil(() => b.store.state.mic.mode === 'open', 3000, 'open again');
  } finally { a.stop(); b.stop(); }
});

//== agent ======================================================================
test('agent: setInput/setOutput with previous + YAML write-back, pause/resume, testTone, reloadConfig, retryNow', async () => {
  const ag = await ready(connectClient(apiPorts.A, 'vdi'));
  try {
    assert.ok(['meters', 'commands'].every(f => ag.hello.features.includes(f)));
    const st = () => ag.store.state;
    await expectErr(ag.cmd('agent.setInput', {}), 'bad_request');
    await expectErr(ag.cmd('agent.setInput', { node: 'No Such Mic' }), 'not_found');
    await expectErr(ag.cmd('station.setLevel', { station: A, db: 0 }), 'bad_request');

    // input switch + YAML
    const curIn = st().input.node, curOut = st().output.node;
    let other = curIn, r;
    for (const d of st().devices.inputs.filter(d => d.node !== curIn)) {
      try { r = await ag.cmd('agent.setInput', { node: d.node }); other = d.node; break; } catch (e) { assert.equal(e.code, 'internal', e.message); }
    }
    if (other === curIn) r = await ag.cmd('agent.setInput', { node: curIn });
    assert.equal(r.previous, curIn);
    await waitUntil(() => st().input.node === other, 8000, 'input switched');
    let yaml = fs.readFileSync(yamlPath, 'utf8');
    assert.ok(yaml.includes(other) && yaml.includes('input_device'), 'YAML has the new input\n' + yaml);
    for (const line of yamlOriginal.split('\n').filter(Boolean)) assert.ok(yaml.includes(line), 'kept: ' + line);
    const r2 = await ag.cmd('agent.setOutput', { node: curOut });
    assert.equal(r2.previous, curOut);
    assert.ok(fs.readFileSync(yamlPath, 'utf8').includes('output_device'));
    const back = await ag.cmd('agent.setInput', { node: curIn });
    assert.equal(back.previous, other);
    assert.ok(fs.readFileSync(yamlPath, 'utf8').includes(curIn));
    await waitUntil(() => st().input.node === curIn, 8000, 'input restored');

    // pause / resume
    await ag.cmd('agent.pause', {});
    await waitUntil(() => st().sending === 'paused', 3000, 'paused');
    await waitState(s => s.stations[A].agent?.paused === true, 5000, 'console sees paused');
    const r1 = rawMeters(apiPorts.console); await r1.opened; r1.send({ t: 'sub', topics: ['meters'] });
    await waitUntil(() => r1.frames.length && r1.frames.at(-1).stations[A][0] === null, 5000, 'paused station silent');
    assert.ok(peakOf(r1.frames.at(-1).stations[B]) > -16, 'B unaffected');
    await ag.cmd('agent.resume', {});
    await waitUntil(() => st().sending !== 'paused', 3000, 'resumed');
    await waitUntil(() => peakOf(r1.frames.at(-1).stations[A]) > -16, 5000, 'station audible again');
    r1.ws.close();

    // test tone raises the output meter
    const m = rawMeters(apiPorts.A); await m.opened; m.send({ t: 'sub', topics: ['meters', 'deviceMeters'] });
    await waitUntil(() => m.frames.length > 5, 3000, 'agent frames');
    const f = m.frames.at(-1);
    assert.ok(f.output[0] === null || f.output[0] < -60, 'output quiet before the tone: ' + f.output);
    assert.ok(f.input.length === 2 && typeof f.devices === 'object', 'input + devices');
    await expectErr(ag.cmd('agent.testTone', { node: 'No Such Out' }), 'not_found');
    await ag.cmd('agent.testTone', { node: curOut });
    await waitUntil(() => m.frames.some(x => { const p = peakOf(x.output); return p !== null && p > -20 && p < -16; }), 3000, 'tone ~-18 dBFS');
    await waitUntil(() => { const p = peakOf(m.frames.at(-1).output); return p === null || p < -60; }, 5000, 'tone ended');
    m.ws.close();

    // retryNow (already connected: no-op), reloadConfig
    await ag.cmd('agent.retryNow', {});
    assert.equal(st().connection.state, 'connected');
    fs.appendFileSync(yamlPath, 'bogus_key: 12345\n');
    await expectErr(ag.cmd('agent.reloadConfig', {}), 'bad_request');
    await waitUntil(() => st().configError && st().connection.error?.code === 'config', 3000, 'configError shown');
    fs.writeFileSync(yamlPath, fs.readFileSync(yamlPath, 'utf8').replace('bogus_key: 12345\n', ''));
    await ag.cmd('agent.reloadConfig', {});
    await waitUntil(() => st().configError === null && st().connection.error === null, 3000, 'configError cleared');
    assert.equal(st().connection.state, 'connected');
  } finally { ag.stop(); }
});

//== connection =================================================================
test('connection.disconnect / connect: stations go offline and come back; bad args', async () => {
  await expectErr(cmd('connection.connect', { group: '' }), 'bad_request');
  await expectErr(cmd('connection.connect', { server: 5 }), 'bad_request');
  await cmd('connection.disconnect', {});
  await waitState(s => s.connection.state === 'failed' && s.connection.reason === 'Disconnected' && s.stations[A].presence === 'offline', 8000, 'disconnected');
  await cmd('connection.connect', {});
  await waitState(s => s.connection.state === 'connected' && s.connection.group === 'p43', 15000, 'reconnected');
  await waitState(s => s.stations[A].presence === 'online' && s.stations[B].presence === 'online', 40000, 'stations back');
});

//== the real console-ui against the real engine ================================
test('real console-ui <-> real engine (headless Chrome over CDP): Solo, Mute, Level, mic', { skip: !fs.existsSync(CHROME) && 'Chrome not found (set CHROME)' }, async () => {
  const cport = 9300 + Math.floor(Math.random() * 500);
  const chrome = spawn(CHROME, ['--headless=new', '--disable-gpu', `--remote-debugging-port=${cport}`,
    `--user-data-dir=${fs.mkdtempSync(path.join(tmpRoot, 'chrome-'))}`, '--window-size=1280,860', 'about:blank'], { stdio: 'ignore' });
  let ws;
  try {
    let wsUrl;
    await waitUntil(async () => false, 1).catch(() => {});
    for (let i = 0; i < 60 && !wsUrl; i++) {
      try { wsUrl = (await (await fetch(`http://127.0.0.1:${cport}/json`)).json()).find(t => t.type === 'page')?.webSocketDebuggerUrl; } catch {}
      if (!wsUrl) await sleep(150);
    }
    assert.ok(wsUrl, 'Chrome started');
    ws = new WebSocket(wsUrl);
    await new Promise(r => ws.addEventListener('open', r, { once: true }));
    let seq = 0; const waiting = new Map(); const errors = [];
    ws.addEventListener('message', ev => {
      const m = JSON.parse(ev.data);
      if (m.id && waiting.has(m.id)) { waiting.get(m.id)(m); waiting.delete(m.id); }
      if (m.method === 'Runtime.exceptionThrown') errors.push(m.params.exceptionDetails.exception?.description ?? m.params.exceptionDetails.text);
    });
    const send = (method, params = {}) => new Promise(r => { const id = ++seq; waiting.set(id, r); ws.send(JSON.stringify({ id, method, params })); });
    const js = async expr => (await send('Runtime.evaluate', { expression: expr, awaitPromise: true, returnByValue: true })).result?.result?.value;
    await send('Runtime.enable'); await send('Page.enable');
    await send('Page.navigate', { url: `http://127.0.0.1:${apiPorts.console}/` });
    await waitUntil(async () => false, 1).catch(() => {});
    let status;
    for (let i = 0; i < 100 && status !== 'ready'; i++) { await sleep(150); status = await js('globalThis.__crosspoint?.status'); }
    assert.equal(status, 'ready', 'UI client reached ready against the real engine');
    const uiState = async () => JSON.parse(await js('JSON.stringify(__crosspoint.store.state)'));
    assert.equal((await uiState()).self.role, 'console');
    assert.ok(Object.keys((await uiState()).stations).includes(A));

    // Solo on the first station card
    const first = (await uiState()).stationOrder[0];
    await js(`document.querySelector('.station .tog.solo').click()`);
    await waitState(s => s.stations[first].solo === true, 4000, 'engine solo after UI click');
    assert.equal((await uiState()).stations[first].solo, true, 'UI state patched back');
    await js(`document.querySelector('.station .tog.solo').click()`);
    await waitState(s => s.stations[first].solo === false, 4000, 'engine unsolo');
    // Mute
    await js(`document.querySelector('.station .tog.mute').click()`);
    await waitState(s => s.stations[first].mute === true, 4000, 'engine mute after UI click');
    await js(`document.querySelector('.station .tog.mute').click()`);
    await waitState(s => s.stations[first].mute === false, 4000, 'engine unmute');
    // Level slider
    await js(`(() => { const r = document.querySelector('.station input[type=range]'); r.value = '-9'; r.dispatchEvent(new Event('input', { bubbles: true })); })()`);
    await waitState(s => s.stations[first].level === -9, 4000, 'engine level -9 after UI slider');
    await js(`(() => { const r = document.querySelector('.station input[type=range]'); r.value = '0'; r.dispatchEvent(new Event('input', { bubbles: true })); })()`);
    await waitState(s => s.stations[first].level === 0, 4000, 'engine level 0');
    // mic: the ` key toggles the mic, Push to talk + Space
    await send('Input.dispatchKeyEvent', { type: 'keyDown', key: '`', code: 'Backquote', text: '`' });
    await send('Input.dispatchKeyEvent', { type: 'keyUp', key: '`', code: 'Backquote' });
    await waitState(s => s.mic.on === false, 4000, 'engine mic off after UI key');
    await send('Input.dispatchKeyEvent', { type: 'keyDown', key: '`', code: 'Backquote', text: '`' });
    await send('Input.dispatchKeyEvent', { type: 'keyUp', key: '`', code: 'Backquote' });
    await waitState(s => s.mic.on === true, 4000, 'engine mic on');
    await js(`[...document.querySelectorAll('.seg button')].find(b => b.textContent === 'Push to talk').click()`);
    await waitState(s => s.mic.mode === 'ptt', 4000, 'engine ptt mode');
    await send('Input.dispatchKeyEvent', { type: 'keyDown', key: ' ', code: 'Space', text: ' ' });
    await waitState(s => s.mic.transmitting === true, 4000, 'engine transmitting while Space held');
    await send('Input.dispatchKeyEvent', { type: 'keyUp', key: ' ', code: 'Space' });
    await waitState(s => s.mic.transmitting === false, 4000, 'engine released');
    await js(`[...document.querySelectorAll('.seg button')].find(b => b.textContent === 'Open mic').click()`);
    await waitState(s => s.mic.mode === 'open', 4000, 'engine open mode');
    // meters reach the UI
    const metersSeen = await js(`new Promise(res => { let n = 0; __crosspoint.onMeters(() => { if (++n > 10) res(n); }); setTimeout(() => res(n), 3000); })`);
    assert.ok(metersSeen > 10, 'UI received meters frames: ' + metersSeen);
    assert.deepEqual(errors, [], 'no page errors');
  } finally { try { ws?.close(); } catch {} chrome.kill(); }
});
