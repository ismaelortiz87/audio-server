// node --test console-ui/test
import test from 'node:test';
import assert from 'node:assert/strict';
import { applyPatch, diff, clone } from '../src/lib/patch.js';
import { Client } from '../src/api/client.js';
import { MockEngine } from '../src/api/mock.js';
import { panText, dbText, listNames } from '../src/lib/format.js';
import { withStereoReceive } from '../src/lib/rtc.js';

const tick = () => new Promise(r => setTimeout(r, 0));
async function settle(n = 5) { for (let i = 0; i < n; i++) await tick(); }

function boot(role, scenario, opts = {}) {
  const engine = new MockEngine(role, scenario, { timers: false, ...opts });
  const statuses = [];
  const client = new Client(() => engine.connect(), { expectRole: opts.expectRole ?? role, token: opts.clientToken, onStatus: s => statuses.push(s) }).start();
  return { engine, client, statuses, st: () => client.store.state };
}

test('diff + applyPatch round-trip, including escaped keys and deletions', () => {
  const a = { x: 1, s: { 'a/b': { v: 1 }, keep: [1, 2] }, gone: true };
  const b = { x: 2, s: { 'a/b': { v: 2, w: 3 }, keep: [1, 2, 3] }, added: { y: null } };
  const ops = diff(a, b);
  assert.deepEqual(applyPatch(clone(a), ops), b);
  assert.ok(ops.some(o => o.path === '/s/a~1b/v'));
});

test('client receives hello + state and applies command patches', async () => {
  const { client, st } = boot('console', 'everyday');
  await settle();
  assert.equal(client.status, 'ready');
  assert.equal(st().stations['VDI-DEV-02'].level, -6);
  await client.cmd('station.setLevel', { station: 'VDI-DEV-02', db: -12.5 });
  await settle();
  assert.equal(st().stations['VDI-DEV-02'].level, -12.5);
});

test('levels are clamped and bad args rejected with a code', async () => {
  const { client, st } = boot('console', 'everyday');
  await settle();
  await client.cmd('station.setLevel', { station: 'VDI-DEV-02', db: 40 });
  await settle();
  assert.equal(st().stations['VDI-DEV-02'].level, 6);
  await assert.rejects(client.cmd('station.setMute', { station: 'VDI-DEV-02', on: 'yes' }), { code: 'bad_request' });
  await assert.rejects(client.cmd('station.setMute', { station: 'NOPE', on: true }), { code: 'not_found' });
});

test('hearsYou: open mic, talk toggles and solo narrowing (spec §4.1)', async () => {
  const { client, st } = boot('console', 'everyday');
  await settle();
  const hears = () => st().stationOrder.filter(id => st().stations[id].hearsYou);
  assert.deepEqual(hears(), ['VDI-ACCT-07', 'VDI-DEV-02', 'VDI-QA-11']);

  await client.cmd('station.setTalk', { station: 'VDI-QA-11', on: false });
  await settle();
  assert.deepEqual(hears(), ['VDI-ACCT-07', 'VDI-DEV-02']);

  await client.cmd('station.setSolo', { station: 'VDI-ACCT-07', on: true });
  await settle();
  assert.deepEqual(hears(), ['VDI-ACCT-07'], 'solo narrows the mic to soloed stations');
  assert.equal(st().stations['VDI-DEV-02'].talk, true, 'the stored talk choice is kept');

  await client.cmd('station.setSolo', { station: 'VDI-ACCT-07', on: false });
  await settle();
  assert.deepEqual(hears(), ['VDI-ACCT-07', 'VDI-DEV-02'], 'un-solo restores the previous talk choices');

  await client.cmd('mic.setOn', { on: false });
  await settle();
  assert.deepEqual(hears(), [], 'mic off means nobody hears you');
});

test('hearsYou is false for lost and offline stations', async () => {
  const { st } = boot('console', 'problem');
  await settle();
  assert.equal(st().stations['VDI-QA-11'].hearsYou, false);
  assert.equal(st().stations['VDI-OPS-04'].hearsYou, false);
  assert.equal(st().stations['VDI-ACCT-07'].hearsYou, true);
});

test('push-to-talk: held while pressed, released when the client disconnects (api §2.4)', async () => {
  const engine = new MockEngine('console', 'everyday', { timers: false });
  const a = new Client(() => engine.connect(), {}).start();
  const b = new Client(() => engine.connect(), {}).start();
  await settle();
  await a.cmd('mic.setMode', { mode: 'ptt' });
  await settle();
  assert.equal(b.store.state.mic.transmitting, false);
  await assert.rejects(a.cmd('mic.setOn', { on: true }), { code: 'wrong_mode' });

  await a.cmd('mic.ptt', { down: true });
  await settle();
  assert.equal(b.store.state.mic.transmitting, true);
  assert.equal(b.store.state.stations['VDI-ACCT-07'].hearsYou, true);

  a.stop(); // dropped connection while holding
  await settle(10);
  assert.equal(b.store.state.mic.pttHeld, false);
  assert.equal(b.store.state.mic.transmitting, false, 'a dropped client must not leave the mic on');
});

