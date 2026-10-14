// P5.9 integration test: muting a station also stops sending your mic to it.
//   node --test tests/api/p59.test.mjs
//
// aooserver + 1 console (A, mic live) + 1 vdi (B, the station under test). B's role
// must differ from A's: the P1.4 routing matrix blocks two peers of the same role,
// so a console+console pair never connects.
//
// Asserts the engine state (hearsYou) AND the audio path (the peer dump's
// sendGateGain, written by --dump-peers), through: live -> muted -> muted+soloed ->
// un-muted -> plain. Mute must win over solo, and must leave the station's talk flag
// untouched so un-muting restores whatever talk was.
//
// Needs a working audio device: the peer's aoo streams (what makes it "online") come
// from the audio callback, so a box with no device cannot run this test. Override with
// P59_IN_DEVICE / P59_OUT_DEVICE.

import test, { after, before } from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const APP = process.env.SONOBUS_APP || path.join(ROOT, 'build/desktop-release/SonoBus_artefacts/Release/Standalone/sonobus');
const SERVER = process.env.AOOSERVER || path.join(ROOT, 'build/aooserver-release/aooserver');
const IN_DEVICE = process.env.P59_IN_DEVICE || 'Default Audio Device';
const OUT_DEVICE = process.env.P59_OUT_DEVICE || 'Default Audio Device';

const A = 'p59-console', B = 'p59-station';
const sleep = ms => new Promise(r => setTimeout(r, ms));
const near = (v, x, eps = 0.01) => typeof v === 'number' && Math.abs(v - x) < eps;

