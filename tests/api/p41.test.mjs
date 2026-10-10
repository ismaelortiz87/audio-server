// P4.1 integration test: embedded HTTP + WebSocket control server.
//   node --test tests/api/p41.test.mjs
// Launches the real headless app (no npm deps; Node >= 22 for the global WebSocket).
// Each app gets an isolated settings home via CFFIXED_USER_HOME (macOS ignores $HOME).
// Override the binary with CROSSPOINT_APP=/path/to/Crosspoint.

import test, { after, before } from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const APP = process.env.CROSSPOINT_APP ||
  path.join(ROOT, 'build/desktop-release/SonoBus_artefacts/Release/Standalone/Crosspoint.app/Contents/MacOS/Crosspoint');
const UI_DIR = path.join(ROOT, 'console-ui');
const GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';

const tmpRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'p41-'));
const procs = new Set();

//== helpers ====================================================================
const sleep = ms => new Promise(r => setTimeout(r, ms));

async function freePort() {
  return new Promise((resolve, reject) => {
    const s = net.createServer();
    s.listen(0, '127.0.0.1', () => { const { port } = s.address(); s.close(() => resolve(port)); });
    s.on('error', reject);
  });
}

/** Starts the app; resolves once it printed "Control API listening" (or exits). */
function startApp(args, { waitReady = true } = {}) {
  const home = fs.mkdtempSync(path.join(tmpRoot, 'home-'));
  const proc = spawn(APP, ['-q', ...args], {
    env: { ...process.env, CFFIXED_USER_HOME: home },
    cwd: home,
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  procs.add(proc);
  const app = { proc, output: '', exited: null };
  app.exit = new Promise(resolve => proc.on('exit', (code, signal) => { app.exited = { code, signal }; procs.delete(proc); resolve(app.exited); }));
  const collect = d => { app.output += d.toString(); };
  proc.stdout.on('data', collect);
  proc.stderr.on('data', collect);
  app.ready = new Promise((resolve, reject) => {
    const t = setInterval(() => {
      if (app.output.includes('Control API listening')) { clearInterval(t); resolve(); }
      else if (app.exited) { clearInterval(t); reject(new Error('app exited early: ' + app.output)); }
    }, 25);
    setTimeout(() => { clearInterval(t); reject(new Error('timeout waiting for API: ' + app.output)); }, 20000).unref();
  });
  app.stop = async () => {
    if (!app.exited) proc.kill('SIGTERM');
    const r = await Promise.race([app.exit, sleep(8000).then(() => null)]);
    if (!r) { proc.kill('SIGKILL'); throw new Error('app did not exit within 8 s of SIGTERM'); }
    return r;
  };
  if (!waitReady) app.ready.catch(() => {});   // expected to fail for apps that must refuse to start
  return waitReady ? app.ready.then(() => app) : Promise.resolve(app);
}

/** One HTTP request over a raw socket (so paths are sent byte for byte). */
function rawHttp(port, requestText) {
  return new Promise((resolve, reject) => {
    const s = net.connect(port, '127.0.0.1');
    const chunks = [];
    s.on('data', d => chunks.push(d));
    s.on('end', () => resolve(parseHttp(Buffer.concat(chunks))));
    s.on('error', reject);
    s.write(requestText);
    setTimeout(() => { s.destroy(); reject(new Error('rawHttp timeout')); }, 5000).unref();
  });
}
function parseHttp(buf) {
  const i = buf.indexOf('\r\n\r\n');
  const head = buf.subarray(0, i).toString();
  const lines = head.split('\r\n');
  const headers = {};
  for (const l of lines.slice(1)) { const k = l.indexOf(':'); headers[l.slice(0, k).toLowerCase()] = l.slice(k + 1).trim(); }
  return { status: Number(lines[0].split(' ')[1]), headers, body: buf.subarray(i + 4) };
}
const get = (port, p, extra = '') => rawHttp(port, `GET ${p} HTTP/1.1\r\nHost: 127.0.0.1:${port}\r\nConnection: close\r\n${extra}\r\n`);

/** Minimal RFC 6455 client over a raw socket: lets the test set Origin, mask, fragment, ping. */
class RawWs {
  static async open(port, { origin, host, path: p = '/api/v1/ws', key } = {}) {
    const ws = new RawWs();
    ws.key = key ?? crypto.randomBytes(16).toString('base64');
    ws.sock = net.connect(port, '127.0.0.1');
    ws.buf = Buffer.alloc(0);
    ws.frames = [];
    ws.waiters = [];
    ws.sock.on('data', d => { ws.buf = Buffer.concat([ws.buf, d]); ws.pump(); });
    ws.sock.on('close', () => { ws.closed = true; ws.pump(); });
    ws.sock.on('error', () => {});
    let req = `GET ${p} HTTP/1.1\r\nHost: ${host ?? '127.0.0.1:' + port}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n` +
              `Sec-WebSocket-Key: ${ws.key}\r\nSec-WebSocket-Version: 13\r\n`;
    if (origin) req += `Origin: ${origin}\r\n`;
    ws.sock.write(req + '\r\n');
    ws.response = await new Promise((resolve, reject) => {
      ws.onHead = resolve;
      setTimeout(() => reject(new Error('no handshake response')), 5000).unref();
      ws.pump();
    });
    return ws;
  }
  pump() {
    if (!this.head) {
      const i = this.buf.indexOf('\r\n\r\n');
      if (i < 0) { if (this.closed && this.onHead) this.onHead(null); return; }
      this.head = parseHttp(this.buf.subarray(0, i + 4));
      this.buf = this.buf.subarray(i + 4);
      this.onHead?.(this.head);
    }
    if (this.head.status !== 101) return;
    for (;;) {
      if (this.buf.length < 2) break;
      const op = this.buf[0] & 0x0f;
      let len = this.buf[1] & 0x7f, off = 2;
      if (len === 126) { if (this.buf.length < 4) break; len = this.buf.readUInt16BE(2); off = 4; }
      else if (len === 127) { if (this.buf.length < 10) break; len = Number(this.buf.readBigUInt64BE(2)); off = 10; }
      if (this.buf.length < off + len) break;
      const payload = this.buf.subarray(off, off + len);
      this.buf = this.buf.subarray(off + len);
      const f = { op, payload };
      if (op === 1) f.json = JSON.parse(payload.toString());
      if (op === 8) f.code = payload.length >= 2 ? payload.readUInt16BE(0) : 1005;
      this.frames.push(f);
    }
    const w = this.waiters; this.waiters = [];
    for (const fn of w) fn();
  }
  /** Next frame matching pred (default: any), or rejects after `ms`. */
  async next(pred = () => true, ms = 4000) {
    const deadline = Date.now() + ms;
    for (;;) {
      const i = this.frames.findIndex(pred);
      if (i >= 0) return this.frames.splice(i, 1)[0];
      if (this.closed && !this.waiters.length && Date.now() > deadline) throw new Error('socket closed, no frame');
      const left = deadline - Date.now();
      if (left <= 0) throw new Error('timeout waiting for frame');
      await new Promise(r => { this.waiters.push(r); setTimeout(r, Math.min(left, 100)); });
    }
  }
  frame(op, payload, { fin = true, mask = true } = {}) {
    const p = Buffer.isBuffer(payload) ? payload : Buffer.from(payload);
    const head = [ (fin ? 0x80 : 0) | op ];
    const m = mask ? 0x80 : 0;
    if (p.length < 126) head.push(m | p.length);
    else if (p.length <= 0xffff) head.push(m | 126, p.length >> 8, p.length & 255);
    else { head.push(m | 127); const b = Buffer.alloc(8); b.writeBigUInt64BE(BigInt(p.length)); head.push(...b); }
    const key = crypto.randomBytes(4);
    const body = mask ? Buffer.from(p.map((x, i) => x ^ key[i & 3])) : p;
    return Buffer.concat([Buffer.from(head), mask ? key : Buffer.alloc(0), body]);
  }
  sendText(s, opts) { this.sock.write(this.frame(1, s, opts)); }
  sendJson(o) { this.sendText(JSON.stringify(o)); }
  sendRaw(op, payload, opts) { this.sock.write(this.frame(op, payload, opts)); }
  destroy() { this.sock.destroy(); }
  async waitClosed(ms = 4000) {
    const f = await this.next(x => x.op === 8, ms);
    return f.code;
  }
}

const exp = key => crypto.createHash('sha1').update(key + GUID).digest('base64');

//== fixtures ===================================================================
let consoleApp, vdiApp, tokenApp, lanApp;
let pConsole, pVdi, pToken, pLan;

before(async () => {
  assert.ok(fs.existsSync(APP), `app binary not found: ${APP} (run scripts/build-desktop.sh)`);
  [pConsole, pVdi, pToken, pLan] = [await freePort(), await freePort(), await freePort(), await freePort()];
  [consoleApp, vdiApp, tokenApp, lanApp] = await Promise.all([
    startApp(['--api-port', String(pConsole), '--ui-dir', UI_DIR, '-n', 'test-console']),
    startApp(['--role', 'vdi', '--api-port', String(pVdi), '--ui-dir', UI_DIR, '-n', 'test-vdi']),
    startApp(['--api-port', String(pToken), '--api-token', 's3cret']),
    startApp(['--api-port', String(pLan), '--api-bind', '0.0.0.0', '--api-token', 'lan-secret',
              '--api-allow-origin', 'https://crosspoint.example.com']),
  ]);
});

after(async () => {
  for (const p of procs) p.kill('SIGKILL');          // only processes this test started
  fs.rmSync(tmpRoot, { recursive: true, force: true });
});

//== tests ======================================================================
test('health: 200 JSON without auth, both roles', async () => {
  const r = await fetch(`http://127.0.0.1:${pConsole}/api/v1/health`);
  assert.equal(r.status, 200);
  assert.match(r.headers.get('content-type'), /^application\/json/);
  const j = await r.json();
  assert.deepEqual(Object.keys(j), ['ok', 'app', 'version', 'role']);
  assert.equal(j.ok, true);
  assert.equal(j.app, 'Crosspoint');
  assert.match(j.version, /^\d+\.\d+\.\d+/);
  assert.equal(j.role, 'console');
  assert.equal((await (await fetch(`http://127.0.0.1:${pVdi}/api/v1/health`)).json()).role, 'vdi');
  // also with the token app (health needs no token)
  assert.equal((await fetch(`http://127.0.0.1:${pToken}/api/v1/health`)).status, 200);
});

test('SHA-1 / accept key: RFC 6455 sample key gets the documented accept value', async () => {
  const ws = await RawWs.open(pConsole, { key: 'dGhlIHNhbXBsZSBub25jZQ==' });
  assert.equal(ws.response.status, 101);
  assert.equal(ws.response.headers['sec-websocket-accept'], 's3pPLMBiTxaQ9kYGzzhZRbK+xOo=');
  assert.equal(ws.response.headers.upgrade.toLowerCase(), 'websocket');
  ws.destroy();
});

test('hello then placeholder state (loopback, no token)', async () => {
  const ws = await RawWs.open(pConsole);
  const hello = (await ws.next()).json;
  assert.equal(hello.t, 'hello');
  assert.equal(hello.v, 1);
  assert.equal(hello.app, 'Crosspoint');
  assert.equal(hello.role, 'console');
  assert.equal(hello.auth, 'none');
  assert.match(hello.version, /^\d+\.\d+\.\d+/);
  assert.ok(Array.isArray(hello.features));
  const st = (await ws.next()).json;
  assert.equal(st.t, 'state');
  assert.equal(typeof st.rev, 'number');
  assert.deepEqual({ name: st.state.self.name, role: st.state.self.role }, { name: 'test-console', role: 'console' });   // P4.2: the engine's full state may follow
  // resync returns a fresh state with a rev that did not go backwards
  ws.sendJson({ t: 'resync' });
  const again = (await ws.next(f => f.json?.t === 'state')).json;
  assert.ok(again.rev >= st.rev);
  ws.destroy();
});

test('VDI role: hello and state say vdi', async () => {
  const ws = await RawWs.open(pVdi);
  assert.equal((await ws.next()).json.role, 'vdi');
  { const self = (await ws.next()).json.state.self; assert.deepEqual({ name: self.name, role: self.role }, { name: 'test-vdi', role: 'vdi' }); }   // P4.2
  ws.destroy();
});

test('global WebSocket (no Origin header) works too', async () => {
  const ws = new WebSocket(`ws://127.0.0.1:${pConsole}/api/v1/ws`);
  const msgs = [];
  ws.onmessage = e => msgs.push(JSON.parse(e.data));
  await new Promise((res, rej) => { ws.onopen = res; ws.onerror = () => rej(new Error('ws error')); });
  while (msgs.length < 2) await sleep(20);
  assert.deepEqual(msgs.map(m => m.t), ['hello', 'state']);
  ws.close(1000);
});

test('unknown message types are ignored; sub/unsub/visibility accepted', async () => {
  const ws = await RawWs.open(pConsole);
  await ws.next(); await ws.next();
  for (const m of [{ t: 'nope' }, { t: 'sub', topics: ['meters'] }, { t: 'visibility', hidden: true }, { t: 'unsub', topics: ['meters'] }])
    ws.sendJson(m);
  ws.sendJson({ t: 'cmd', id: 'z1', cmd: 'x' });
  const ack = (await ws.next()).json;           // first thing back must be the ack (nothing else was answered)
  assert.equal(ack.id, 'z1');
  ws.destroy();
});

test('cmd -> not_supported ack (until P4.4)', async () => {
  const ws = await RawWs.open(pConsole);
  await ws.next(); await ws.next();
  ws.sendJson({ t: 'cmd', id: 'c17', cmd: 'station.setLevel', args: { station: 'A', db: -6 } });
  const ack = (await ws.next(f => f.json?.t === 'ack')).json;
  assert.deepEqual(ack, { t: 'ack', id: 'c17', ok: false, error: { code: 'not_supported', message: ack.error.message } });
  assert.ok(ack.error.message.length > 0);
  ws.destroy();
});

test('malformed input closes with 4400', async () => {
  for (const bad of ['{not json', '[1,2]', '"str"', '{"no":"t"}', '{"t":5}', '[' .repeat(5000), '{"t":"cmd","cmd":"x"}']) {
    const ws = await RawWs.open(pConsole);
    await ws.next(); await ws.next();
    ws.sendText(bad);
    assert.equal(await ws.waitClosed(), 4400, `input: ${bad.slice(0, 20)}`);
    ws.destroy();
  }
});

test('WebSocket framing: ping/pong, fragmented message, unmasked frame, binary', async () => {
  let ws = await RawWs.open(pConsole);
  await ws.next(); await ws.next();
  ws.sendRaw(9, 'hi');
  assert.equal((await ws.next(f => f.op === 10)).payload.toString(), 'hi');
  // fragmented text message split over 3 frames, with a ping in between
  const msg = JSON.stringify({ t: 'cmd', id: 'frag', cmd: 'x' });
  ws.sendRaw(1, msg.slice(0, 5), { fin: false });
  ws.sendRaw(9, '');
  ws.sendRaw(0, msg.slice(5, 12), { fin: false });
  ws.sendRaw(0, msg.slice(12), { fin: true });
  assert.equal((await ws.next(f => f.json?.t === 'ack')).json.id, 'frag');
  // large message (extended 16-bit length)
  ws.sendJson({ t: 'cmd', id: 'big', cmd: 'x', args: { pad: 'p'.repeat(5000) } });
  assert.equal((await ws.next(f => f.json?.t === 'ack')).json.id, 'big');
  // client close is echoed
  ws.sendRaw(8, Buffer.from([0x03, 0xe8]));
  assert.equal(await ws.waitClosed(), 1000);
  ws.destroy();

  ws = await RawWs.open(pConsole);
  await ws.next(); await ws.next();
  ws.sendRaw(1, '{"t":"x"}', { mask: false });
  assert.equal(await ws.waitClosed(), 1002);
  ws.destroy();

  ws = await RawWs.open(pConsole);
  await ws.next(); await ws.next();
  ws.sendRaw(2, Buffer.from([1, 2, 3]));
  assert.equal(await ws.waitClosed(), 1003);
  ws.destroy();
});

test('Origin check: foreign origin 403; localhost, own and listed origins accepted', async () => {
  let ws = await RawWs.open(pConsole, { origin: 'https://evil.example.com' });
  assert.equal(ws.response.status, 403);
  ws.destroy();
  ws = await RawWs.open(pConsole, { origin: 'null' });
  assert.equal(ws.response.status, 403);
  ws.destroy();
  ws = await RawWs.open(pConsole, { origin: `http://localhost:${pConsole + 1}` });   // wrong port
  assert.equal(ws.response.status, 403);
  ws.destroy();
  for (const o of [`http://localhost:${pConsole}`, `http://127.0.0.1:${pConsole}`]) {
    ws = await RawWs.open(pConsole, { origin: o });
    assert.equal(ws.response.status, 101, o);
    ws.destroy();
  }
  // DNS rebinding: attacker name in both Host and Origin must not pass on a loopback bind
  ws = await RawWs.open(pConsole, { origin: `http://evil.example.com:${pConsole}`, host: `evil.example.com:${pConsole}` });
  assert.equal(ws.response.status, 403);
  ws.destroy();
  assert.equal((await get(pConsole, '/api/v1/health', 'X: 1\r\n').then(() => rawHttp(pConsole, 'GET /api/v1/health HTTP/1.1\r\nHost: evil.example.com\r\n\r\n'))).status, 403);
});

test('upgrade validation: plain GET of the ws path is 426', async () => {
  const r = await get(pConsole, '/api/v1/ws');
  assert.equal(r.status, 426);
});

test('token auth: hello says token, no state before auth, bad token -> 4401', async () => {
  let ws = await RawWs.open(pToken);
  const hello = (await ws.next()).json;
  assert.equal(hello.auth, 'token');
  await sleep(150);
  assert.equal(ws.frames.length, 0, 'no state may be sent before auth');
  ws.sendJson({ t: 'auth', token: 'wrong' });
  assert.deepEqual((await ws.next()).json, { t: 'auth', ok: false });
  assert.equal(await ws.waitClosed(), 4401);
  ws.destroy();

  ws = await RawWs.open(pToken);
  await ws.next();
  ws.sendJson({ t: 'cmd', id: 'x', cmd: 'y' });          // anything but auth first
  assert.equal(await ws.waitClosed(), 4401);
  ws.destroy();

  ws = await RawWs.open(pToken);
  await ws.next();
  ws.sendJson({ t: 'auth', token: 's3cret' });
  assert.deepEqual((await ws.next()).json, { t: 'auth', ok: true });
  assert.equal((await ws.next()).json.t, 'state');
  ws.destroy();
});

test('non-loopback bind requires a token (refuses to start otherwise)', async () => {
  const port = await freePort();
  const app = await startApp(['--api-port', String(port), '--api-bind', '0.0.0.0'], { waitReady: false });
  const r = await Promise.race([app.exit, sleep(10000).then(() => null)]);
  assert.ok(r, 'app should exit by itself');
  assert.notEqual(r.code, 0);
  assert.match(app.output, /refusing to start without --api-token/);
  await assert.rejects(fetch(`http://127.0.0.1:${port}/api/v1/health`));
  // an empty-looking token via the environment variable does not count either
});

test('non-loopback bind with token: hello auth=token, Origin allow-list enforced', async () => {
  let ws = await RawWs.open(pLan);
  assert.equal((await ws.next()).json.auth, 'token');
  ws.destroy();
  ws = await RawWs.open(pLan, { origin: 'https://crosspoint.example.com' });
  assert.equal(ws.response.status, 101);
  ws.destroy();
  ws = await RawWs.open(pLan, { origin: 'https://other.example.com' });
  assert.equal(ws.response.status, 403);
  ws.destroy();
  // own Host is accepted on a non-loopback bind (reverse proxy / LAN name)
  ws = await RawWs.open(pLan, { origin: 'http://crosspoint.lan:8080', host: 'crosspoint.lan:8080' });
  assert.equal(ws.response.status, 101);
  ws.destroy();
});

test('an explicit port that is already in use is a startup error', async () => {
  const blocker = net.createServer();
  await new Promise(r => blocker.listen(0, '127.0.0.1', r));
  const port = blocker.address().port;
  const app = await startApp(['--api-port', String(port)], { waitReady: false });
  const r = await Promise.race([app.exit, sleep(10000).then(() => null)]);
  blocker.close();
  assert.ok(r, 'app should exit by itself');
  assert.notEqual(r.code, 0);
  assert.match(app.output, /cannot listen/);
});

test('--api-port 0 disables the server', async () => {
  const app = await startApp(['--api-port', '0', '-g', 'g', '-c', '127.0.0.1:1'], { waitReady: false });
  await sleep(1500);
  assert.ok(!app.output.includes('Control API listening'));
  const r = await app.stop();
  assert.ok(r);
});

test('static files: types, index per role, traversal refused', async () => {
  const idx = await get(pConsole, '/');
  assert.equal(idx.status, 200);
  assert.match(idx.headers['content-type'], /^text\/html/);
  assert.equal(idx.body.toString(), fs.readFileSync(path.join(UI_DIR, 'index.html'), 'utf8'));
  const agent = await get(pVdi, '/');
  assert.equal(agent.body.toString(), fs.readFileSync(path.join(UI_DIR, 'agent.html'), 'utf8'));

  const want = { '.js': /^text\/javascript/, '.css': /^text\/css/, '.json': /^application\/json/,
                 '.webmanifest': /^application\/manifest\+json/, '.png': /^image\/png$/ };
  const seen = new Set();
  const walk = d => fs.readdirSync(d, { withFileTypes: true }).flatMap(e =>
    e.isDirectory() ? (e.name === 'node_modules' ? [] : walk(path.join(d, e.name))) : [path.join(d, e.name)]);
  for (const f of walk(UI_DIR)) {
    const ext = path.extname(f);
    if (!want[ext] || seen.has(ext)) continue;
    const rel = '/' + path.relative(UI_DIR, f).split(path.sep).map(encodeURIComponent).join('/');
    const r = await get(pConsole, rel);
    assert.equal(r.status, 200, rel);
    assert.match(r.headers['content-type'], want[ext], rel);
    assert.ok(Buffer.compare(r.body, fs.readFileSync(f)) === 0, `${rel} content matches`);
    seen.add(ext);
  }
  assert.deepEqual([...seen].sort(), Object.keys(want).sort(), 'every required content type had a sample file');

  // console-ui ships no .svg yet, so serve one from a fixture folder
  const fx = fs.mkdtempSync(path.join(tmpRoot, 'ui-'));
  fs.writeFileSync(path.join(fx, 'logo.svg'), '<svg xmlns="http://www.w3.org/2000/svg"/>');
  const fxPort = await freePort();
  const fxApp = await startApp(['--api-port', String(fxPort), '--ui-dir', fx]);
  const svg = await get(fxPort, '/logo.svg');
  assert.equal(svg.status, 200);
  assert.equal(svg.headers['content-type'], 'image/svg+xml');
  assert.equal((await fxApp.stop()).code, 0);

  // HEAD: headers only
  const head = await rawHttp(pConsole, `HEAD /index.html HTTP/1.1\r\nHost: 127.0.0.1:${pConsole}\r\n\r\n`);
  assert.equal(head.status, 200);
  assert.equal(head.body.length, 0);

  const readme = fs.readFileSync(path.join(ROOT, 'README.md'));
  for (const p of ['/../README.md', '/%2e%2e/README.md', '/..%2fREADME.md', '/src/../../README.md', '/%2E%2E/%2E%2E/etc/passwd',
                   '/..\\README.md', '/a/%00/b', '/.git/config', '//etc/passwd']) {
    const r = await get(pConsole, p);
    assert.ok([400, 403, 404].includes(r.status), `${p} -> ${r.status}`);
    assert.ok(!r.body.includes(readme.subarray(0, 40)), `${p} leaked the parent README`);
  }
  assert.equal((await get(pConsole, '/does-not-exist.js')).status, 404);
  assert.equal((await get(pConsole, '/api/v1/nope')).status, 404);
  assert.equal((await rawHttp(pConsole, `POST /index.html HTTP/1.1\r\nHost: 127.0.0.1:${pConsole}\r\n\r\n`)).status, 405);
  assert.equal((await rawHttp(pConsole, 'garbage\r\n\r\n')).status, 400);
});

test('real console-ui client.js completes the handshake against the engine', async () => {
  const { Client, ApiError } = await import(pathToFileURL(path.join(UI_DIR, 'src/api/client.js')).href);
  const { webSocketTransport } = await import(pathToFileURL(path.join(UI_DIR, 'src/api/client.js')).href);
  const open = (port, opts) => new Promise((resolve, reject) => {
    const statuses = [];
    const client = new Client(webSocketTransport(`ws://127.0.0.1:${port}/api/v1/ws`),
      { ...opts, onStatus: (s, d) => { statuses.push(s); if (s === 'ready' || s === 'fatal' || s === 'wrong-role') resolve({ client, statuses, s, d }); } });
    client.start();
    setTimeout(() => reject(new Error('client never became ready: ' + statuses)), 5000).unref();
  });

  let r = await open(pConsole, { expectRole: 'console' });
  assert.equal(r.s, 'ready');
  assert.equal(r.client.hello.role, 'console');
  assert.deepEqual({ name: r.client.store.state.self.name, role: r.client.store.state.self.role }, { name: 'test-console', role: 'console' });   // P4.2
  await assert.rejects(r.client.cmd('station.setMute', { station: 'A', on: true }),
    e => e instanceof ApiError && e.code === 'not_supported');
  r.client.stop();

  r = await open(pConsole, { expectRole: 'vdi' });
  assert.equal(r.s, 'wrong-role');
  r.client.stop();

  r = await open(pToken, { expectRole: 'console', token: 's3cret' });
  assert.equal(r.s, 'ready');
  r.client.stop();

  r = await open(pToken, { expectRole: 'console', token: 'bad' });
  assert.equal(r.s, 'fatal');
  assert.equal(r.d.code, 4401);
  r.client.stop();
});

test('clean shutdown: SIGTERM with live sessions closes them with 1001 and exits promptly', async () => {
  const port = await freePort();
  const app = await startApp(['--api-port', String(port), '--ui-dir', UI_DIR]);
  const a = await RawWs.open(port); await a.next(); await a.next();
  const b = await RawWs.open(port); await b.next(); await b.next();
  // a half-open HTTP request must not block shutdown either
  const half = net.connect(port, '127.0.0.1'); half.on('error', () => {}); half.write('GET /ind');
  await sleep(100);
  const t0 = Date.now();
  const r = await app.stop();
  const took = Date.now() - t0;
  assert.equal(r.code, 0, `exit status (signal=${r.signal}): ${app.output}`);
  assert.ok(took < 6000, `exit took ${took} ms`);
  assert.equal(await a.waitClosed(), 1001);
  assert.equal(await b.waitClosed(), 1001);
  half.destroy(); a.destroy(); b.destroy();
  await assert.rejects(fetch(`http://127.0.0.1:${port}/api/v1/health`));
});

test('remaining apps also exit cleanly on SIGTERM', async () => {
  for (const app of [consoleApp, vdiApp, tokenApp, lanApp]) {
    const r = await app.stop();
    assert.equal(r.code, 0, `exit status (signal=${r.signal})`);
  }
});
