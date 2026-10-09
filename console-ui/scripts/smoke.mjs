// Browser smoke test: drives real Chrome (DevTools protocol, no deps) against
// the mock and checks that clicks and keys reach the engine. Needs Chrome and
// the dev server:  node scripts/serve.mjs &  node scripts/smoke.mjs
//   CHROME=/path/to/chrome   UI=http://localhost:5173

import { spawn } from 'node:child_process';
import { mkdtempSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const CHROME = process.env.CHROME ?? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const UI = process.env.UI ?? 'http://localhost:5173';
const port = 9300 + Math.floor(Math.random() * 500);
const chrome = spawn(CHROME, ['--headless=new', '--disable-gpu', `--remote-debugging-port=${port}`,
  `--user-data-dir=${mkdtempSync(join(tmpdir(), 'xp-smoke-'))}`, '--window-size=1280,860', 'about:blank'], { stdio: 'ignore' });

const sleep = ms => new Promise(r => setTimeout(r, ms));
let failures = 0;
const check = (ok, what) => { console.log(`${ok ? '✔' : '✖'} ${what}`); if (!ok) failures++; };

async function target() {
  for (let i = 0; i < 50; i++) {
    try {
      const list = await (await fetch(`http://127.0.0.1:${port}/json`)).json();
      const page = list.find(t => t.type === 'page');
      if (page) return page.webSocketDebuggerUrl;
    } catch {}
    await sleep(100);
  }
  throw new Error('Chrome did not start');
}

const ws = new WebSocket(await target());
await new Promise(r => ws.addEventListener('open', r, { once: true }));
let seq = 0;
const waiting = new Map();
const errors = [];
ws.addEventListener('message', ev => {
  const m = JSON.parse(ev.data);
  if (m.id && waiting.has(m.id)) { waiting.get(m.id)(m); waiting.delete(m.id); }
  if (m.method === 'Runtime.exceptionThrown') errors.push(m.params.exceptionDetails.exception?.description ?? m.params.exceptionDetails.text);
  if (m.method === 'Runtime.consoleAPICalled' && m.params.type === 'error') errors.push(m.params.args.map(a => a.value).join(' '));
});
const send = (method, params = {}) => new Promise(r => { const id = ++seq; waiting.set(id, r); ws.send(JSON.stringify({ id, method, params })); });
const js = async expr => (await send('Runtime.evaluate', { expression: expr, awaitPromise: true, returnByValue: true })).result?.result?.value;
const key = async (k, code = k) => {
  await send('Input.dispatchKeyEvent', { type: 'keyDown', key: k, code, text: k.length === 1 ? k : undefined });
  await send('Input.dispatchKeyEvent', { type: 'keyUp', key: k, code });
};
const st = () => js('JSON.stringify(__crosspoint.store.state)').then(JSON.parse);
const open = async url => { await send('Page.navigate', { url }); await sleep(800); };

await send('Runtime.enable');
await send('Page.enable');

// ---------------------------------------------------------------- console
await open(`${UI}/?mock=everyday`);
check((await st())?.self?.role === 'console', 'console loads mock state');

await js(`document.querySelector('.station .tog.solo').click()`);
await sleep(150);
let s = await st();
check(s.stations['VDI-ACCT-07'].solo === true, 'clicking Solo solos the first station');
check(s.stations['VDI-DEV-02'].hearsYou === false, 'solo narrows the mic (DEV-02 no longer hears you)');
check(await js(`document.querySelectorAll('.tog.talk[data-state="held"]').length`) === 2, 'other talk buttons show "Paused by solo"');
check((await js(`document.querySelector('.liveband').textContent`)).includes('Solo: only ACCT-07'), 'live band says "Solo: only ACCT-07 hears you"');

await key('s'); await sleep(150);
check((await st()).stations['VDI-ACCT-07'].solo === false, 'S toggles solo on the selected station');
await key('2'); await key('m'); await sleep(150);
check((await st()).stations['VDI-DEV-02'].mute === true, '2 then M mutes the second station');
await key('ArrowRight', 'ArrowRight'); await sleep(150);
check((await st()).stations['VDI-DEV-02'].pan === 0.5, '→ moves the selected station to the next snap');
await key('`', 'Backquote'); await sleep(150);
check((await st()).mic.on === false, '` turns the mic off');
check((await js(`document.querySelector('.liveband').textContent`)).includes('Mic off'), 'live band says "Mic off"');

await js(`[...document.querySelectorAll('.seg button')].find(b => b.textContent === 'Push to talk').click()`);
await sleep(150);
check((await st()).mic.mode === 'ptt', 'Push to talk mode selected');
await send('Input.dispatchKeyEvent', { type: 'keyDown', key: ' ', code: 'Space', text: ' ' });
await sleep(150);
check((await st()).mic.transmitting === true, 'holding Space transmits');
await send('Input.dispatchKeyEvent', { type: 'keyUp', key: ' ', code: 'Space' });
await sleep(150);
check((await st()).mic.transmitting === false, 'releasing Space stops');

await js(`document.querySelector('.stations .station:nth-child(3) .pan button:nth-child(2)').click()`);
await sleep(150);
check((await st()).stations['VDI-QA-11'].pan === -1, 'clicking L on the placement control pans hard left');

// settings, health details, connection flow (P5.2-P5.4)
await open(`${UI}/?mock=everyday`);
await js(`document.querySelector('.station .health').click()`);
await sleep(100);
check(await js(`!document.querySelector('.station .hdetail').hidden && document.querySelector('.station .hdetail').textContent.includes('Latency')`), 'tapping the health line shows link details');
await js(`document.querySelector('.top .iconbtn').click()`);
await sleep(150);
check(await js(`!document.querySelector('.sheet').hidden`), 'gear opens settings');
await js(`(() => { const s = [...document.querySelectorAll('.sheet select')][2]; s.value = '-24'; s.dispatchEvent(new Event('change')); })()`);
await sleep(150);
check((await st()).settings.soloDimDb === -24, 'solo dim amount is editable');
await js(`(() => { const s = document.querySelector('.sheet select'); s.value = 'airpods-mic'; s.dispatchEvent(new Event('change')); })()`);
await sleep(150);
check((await st()).mic.device === 'AirPods Pro', 'microphone can be changed');
await js(`document.querySelector('.sheet .iconbtn').focus()`);
await key('Escape', 'Escape'); await sleep(150);
check(await js(`document.querySelector('.sheet').hidden && document.activeElement === document.querySelector('.top .iconbtn')`), 'Escape closes settings and returns focus to the gear');

await open(`${UI}/?mock=failed`);
check(await js(`!document.querySelector('.top .btn.small').hidden`), 'failed state offers a Settings button');
await js(`document.querySelector('.top .btn.small').click()`);
await sleep(150);
await js(`(() => { const [srv, grp, pw, nm] = document.querySelectorAll('.connform input'); grp.value = 'ops2'; pw.value = 'wrong'; document.querySelector('.connform button').click(); })()`);
await sleep(200);
check(await js(`!document.querySelector('.connerr').hidden && document.querySelector('.connerr').textContent.includes('Wrong group password')`), 'a bad password shows the reason in the form');
await js(`(() => { const pw = document.querySelectorAll('.connform input')[2]; pw.value = 's3cret'; document.querySelector('.connform button').click(); })()`);
await sleep(200);
s = await st();
check(s.connection.state === 'connected' && s.connection.group === 'ops2', 'connecting from the form uses the typed group');
check((await js(`document.querySelector('.conn').textContent`)).includes('Connected'), 'top bar shows Connected');

// PWA (P7.7) and the unreachable-engine state: the dev server has no engine,
// so the real (non-mock) page must say so, and Chrome must consider it installable.
await open(`${UI}/`);
await sleep(5000);
check(await js(`document.querySelector('.overlay h1')?.textContent === "Can't reach the Console server"`), 'no engine → "Can\'t reach the Console server"');
check(await js(`navigator.serviceWorker.getRegistration().then(r => !!r)`), 'service worker registered on localhost');
const inst = (await send('Page.getInstallabilityErrors')).result?.installabilityErrors ?? ['(no result)'];
check(inst.length === 0, `installable as a PWA${inst.length ? ': ' + JSON.stringify(inst) : ''}`);
const man = (await send('Page.getAppManifest')).result;
check(!!man?.url && !(man.errors ?? []).length, 'manifest parsed without errors');
errors.length = 0; // the page legitimately logs WebSocket failures here
await js(`navigator.serviceWorker.getRegistrations().then(rs => Promise.all(rs.map(r => r.unregister())))`);

// ---------------------------------------------------------------- agent
await open(`${UI}/agent.html?mock=nodevice`);
check((await st())?.input?.status === 'missing', 'agent loads the missing-device state');
await js(`document.querySelector('[data-in="loopback.monitor"]').click()`);
await sleep(200);
s = await st();
check(s.input.status === 'ok' && s.input.node === 'loopback.monitor', 'picking an input fixes the missing device');
check((await js(`document.querySelector('.toast').textContent`)).includes('Saved to vdi.yaml'), 'shows "Saved to vdi.yaml"');
check(await js(`document.querySelector('[data-undo]') === null`), 'no Undo back to a missing device');

await open(`${UI}/agent.html?mock=listening`);
await js(`document.querySelector('[data-toggle="in"]').click()`);
await sleep(150);
await js(`document.querySelector('[data-in="remote_mic"]').click()`);
await sleep(200);
check((await st()).input.node === 'remote_mic', 'switching input from the picker');
await js(`document.querySelector('[data-undo]').click()`);
await sleep(200);
check((await st()).input.node === 'loopback_sink.monitor', 'Undo restores the previous node');

check(errors.length === 0, `no page errors${errors.length ? ': ' + errors.join(' | ') : ''}`);
ws.close();
chrome.kill();
console.log(failures ? `\n${failures} failed` : '\nall passed');
process.exit(failures ? 1 : 0);