async function freePort() {
  return await new Promise((res, rej) => {
    const s = net.createServer();
    s.on('error', rej);
    s.listen(0, '127.0.0.1', () => { const p = s.address().port; s.close(() => res(p)); });
  });
}
async function portOpen(port) {
  return await new Promise(r => { const s = net.connect(port, '127.0.0.1', () => { s.destroy(); r(true); }); s.on('error', () => r(false)); });
}
function launch(cmd, args, { home, env } = {}) {
  const p = spawn(cmd, args, {
    env: { ...process.env, ...(home ? { HOME: home, XDG_CONFIG_HOME: path.join(home, '.config') } : {}), ...env },
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  p.output = '';
  p.stdout.on('data', d => { p.output += d; });
  p.stderr.on('data', d => { p.output += d; });
  p.exited = false;
  p.on('exit', () => { p.exited = true; });
  return p;
}
async function waitUntil(fn, ms, what) {
  const t0 = Date.now();
  for (;;) {
    const v = await fn();
    if (v) return v;
    if (Date.now() - t0 > ms) throw new Error(`timed out waiting for ${what}`);
    await sleep(250);
  }
}

function apiClient(port, label) {
  const ws = new WebSocket(`ws://127.0.0.1:${port}/api/v1/ws`);
  let seq = 0;
  const pending = new Map();
  let waiters = [];
  ws.addEventListener('message', ev => {
    let m; try { m = JSON.parse(ev.data); } catch { return; }
    if (m.t === 'state') waiters.splice(0).forEach(w => w(m.state));
    if (m.t === 'ack' && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
  });
  const ready = new Promise((res, rej) => {
    ws.addEventListener('open', res);
    ws.addEventListener('error', () => rej(new Error(`${label}: ws error`)));
  });
  return {
    ready, label,
    close: () => ws.close(),
    async cmd(name, args = {}, timeout = 8000) {
      const id = 'c' + (++seq);
      const p = new Promise((res, rej) => {
        pending.set(id, res);
        setTimeout(() => { pending.delete(id); rej(new Error(`${label}: ${name} timed out`)); }, timeout);
      });
      ws.send(JSON.stringify({ t: 'cmd', id, cmd: name, args }));
      const a = await p;
      if (!a.ok) throw new Error(`${label}: ${name} -> ${JSON.stringify(a.error)}`);
      return a;
    },
    async snap(timeout = 8000) {
      const p = new Promise((res, rej) => {
        waiters.push(res);
        setTimeout(() => rej(new Error(`${label}: resync timed out`)), timeout);
      });
      ws.send(JSON.stringify({ t: 'resync' }));
      return await p;
    },
    async station(name, ms = 30000) {
      return await waitUntil(async () => {
        const st = await this.snap();
        const s = st.stations?.[name];
        return s && s.presence === 'online' ? s : null;
      }, ms, `${label}: station ${name} online`);
    },
  };
}

// The dump is rewritten every second; a read can land mid-write, so retry.
function gate(dumpPath, name) {
  for (let i = 0; i < 5; ++i) {
    try {
      const d = JSON.parse(fs.readFileSync(dumpPath, 'utf8'));
      const list = d.peers || d;
      const e = list.find?.(p => p.name === name) ?? list[name];
      if (e) return e.sendGateGain;
    } catch { /* mid-write */ }
  }
  return null;
}

let tmpRoot, serverProc, appProc, client;
let dumpPath, port;

before(async () => {
  assert.ok(fs.existsSync(APP), `app not built: ${APP} (scripts/build-desktop.sh --app-only)`);
  assert.ok(fs.existsSync(SERVER), `aooserver not built: ${SERVER} (scripts/build-desktop.sh --server-only)`);
  tmpRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'p59-'));
  port = await freePort();
  serverProc = launch(SERVER, ['-p', String(port), '-l', path.join(tmpRoot, 'serverlog')]);
  await waitUntil(() => portOpen(port), 15000, 'aooserver to listen');

  const apiConsole = await freePort(), apiStation = await freePort();
  const yaml = (name, role) => path.join(tmpRoot, `${name}.yaml`);
  const write = (p, name, role) => fs.writeFileSync(p,
    `server: 127.0.0.1:${port}\ngroup: p59\nusername: ${name}\nrole: ${role}\n`
    + `audio:\n  input_device: ${IN_DEVICE}\n  output_device: ${OUT_DEVICE}\n`);
  const consoleYaml = yaml(A), stationYaml = yaml(B);
  write(consoleYaml, A, 'console');
  write(stationYaml, B, 'vdi');

  dumpPath = path.join(tmpRoot, 'peers.json');
  appProc = [
    launch(APP, ['--headless', '--config', consoleYaml, '--api-port', String(apiConsole), '--dump-peers', dumpPath],
      { home: path.join(tmpRoot, 'console-home') }),
    launch(APP, ['--headless', '--config', stationYaml, '--api-port', String(apiStation)],
      { home: path.join(tmpRoot, 'station-home') }),
  ];
  // Wait for the control API to listen before connecting: the WS is the readiness
  // signal, and an app that died (bad config, no audio device) must say so here
  // rather than as a bare 'ws error'.
  try {
    await waitUntil(() => portOpen(apiConsole), 30000, 'the console control API to listen');
    await waitUntil(() => portOpen(apiStation), 30000, 'the station control API to listen');
  } catch (e) {
    throw new Error(`${e.message}\nconsole app exited=${appProc[0].exited}\n${appProc[0].output.slice(-2000)}`);
  }
  client = apiClient(apiConsole, 'console');
  await client.ready;
  await client.station(B, 45000);
  await client.cmd('mic.setOn', { on: true });
  await client.cmd('station.setLevel', { station: B, db: -40 });   // keep the run quiet
  await waitUntil(() => near(gate(dumpPath, B), 1), 20000, 'the send gate to open');
});

after(async () => {
  client?.close();
  for (const p of [...(appProc || []), serverProc].filter(Boolean)) {
    if (!p) continue;
    p.kill('SIGTERM');
    await sleep(300);
    if (!p.exited) p.kill('SIGKILL');
  }
  await sleep(500);
  if (tmpRoot) fs.rmSync(tmpRoot, { recursive: true, force: true });
});

test('P5.9: muting a station closes your mic to it, and leaves its talk flag alone', async () => {
  const before = await client.station(B);
  assert.equal(before.hearsYou, true, 'the station hears you while the mic is live');
  assert.equal(before.talk, true);
  assert.ok(near(gate(dumpPath, B), 1), 'the send gate is open');

  await client.cmd('station.setMute', { station: B, on: true });
  await waitUntil(() => gate(dumpPath, B) < 0.01, 10000, 'the send gate to close');
  const muted = await client.station(B);
  assert.equal(muted.hearsYou, false, 'a muted station does not hear you');
  assert.equal(muted.talk, true, 'mute leaves the talk choice untouched');
  assert.equal(muted.mute, true);
});

test('P5.9: mute wins over solo', async () => {
  await client.cmd('station.setMute', { station: B, on: true });
  await client.cmd('station.setSolo', { station: B, on: true });
  await sleep(1500);
  const s = await client.station(B);
  assert.equal(s.solo, true);
  assert.equal(s.hearsYou, false, 'a muted, soloed station still does not hear you');
  assert.ok(near(gate(dumpPath, B), 0), 'the gate stays closed');
});

test('P5.9: un-muting restores what solo alone gives', async () => {
  await client.cmd('station.setSolo', { station: B, on: true });
  await client.cmd('station.setMute', { station: B, on: false });
  await waitUntil(() => gate(dumpPath, B) > 0.9, 15000, 'the send gate to reopen');
  const s = await client.station(B);
  assert.equal(s.hearsYou, true, 'soloed and un-muted: hears you again');
  assert.equal(s.talk, true);

  await client.cmd('station.setSolo', { station: B, on: false });
  await sleep(1500);
  assert.equal((await client.station(B)).hearsYou, true, 'and plain again');
});
