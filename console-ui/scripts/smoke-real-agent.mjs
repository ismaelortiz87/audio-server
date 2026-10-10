// Agent localhost UI against a REAL VDI engine (P2.6): starts a local aooserver,
// a headless Console and a headless VDI (isolated settings homes), opens the
// VDI's own UI (served by the engine with --ui-dir, `/` = agent.html for the
// vdi role) in headless Chrome and checks it reflects and drives the engine.
//   node console-ui/scripts/smoke-real-agent.mjs     (after scripts/build-desktop.sh)
import { spawn } from 'node:child_process';
import { mkdtempSync, writeFileSync, readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createServer } from 'node:net';

const repo = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const APP = process.env.APP ?? `${repo}/build/desktop-release/SonoBus_artefacts/Release/Standalone/Crosspoint.app/Contents/MacOS/Crosspoint`;
const SERVER = process.env.SERVER ?? `${repo}/build/aooserver-release/aooserver`;
const CHROME = process.env.CHROME ?? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const sleep = ms => new Promise(r => setTimeout(r, ms));
const freePort = () => new Promise(r => { const s = createServer().listen(0, '127.0.0.1', () => { const p = s.address().port; s.close(() => r(p)); }); });
let failures = 0;
const check = (ok, what) => { console.log(`${ok ? '✔' : '✖'} ${what}`); if (!ok) failures++; };
const kids = [];
const run = (cmd, args, opts) => { const p = spawn(cmd, args, { stdio: 'ignore', ...opts }); kids.push(p); return p; };

const base = mkdtempSync(join(tmpdir(), 'xp-agent-'));
const aooPort = await freePort(), conPort = await freePort(), vdiPort = await freePort(), cdpPort = await freePort();
run(SERVER, ['-p', String(aooPort)], { cwd: base });
await sleep(800);

function peer(name, role, apiPort, extra = {}) {
  const home = mkdtempSync(join(base, `${name}-`));
  const cfg = join(home, `${name}.yaml`);
  writeFileSync(cfg, `# smoke-real-agent ${name}\nrole: ${role}\nserver: 127.0.0.1:${aooPort}\ngroup: xp-agent-smoke\nusername: ${name}\n`);
  return { cfg, proc: run(APP, ['--headless', '--config', cfg, '--api-port', String(apiPort), '--ui-dir', `${repo}/console-ui`],
    { cwd: home, env: { ...process.env, CFFIXED_USER_HOME: home, ...extra } }) };
}
const con = peer('console-1', 'console', conPort);
const vdi = peer('VDI-SMOKE-01', 'vdi', vdiPort);
// Wait for both engines' APIs instead of a fixed sleep (robust under load).
for (const port of [conPort, vdiPort]) {
  for (let i = 0; i < 120; i++) { try { if ((await fetch(`http://127.0.0.1:${port}/api/v1/health`)).ok) break; } catch {} await sleep(250); }
}

const chrome = run(CHROME, ['--headless=new', '--disable-gpu', `--remote-debugging-port=${cdpPort}`, `--user-data-dir=${base}/prof`, 'about:blank']);
let wsUrl;
for (let i = 0; i < 50 && !wsUrl; i++) { try { wsUrl = (await (await fetch(`http://127.0.0.1:${cdpPort}/json`)).json()).find(t => t.type === 'page')?.webSocketDebuggerUrl; } catch {} await sleep(100); }
const ws = new WebSocket(wsUrl); await new Promise(r => ws.addEventListener('open', r, { once: true }));
let seq = 0; const waiting = new Map(); const errors = [];
ws.addEventListener('message', ev => { const m = JSON.parse(ev.data); if (m.id && waiting.has(m.id)) { waiting.get(m.id)(m); waiting.delete(m.id); }
  if (m.method === 'Runtime.exceptionThrown') errors.push(m.params.exceptionDetails.exception?.description ?? 'exception'); });
const send = (method, params = {}) => new Promise(r => { const id = ++seq; waiting.set(id, r); ws.send(JSON.stringify({ id, method, params })); });
const js = async expr => (await send('Runtime.evaluate', { expression: expr, awaitPromise: true, returnByValue: true })).result?.result?.value;
const st = () => js('JSON.stringify(__crosspoint.store.state)').then(s => (s ? JSON.parse(s) : null));
await send('Runtime.enable');

try {
  await send('Page.navigate', { url: `http://127.0.0.1:${vdiPort}/` });
  let s = null;
  for (let i = 0; i < 120; i++) { await sleep(250); s = await st(); if (s?.consoles?.length) break; }
  check(s?.self?.role === 'vdi', 'agent page loads real VDI state (role vdi)');
  check((await js(`document.querySelector('.ident .name').textContent`)) === 'VDI-SMOKE-01', 'shows the VDI name');
  check((await js(`document.querySelector('.state .big').textContent`)) === 'Connected', `state card says Connected`);
  check((await js(`document.querySelector('.lrow .who')?.textContent`)) === 'console-1', '"Listening now" lists the real Console');
  check((await js(`document.querySelector('.foot .path').textContent`)).includes('VDI-SMOKE-01.yaml'), 'footer shows the config path');

  await js(`document.querySelector('.foot .btn').click()`);
  await sleep(700);
  s = await st();
  check(s.sending === 'paused' && (await js(`document.querySelector('.state .big').textContent`)) === 'Sending paused', 'Pause sending pauses the real engine');
  await js(`document.querySelector('.foot .btn').click()`);
  await sleep(700);
  check((await st()).sending === 'active', 'Resume sending resumes it');

  await js(`document.querySelector('[data-toggle="in"]').click()`);
  await sleep(300);
  const opts = await js(`document.querySelectorAll('[data-in]').length`);
  check(opts > 0, `input picker lists the machine's real devices (${opts})`);
  if (process.env.SHOT) {  // design-review screenshot of the real page
    await send('Emulation.setDeviceMetricsOverride', { width: 520, height: 900, deviceScaleFactor: 1, mobile: false });
    await sleep(400);
    writeFileSync(process.env.SHOT, Buffer.from((await send('Page.captureScreenshot', { format: 'png' })).result.data, 'base64'));
  }
  check(errors.length === 0, `no page errors${errors.length ? ': ' + errors.join(' | ') : ''}`);
  check(readFileSync(vdi.cfg, 'utf8').startsWith('# smoke-real-agent'), 'config file untouched by browsing (no accidental write-back)');
} finally {
  ws.close();
  for (const k of kids.reverse()) { try { k.kill('SIGTERM'); } catch {} }
}
console.log(failures ? `\n${failures} failed` : '\nall passed');
process.exit(failures ? 1 : 0);
