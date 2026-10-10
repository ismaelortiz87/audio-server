// P4.2 integration test: engine state snapshot and patches over the control API.
//   node --test tests/api/p42.test.mjs
// Starts a local aooserver and real headless peers (1 console + 2 vdi, same
// group, each with an isolated CFFIXED_USER_HOME), connects the real
// console-ui client (src/api/client.js) to the console's API and to a VDI's.
// Needs: scripts/build-desktop.sh (build/aooserver-release/aooserver and the app).
// Override binaries with CROSSPOINT_APP / AOOSERVER.

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
const { Client, ApiError } = await import(pathToFileURL(path.join(ROOT, 'console-ui/src/api/client.js')).href);

const tmpRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'p42-'));
const procs = new Set();
const sleep = ms => new Promise(r => setTimeout(r, ms));

//== helpers ====================================================================
async function freePort() {
  // the AOO server needs the same port free for TCP and UDP
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

function launch(cmd, args, { home, cwd } = {}) {
  const proc = spawn(cmd, args, {
    env: { ...process.env, ...(home ? { CFFIXED_USER_HOME: home } : {}) },
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
    await sleep(40);
  }
}

async function stopProc(p, signal = 'SIGTERM') {
  if (p.exited) return p.exited;
  p.proc.kill(signal);
  const r = await Promise.race([p.exit, sleep(8000).then(() => null)]);
  if (!r) { p.proc.kill('SIGKILL'); throw new Error('process did not exit after ' + signal); }
  return r;
}

/** A peer: `-q` headless app joined to the group, API on its own port. */
async function startPeer({ name, role, port, apiPort, home }) {
  home ??= fs.mkdtempSync(path.join(tmpRoot, `home-${name}-`));
  const p = launch(APP, ['-q', '-c', `127.0.0.1:${port}`, '-g', 'p42', '-n', name, '--role', role,
    '--api-port', String(apiPort)], { home });
  p.name = name; p.home = home; p.apiPort = apiPort;
  await waitUntil(() => p.output.includes('Control API listening') || p.exited, 20000, `${name} API`);
  assert.ok(!p.exited, `${name} exited early: ${p.output}`);
  return p;
}

/** Real console-ui Client over a WebSocket, recording every message and every resync. */
function connectClient(apiPort, expectRole) {
  const log = { msgs: [], sent: [] };
  const url = `ws://127.0.0.1:${apiPort}/api/v1/ws`;
  const open = () => {
    const ws = new WebSocket(url);
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
  return client;
}

const isIso = s => typeof s === 'string' && /^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ$/.test(s);
const numOrNull = v => v === null || (typeof v === 'number' && Number.isFinite(v));

/** Console state shape, docs/control-api.md 3.2. */
function checkConsoleShape(st) {
  assert.equal(st.self.role, 'console');
  assert.equal(typeof st.self.name, 'string');
  assert.ok(['mac', 'web', 'other'].includes(st.self.kind), 'self.kind');
  const c = st.connection;
  assert.ok(['connecting', 'connected', 'reconnecting', 'failed'].includes(c.state), 'connection.state ' + c.state);
  assert.equal(typeof c.server, 'string');
  assert.equal(typeof c.group, 'string');
  assert.equal(typeof c.passwordSaved, 'boolean');
  assert.ok(c.reason === null || typeof c.reason === 'string');
  assert.ok(numOrNull(c.retryInSec));
  assert.ok(Array.isArray(st.stationOrder));
  assert.deepEqual([...st.stationOrder].sort(), Object.keys(st.stations).sort(), 'stationOrder lists every station');
  for (const [id, s] of Object.entries(st.stations)) {
    assert.equal(s.id, id);
    assert.equal(typeof s.name, 'string');
    assert.ok(Number.isInteger(s.colorIndex) && s.colorIndex >= 0 && s.colorIndex <= 3, 'colorIndex');
    assert.ok(['online', 'lost', 'offline'].includes(s.presence), 'presence');
    assert.ok(s.presence === 'lost' ? Number.isInteger(s.lostForSec) : s.lostForSec === null, 'lostForSec');
    assert.ok(isIso(s.lastSeen), 'lastSeen ' + s.lastSeen);
    assert.ok(['clear', 'unstable'].includes(s.health), 'health');
    for (const k of ['latencyMs', 'lossPct', 'jitterBufferMs']) {
      assert.ok(numOrNull(s[k]), k);
      if (s.presence !== 'online') assert.equal(s[k], null, `${k} null when ${s.presence}`);
    }
    if (s.agent !== null) {
      assert.ok(['ok', 'missing', 'silent'].includes(s.agent.input), 'agent.input');
      assert.ok(['ok', 'missing'].includes(s.agent.output), 'agent.output');
      assert.equal(typeof s.agent.paused, 'boolean');
      assert.ok(s.agent.configError === null || typeof s.agent.configError === 'string');
      for (const k of ['inputNode', 'silentForMin', 'outputNode']) assert.ok(k in s.agent, 'agent.' + k);
    }
    assert.ok(s.level >= -40 && s.level <= 6, 'level');
    assert.ok(typeof s.pan === 'number');
    for (const k of ['mute', 'solo', 'talk', 'hearsYou']) assert.equal(typeof s[k], 'boolean', k);
  }
  assert.ok(Array.isArray(st.unknownPeers) && Array.isArray(st.otherConsoles));
  assert.ok(['open', 'ptt'].includes(st.mic.mode));
  for (const k of ['on', 'pttHeld', 'transmitting']) assert.equal(typeof st.mic[k], 'boolean', 'mic.' + k);
  assert.equal(typeof st.mic.device, 'string');
  assert.equal(typeof st.output.device, 'string');
  assert.ok(typeof st.output.level === 'number');
  assert.ok(typeof st.settings.soloDimDb === 'number');
  assert.ok(st.settings.pttHotkey === null || typeof st.settings.pttHotkey === 'string');
  assert.ok(['opus', 'pcm'].includes(st.settings.codec));
  assert.ok(numOrNull(st.settings.bitrateKbps));
  assert.ok(['auto', 'manual'].includes(st.settings.networkBuffer.mode));
  assert.ok(typeof st.settings.networkBuffer.currentMs === 'number');
  for (const list of [st.devices.inputs, st.devices.outputs]) {
    assert.ok(Array.isArray(list));
    for (const d of list) { assert.equal(typeof d.id, 'string'); assert.equal(typeof d.name, 'string'); }
  }
}

/** Agent state shape, docs/control-api.md 3.3. */
function checkAgentShape(st) {
  assert.equal(st.self.role, 'vdi');
  assert.equal(typeof st.self.name, 'string');
  const c = st.connection;
  assert.ok(['connecting', 'connected', 'reconnecting', 'error'].includes(c.state), 'connection.state ' + c.state);
  for (const k of ['server', 'group']) assert.equal(typeof c[k], 'string', k);
  for (const k of ['attempt', 'retryInSec']) assert.ok(numOrNull(c[k]), k);
  assert.ok(c.error === null || (typeof c.error.code === 'string' && typeof c.error.message === 'string'));
  assert.ok(['active', 'paused', 'idle'].includes(st.sending));
  assert.ok(Array.isArray(st.consoles));
  for (const k of st.consoles) {
    assert.equal(typeof k.name, 'string');
    assert.ok(['mac', 'web', 'other'].includes(k.kind));
    assert.ok(numOrNull(k.latencyMs));
    assert.equal(typeof k.talking, 'boolean');
  }
  assert.ok(['ok', 'missing', 'silent'].includes(st.input.status));
  assert.ok(['ok', 'missing'].includes(st.output.status));
  for (const k of ['node', 'description']) { assert.equal(typeof st.input[k], 'string'); assert.equal(typeof st.output[k], 'string'); }
  assert.ok(numOrNull(st.input.silentForMin));
  for (const list of [st.devices.inputs, st.devices.outputs]) {
    assert.ok(Array.isArray(list));
    for (const d of list) { assert.equal(typeof d.node, 'string'); assert.equal(typeof d.description, 'string'); }
  }
  assert.ok(st.configPath === null || typeof st.configPath === 'string');
  assert.ok(st.configError === null || typeof st.configError === 'string');
}

/** Every state/patch message must follow the previous rev by exactly 1, within a connection. */
function assertConsecutive(msgs) {
  let rev = null;
  for (const m of msgs) {
    if (m.t === 'state') rev = m.rev;
    else if (m.t === 'patch') { assert.equal(m.rev, rev + 1, `rev gap before ${m.rev}`); rev = m.rev; }
  }
}

//== fixture ====================================================================
let serverProc, port, consoleApp, vdiA, vdiB, consoleHome;
let apiPorts;
let consoleClient;
const ids = { A: 'VDI-A', B: 'VDI-B' };

before(async () => {
  assert.ok(fs.existsSync(APP), `app not built: ${APP} (run scripts/build-desktop.sh)`);
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
  apiPorts = { console: await freePort(), A: await freePort(), B: await freePort() };
  consoleHome = fs.mkdtempSync(path.join(tmpRoot, 'home-console-'));
  consoleApp = await startPeer({ name: 'console-1', role: 'console', port, apiPort: apiPorts.console, home: consoleHome });
  vdiA = await startPeer({ name: ids.A, role: 'vdi', port, apiPort: apiPorts.A });
  vdiB = await startPeer({ name: ids.B, role: 'vdi', port, apiPort: apiPorts.B });
});

after(async () => {
  consoleClient?.stop();
  for (const p of [...procs]) { try { p.kill('SIGKILL'); } catch {} }
  fs.rmSync(tmpRoot, { recursive: true, force: true });
});

const stationsOf = c => c.store.state?.stations ?? {};
const waitStation = (c, id, pred, ms, what) =>
  waitUntil(() => { const s = stationsOf(c)[id]; return s && pred(s) ? s : false; }, ms, `${id} ${what}`);

//== tests ======================================================================
test('console: initial state matches section 3.2 and both VDIs appear online with agents', async () => {
  consoleClient = connectClient(apiPorts.console, 'console');
  await waitUntil(() => consoleClient.status === 'ready', 10000, 'ready');
  // The API listens before the engine registers its state; a client that connects in
  // that gap gets the real state as a second `state` message (ApiServer::setStateProvider).
  await waitUntil(() => consoleClient.store.state?.mic, 10000, 'engine state');
  const hello = consoleClient.hello;
  assert.equal(hello.role, 'console');
  assert.ok(hello.features.includes('stations'));
  checkConsoleShape(consoleClient.store.state);

  for (const id of Object.values(ids))
    await waitStation(consoleClient, id, s => s.presence === 'online' && s.agent !== null, 25000, 'online with agent');

  const st = consoleClient.store.state;
  checkConsoleShape(st);
  assert.equal(st.self.name, 'console-1');
  assert.equal(st.connection.state, 'connected');
  assert.equal(st.connection.group, 'p42');
  assert.equal(st.connection.server, `127.0.0.1:${port}`);
  if (process.env.P42_DUMP) console.log(JSON.stringify(st, null, 1));
  const a = st.stations[ids.A], b = st.stations[ids.B];
  assert.notEqual(a.colorIndex, b.colorIndex, 'distinct colour indexes');
  assert.deepEqual(st.stationOrder, [a, b].sort((x, y) => x.colorIndex - y.colorIndex).map(s => s.id), 'stationOrder follows colorIndex');
  assert.equal(a.agent.configError, null);
  assert.equal(typeof a.latencyMs, 'number');
  assert.deepEqual(st.unknownPeers, []);
  assert.deepEqual(st.otherConsoles, []);
  assert.equal(st.mic.mode, 'open');
});

let colorOfA;

test('console: SIGSTOP turns a station lost, SIGKILL turns it offline; patches have consecutive revs', async () => {
  colorOfA = consoleClient.store.state.stations[ids.A].colorIndex;
  const mark = consoleClient.log.msgs.length;

  process.kill(vdiA.pid, 'SIGSTOP');                 // our own child
  const lost = await waitStation(consoleClient, ids.A, s => s.presence === 'lost', 15000, 'lost');
  assert.equal(typeof lost.lostForSec, 'number');
  assert.equal(lost.latencyMs, null);
  assert.equal(stationsOf(consoleClient)[ids.B].presence, 'online', 'the other VDI is unaffected');
  await waitStation(consoleClient, ids.A, s => s.lostForSec >= 1, 8000, 'lostForSec counting');

  process.kill(vdiA.pid, 'SIGKILL');
  await vdiA.exit;
  const off = await waitStation(consoleClient, ids.A, s => s.presence === 'offline', 30000, 'offline');
  assert.equal(off.agent, null);
  assert.equal(off.colorIndex, colorOfA);
  assert.ok(isIso(off.lastSeen));
  const order = consoleClient.store.state.stationOrder;
  assert.equal(order[order.length - 1], ids.A, 'offline stations come last');
  checkConsoleShape(consoleClient.store.state);

  const seen = consoleClient.log.msgs.slice(mark).filter(m => m.t === 'patch');
  assert.ok(seen.length > 0);
  const presencePatches = seen.flatMap(m => m.ops).filter(o => o.path === `/stations/${ids.A}/presence`).map(o => o.value);
  assert.ok(presencePatches.includes('lost') && presencePatches.includes('offline'), 'presence patches: ' + presencePatches);
});

test('console: the colorIndex is stable when the VDI rejoins', async () => {
  vdiA = await startPeer({ name: ids.A, role: 'vdi', port, apiPort: apiPorts.A });
  const s = await waitStation(consoleClient, ids.A, s => s.presence === 'online' && s.agent !== null, 25000, 'back online');
  assert.equal(s.colorIndex, colorOfA);
  assert.equal(consoleClient.store.state.stations[ids.B].colorIndex !== colorOfA, true);
});

test('console: the real client applied every patch without a resync, and a fresh snapshot agrees', async () => {
  assert.equal(consoleClient.log.sent.filter(m => m.t === 'resync').length, 0, 'client never needed a resync');
  assertConsecutive(consoleClient.log.msgs);
  assert.ok(consoleClient.log.msgs.filter(m => m.t === 'patch').length > 5, 'the rest of the session was patches');

  // A second client's snapshot equals the first client's patched state (volatile fields aside).
  const norm = st => {
    const c = structuredClone(st);
    for (const s of Object.values(c.stations)) { delete s.lastSeen; delete s.lostForSec; delete s.latencyMs; delete s.jitterBufferMs; delete s.lossPct; delete s.health; }
    return c;
  };
  let lastErr;
  for (let i = 0; i < 10; i++) {
    const second = connectClient(apiPorts.console, 'console');
    await waitUntil(() => second.status === 'ready', 10000, 'second client ready');
    await sleep(300);
    try {
      assert.equal(second.store.rev, consoleClient.store.rev, 'same rev');
      assert.deepEqual(norm(consoleClient.store.state), norm(second.store.state));
      second.stop();
      return;
    } catch (e) { lastErr = e; second.stop(); await sleep(300); }
  }
  throw lastErr;
});

test('agent role: the VDI API serves section 3.3 state with the console in consoles[]', async () => {
  const c = connectClient(apiPorts.B, 'vdi');
  try {
    await waitUntil(() => c.status === 'ready', 10000, 'agent ready');
    await waitUntil(() => c.store.state?.consoles, 10000, 'engine state');
    assert.equal(c.hello.role, 'vdi');
    assert.ok(c.hello.features.includes('consoles'));
    await waitUntil(() => c.store.state.consoles.length > 0, 20000, 'console listed');
    const st = c.store.state;
    checkAgentShape(st);
    assert.equal(st.self.name, ids.B);
    assert.equal(st.connection.state, 'connected');
    assert.equal(st.connection.group, 'p42');
    assert.equal(st.sending, 'active');
    const k = st.consoles.find(x => x.name === 'console-1');
    assert.ok(k, 'console-1 in consoles[]');
    assert.ok(['mac', 'web', 'other'].includes(k.kind));
    assert.equal(c.log.sent.filter(m => m.t === 'resync').length, 0);
    assertConsecutive(c.log.msgs);
  } finally { c.stop(); }
});

test('console: remembered stations survive a Console restart (offline, same colorIndex, last)', async () => {
  const before = structuredClone(consoleClient.store.state);
  consoleClient.stop();
  consoleClient = null;
  await stopProc(vdiA); await stopProc(vdiB);
  await stopProc(consoleApp);                        // SIGTERM: saves the plugin state

  consoleApp = await startPeer({ name: 'console-1', role: 'console', port, apiPort: apiPorts.console, home: consoleHome });
  const c = connectClient(apiPorts.console, 'console');
  try {
    await waitUntil(() => c.status === 'ready', 10000, 'ready after restart');
    await waitUntil(() => c.store.state?.mic, 10000, 'engine state');
    const st = c.store.state;
    checkConsoleShape(st);
    for (const id of Object.values(ids)) {
      assert.ok(st.stations[id], `${id} remembered`);
      assert.equal(st.stations[id].presence, 'offline');
      assert.equal(st.stations[id].colorIndex, before.stations[id].colorIndex, `${id} colorIndex persisted`);
      assert.equal(st.stations[id].agent, null);
    }
    assert.deepEqual(st.stationOrder, [...Object.values(ids)].sort((x, y) => before.stations[x].colorIndex - before.stations[y].colorIndex));
  } finally { c.stop(); }
});
