// Phone engine smoke test (A5/A6): talks to the Console API of the Crosspoint Android
// app through `adb forward`, joins a throwaway group on the public AOO server, and
// checks that the app serves the current console-ui files.
//   adb -s emulator-5562 forward tcp:7071 tcp:7070
//   node console-ui/scripts/smoke-android.mjs            (PHONE=http://127.0.0.1:7071)
//   node console-ui/scripts/smoke-android.mjs --state    (only print the engine facts)
// Needs Node 22+ (global WebSocket and fetch). No secrets: the group is random and open.
import { readFileSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const PHONE = process.env.PHONE ?? 'http://127.0.0.1:7071';
const SERVER = process.env.AOO ?? 'aoo.sonobus.net:10998';
const ui = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const sleep = ms => new Promise(r => setTimeout(r, ms));
let failures = 0;
const check = (ok, what) => { console.log(`${ok ? '✔' : '✖'} ${what}`); if (!ok) failures++; };

// -- the UI the phone serves is the UI in the repo (A6)
for (const f of ['index.html', 'sw.js', 'src/console/app.js', 'src/console/components.js']) {
  const res = await fetch(`${PHONE}/${f}`);
  const body = await res.text();
  check(res.ok && body === readFileSync(resolve(ui, f), 'utf8'), `GET /${f} is byte-identical to console-ui/${f}`);
}

// -- the API
const ws = new WebSocket(PHONE.replace(/^http/, 'ws') + '/api/v1/ws');
const msgs = [];
let state = null, hello = null, seq = 0;
const pending = new Map();
ws.addEventListener('message', ev => {
  const m = JSON.parse(ev.data); msgs.push(m);
  if (m.t === 'hello') hello = m;
  if (m.t === 'state') state = m.state;
  if (m.t === 'patch') for (const op of m.ops) apply(op);
  if (m.t === 'ack' && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
});
function apply(op) {   // enough of RFC 6902 for the paths we look at
  const parts = op.path.split('/').slice(1).map(p => p.replace(/~1/g, '/').replace(/~0/g, '~'));
  let o = state; for (const p of parts.slice(0, -1)) o = o?.[p]; if (!o) return;
  const k = parts.at(-1);
  if (op.op === 'remove') delete o[k]; else o[k] = op.value;
}
await new Promise((ok, no) => { ws.addEventListener('open', ok); ws.addEventListener('error', () => no(new Error('cannot reach ' + PHONE))); });
for (let i = 0; i < 50 && !(state?.connection); i++) await sleep(100);
const cmd = (c, args = {}) => new Promise(r => { const id = 'c' + ++seq; pending.set(id, r); ws.send(JSON.stringify({ t: 'cmd', id, cmd: c, args })); });

check(hello?.role === 'console', `hello role is console (got ${hello?.role})`);
check(state?.self?.kind === 'android', `self.kind is android (got ${state?.self?.kind})`);
console.log('engine facts:', JSON.stringify({
  self: state?.self, devices: state?.devices, audio: state?.audio, mic: state?.mic?.device, output: state?.output?.device,
  settings: state?.settings, connection: state?.connection,
}, null, 1));
if (process.argv.includes('--state')) { console.log(JSON.stringify(state, null, 1)); process.exit(0); }

const group = process.env.GROUP ?? `xp-a5-${Math.random().toString(36).slice(2, 8)}`;
const ack = await cmd('connection.connect', { server: SERVER, group, name: 'Phone A5' });
check(ack.ok, `connection.connect ${SERVER} / ${group}: ${JSON.stringify(ack.error ?? ack.result ?? {})}`);
for (let i = 0; i < 100 && state?.connection?.state !== 'connected'; i++) await sleep(100);
check(state?.connection?.state === 'connected', `connected (state ${state?.connection?.state}, ${state?.connection?.reason ?? ''})`);
console.log('GROUP', group);
ws.close();
process.exit(failures ? 1 : 0);
