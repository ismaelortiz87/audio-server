// P7.2: browser sign-in. Token -> HttpOnly session cookie; a WebSocket upgrade
// carrying a valid cookie is authenticated; token rotation invalidates cookies;
// the real console-ui shows the sign-in card and gets in.
//   node --test tests/api/p72.test.mjs    (after scripts/build-desktop.sh)
import test from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { mkdtempSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createServer } from 'node:net';

const repo = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const APP = process.env.APP ?? `${repo}/build/desktop-release/SonoBus_artefacts/Release/Standalone/Crosspoint.app/Contents/MacOS/Crosspoint`;
const CHROME = process.env.CHROME ?? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const sleep = ms => new Promise(r => setTimeout(r, ms));
const freePort = () => new Promise(r => { const s = createServer().listen(0, '127.0.0.1', () => { const p = s.address().port; s.close(() => r(p)); }); });

async function engine(token) {
  const home = mkdtempSync(join(tmpdir(), 'xp-p72-'));
  const port = await freePort();
  const proc = spawn(APP, ['--headless', '--role', 'console', '--api-port', String(port), '--api-token', token, '--ui-dir', `${repo}/console-ui`],
    { cwd: home, env: { ...process.env, CFFIXED_USER_HOME: home }, stdio: 'ignore' });
  for (let i = 0; i < 120; i++) { try { if ((await fetch(`http://127.0.0.1:${port}/api/v1/health`)).ok) break; } catch {} await sleep(250); }
  return { port, base: `http://127.0.0.1:${port}`, stop: () => new Promise(r => { proc.once('exit', r); proc.kill('SIGTERM'); }) };
}

function hello(base, headers = {}) {
  return new Promise((res, rej) => {
    const ws = new WebSocket(base.replace('http', 'ws') + '/api/v1/ws', { headers });
    const msgs = [];
    ws.onmessage = ev => { msgs.push(JSON.parse(ev.data)); if (msgs.length === 2 || (msgs[0]?.auth === 'token')) { ws.close(); res(msgs); } };
    ws.onerror = e => rej(e);
    setTimeout(() => { ws.close(); res(msgs); }, 4000);
  });
}

const cookieOf = res => (res.headers.get('set-cookie') ?? '').split(';')[0];

test('session endpoint, cookie flags, WS auth by cookie, sign-out, rotation', async () => {
  let e = await engine('s3cret-one');
  try {
    let r = await fetch(`${e.base}/api/v1/session`);
    assert.deepEqual(await r.json(), { required: true, authenticated: false });

    r = await fetch(`${e.base}/api/v1/session`, { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ token: 'nope' }) });
    assert.equal(r.status, 401);

    r = await fetch(`${e.base}/api/v1/session`, { method: 'POST', headers: { 'content-type': 'application/json', origin: 'https://evil.example' }, body: JSON.stringify({ token: 's3cret-one' }) });
    assert.equal(r.status, 403, 'a foreign Origin cannot sign in (CSRF)');

    r = await fetch(`${e.base}/api/v1/session`, { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify({ token: 's3cret-one' }) });
    assert.equal(r.status, 200);
    const sc = r.headers.get('set-cookie');
    assert.match(sc, /^crosspoint_session=[0-9a-f]{40};/);
    assert.match(sc, /HttpOnly/); assert.match(sc, /SameSite=Strict/); assert.match(sc, /Path=\//);
    assert.doesNotMatch(sc, /s3cret-one/, 'the token itself is never in the cookie');
    const cookie = cookieOf(r);

    r = await fetch(`${e.base}/api/v1/session`, { headers: { cookie } });
    assert.deepEqual(await r.json(), { required: true, authenticated: true });

    let m = await hello(e.base, { cookie });
    assert.equal(m[0].auth, 'none', 'a signed-in browser needs no auth message');
    assert.equal(m[1]?.t, 'state', 'and gets state straight away');

    m = await hello(e.base);
    assert.equal(m[0].auth, 'token', 'without the cookie the token is still required');

    r = await fetch(`${e.base}/api/v1/session`, { method: 'DELETE', headers: { cookie } });
    assert.equal(r.status, 204);
    assert.match(r.headers.get('set-cookie'), /Max-Age=0/);

    await e.stop();
    e = await engine('s3cret-two');   // rotate the token
    m = await hello(e.base, { cookie });
    assert.equal(m[0].auth, 'token', 'rotating the token signs old cookies out');
  } finally { await e.stop(); }
});

test('the real console-ui shows the sign-in card, rejects a bad token and gets in', async () => {
  const e = await engine('ui-token-42');
  const cdpPort = await freePort();
  const chrome = spawn(CHROME, ['--headless=new', '--disable-gpu', `--remote-debugging-port=${cdpPort}`, `--user-data-dir=${mkdtempSync(join(tmpdir(), 'xp-p72c-'))}`, 'about:blank'], { stdio: 'ignore' });
  try {
    let wsUrl;
    for (let i = 0; i < 50 && !wsUrl; i++) { try { wsUrl = (await (await fetch(`http://127.0.0.1:${cdpPort}/json`)).json()).find(t => t.type === 'page')?.webSocketDebuggerUrl; } catch {} await sleep(100); }
    const ws = new WebSocket(wsUrl); await new Promise(r => ws.addEventListener('open', r, { once: true }));
    let seq = 0; const waiting = new Map();
    ws.addEventListener('message', ev => { const m = JSON.parse(ev.data); if (m.id && waiting.has(m.id)) { waiting.get(m.id)(m); waiting.delete(m.id); } });
    const send = (method, params = {}) => new Promise(r => { const id = ++seq; waiting.set(id, r); ws.send(JSON.stringify({ id, method, params })); });
    const js = async x => (await send('Runtime.evaluate', { expression: x, awaitPromise: true, returnByValue: true })).result?.result?.value;
    const until = async (x, ms = 8000) => { for (let t = 0; t < ms; t += 200) { if (await js(x)) return true; await sleep(200); } return false; };

    await send('Page.navigate', { url: `${e.base}/` });
    assert.ok(await until(`document.querySelector('.login h1')?.textContent === 'Sign in to this Console'`), 'sign-in card shown');
    await js(`(() => { document.querySelector('.login input').value = 'wrong'; document.querySelector('.login button').click(); })()`);
    assert.ok(await until(`document.querySelector('.loginerr')?.textContent === "That token isn't right."`), 'bad token message');
    await js(`(() => { document.querySelector('.login input').value = 'ui-token-42'; document.querySelector('.login button').click(); })()`);
    assert.ok(await until(`window.__crosspoint?.status === 'ready' && !!document.querySelector('.top .brand')`, 12000), 'signed in: Console is ready');
    assert.equal(await js(`document.cookie.includes('crosspoint_session')`), false, 'the cookie is HttpOnly (invisible to page JS)');
    await send('Page.reload');
    assert.ok(await until(`window.__crosspoint?.status === 'ready'`, 12000), 'still signed in after a reload');
    ws.close();
  } finally { chrome.kill(); await e.stop(); }
});