test('a revision gap triggers a resync instead of applying out of order', async () => {
  const { client, engine, st } = boot('console', 'everyday');
  await settle();
  const rev = client.store.rev;
  // Simulate a lost patch: the client sees rev+2 directly.
  client.handle({ t: 'patch', rev: rev + 2, ops: [{ op: 'set', path: '/stations/VDI-DEV-02/level', value: 3 }] });
  await settle();
  assert.equal(st().stations['VDI-DEV-02'].level, -6, 'out-of-order patch ignored');
  assert.equal(client.store.rev, engine.rev, 'resynced to the engine revision');
});

test('the console UI refuses to run against an agent', async () => {
  const { statuses } = boot('vdi', 'listening', { expectRole: 'console' });
  await settle();
  assert.ok(statuses.includes('wrong-role'));
});

test('token auth: good token gets state, bad token is fatal', async () => {
  const good = boot('console', 'everyday', { auth: 'token', token: 's3', clientToken: 's3' });
  const bad = boot('console', 'everyday', { auth: 'token', token: 's3', clientToken: 'nope' });
  await settle(10);
  assert.equal(good.client.status, 'ready');
  assert.ok(bad.statuses.includes('fatal'));
});

test('agent: setInput switches, reports previous for Undo, clears missing', async () => {
  const { client, st } = boot('vdi', 'nodevice');
  await settle();
  assert.equal(st().input.status, 'missing');
  const res = await client.cmd('agent.setInput', { node: 'loopback.monitor' });
  await settle();
  assert.equal(res.previous, 'loopback_sink.monitor');
  assert.equal(st().input.status, 'ok');
  assert.equal(st().input.node, 'loopback.monitor');
});

test('agent: pause and resume only change sending', async () => {
  const { client, st } = boot('vdi', 'listening');
  await settle();
  await client.cmd('agent.pause');
  await settle();
  assert.equal(st().sending, 'paused');
  await client.cmd('agent.resume');
  await settle();
  assert.equal(st().sending, 'active');
});

test('meters: pre-fader station levels, only online stations, only when subscribed', async () => {
  const { client, engine } = boot('console', 'problem');
  await settle();
  const frames = [];
  client.onMeters(m => frames.push(m));
  engine.tick();
  await settle();
  assert.equal(frames.length, 0, 'no meters before subscribing');
  client.subscribe('meters');
  await settle();
  engine.tick();
  await settle();
  assert.equal(frames.length, 1);
  assert.deepEqual(Object.keys(frames[0].stations).sort(), ['VDI-ACCT-07', 'VDI-DEV-02']);
});

test('format helpers match the spec copy', () => {
  assert.equal(panText(0), 'Centre');
  assert.equal(panText(-0.5), 'L 50');
  assert.equal(panText(1), 'R 100');
  assert.equal(dbText(-6), '−6.0 dB');
  assert.equal(dbText(2.5), '+2.5 dB');
  assert.equal(listNames(['A', 'B', 'C']), 'A, B and C');
});

test('rtc: the browser offer asks to receive Opus in stereo (pans survive), once', () => {
  const sdp = 'v=0\r\nm=audio 9 UDP/TLS/RTP/SAVPF 111 63\r\na=rtpmap:111 opus/48000/2\r\na=fmtp:111 minptime=10;useinbandfec=1\r\na=rtpmap:63 red/48000/2\r\na=fmtp:63 111/111\r\n';
  const out = withStereoReceive(sdp);
  assert.match(out, /a=fmtp:111 minptime=10;useinbandfec=1;stereo=1\r\n/);
  assert.match(out, /a=fmtp:63 111\/111\r\n/, 'other codecs untouched');
  assert.equal(withStereoReceive(out), out, 'idempotent');
});

test('mock: exclusive solo clears the other solos; plain solo adds (P5.7)', async () => {
  const engine = new MockEngine('console', 'busy', { timers: false });
  const client = new Client(() => engine.connect(), { expectRole: 'console' }).start();
  await settle(10);
  const ids = Object.keys(client.store.state.stations).slice(0, 3);
  assert.ok(ids.length >= 2, 'scenario has stations');
  await client.cmd('station.setSolo', { station: ids[0], on: true }); await settle();
  await client.cmd('station.setSolo', { station: ids[1], on: true }); await settle();
  assert.deepEqual(ids.slice(0, 2).map(i => client.store.state.stations[i].solo), [true, true], 'plain solos add up');
  await client.cmd('station.setSolo', { station: ids[0], on: true, exclusive: true }); await settle();
  const solos = Object.values(client.store.state.stations).filter(s => s.solo).map(s => s.id);
  assert.deepEqual(solos, [ids[0]], 'exclusive leaves only that one soloed');
  await assert.rejects(client.cmd('station.setSolo', { station: ids[0], on: true, exclusive: 'yes' }), /exclusive must be true or false/);
});
