// A10 / A14: phone calls and settings problem rows (android.md sections 6, 7).
import test from 'node:test';
import assert from 'node:assert/strict';
import { clone } from '../src/lib/patch.js';
import { Client } from '../src/api/client.js';
import { MockEngine } from '../src/api/mock.js';
import { callBandText, callMicButton, blockedMicButton, micBlocked, problemRows } from '../src/lib/platform.js';
import { allHearYouText } from '../src/lib/format.js';

const tick = () => new Promise(r => setTimeout(r, 0));
async function settle(n = 5) { for (let i = 0; i < n; i++) await tick(); }
function boot(scenario) {
  const engine = new MockEngine('console', scenario, { timers: false });
  const client = new Client(() => engine.connect(), { expectRole: 'console' }).start();
  return { client, st: () => client.store.state };
}

test('A14: during a call the mic button is "Mic off / During your phone call" (disabled) and the band says so', async () => {
  const { st } = boot('android-call');
  await settle();
  assert.equal(callBandText(st()), 'Mic off during your phone call.');
  assert.deepEqual(callMicButton(st()), { label: 'Mic off', sub: 'During your phone call', disabled: true });
});

test('A14: after the call the mic stays off and the band says how to talk again; it clears on mic on or a mode change', async () => {
  const { st } = boot('android-aftercall');
  await settle();
  assert.equal(callBandText(st()), 'Mic off since your phone call. Tap the mic button to talk again.');
  assert.equal(callMicButton(st()), null, 'the button works again after the call');
  const on = clone(st());
  on.mic.on = true; on.platform.micOffByCall = false;
  assert.equal(callBandText(on), null, 'mic turned on');
  const ptt = clone(st());
  ptt.mic.mode = 'ptt';
  assert.equal(callBandText(ptt), null, 'mode changed');
});

test('A14: nothing call-related off Android or in a normal Android session', async () => {
  for (const sc of ['everyday', 'android']) {
    const { st } = boot(sc);
    await settle();
    assert.equal(callBandText(st()), null);
    assert.equal(callMicButton(st()), null);
  }
});

test('A10: settings problem rows appear only when notifications are blocked or the background is restricted', async () => {
  const { st } = boot('android');
  await settle();
  assert.deepEqual(problemRows(st()), [], 'calm until it matters');
  const other = boot('everyday');
  await settle();
  assert.deepEqual(problemRows(other.st()), [], 'null platform');
  const s = clone(st());
  s.platform.notifications = 'denied';
  s.platform.backgroundRestricted = true;
  assert.deepEqual(problemRows(s).map(r => [r.text, r.action, r.command, r.args]), [
    ["Notifications are off, so you can't see whether your mic is live outside the app.", 'Open Android settings', 'platform.openSettings', { page: 'notifications' }],
    ['Android may stop Crosspoint in the background.', 'Allow background use', 'platform.allowBackground', {}],
  ]);
});

// ---------------------------------------------------------------- A9: microphone permission (android.md section 3, 7)

test('A9: prompt: mic button "Mic blocked / Tap to allow", quiet band text, a settings row with "Allow microphone"', async () => {
  const { st } = boot('android-blocked');
  await settle();
  assert.equal(st().platform.micPermission, 'prompt');
  assert.equal(st().mic.transmitting, false, 'the engine reports not transmitting');
  assert.equal(Object.values(st().stations).some(s => s.hearsYou), false);
  assert.deepEqual(blockedMicButton(st()), { label: 'Mic blocked', sub: 'Tap to allow' });
  assert.equal(callBandText(st()), 'Microphone blocked. You can listen, but no station hears you.');
  assert.deepEqual(problemRows(st()).map(r => [r.id, r.text, r.action, r.command]), [
    ['mic', "Microphone blocked. Stations can't hear you.", 'Allow microphone', 'platform.requestMic'],
  ]);
});

test('A9: denied: sub "Allow in Android settings" and the row says "Open Android settings"', async () => {
  const { st } = boot('android-denied');
  await settle();
  assert.equal(st().platform.micPermission, 'denied');
  assert.deepEqual(blockedMicButton(st()), { label: 'Mic blocked', sub: 'Allow in Android settings' });
  assert.equal(callBandText(st()), 'Microphone blocked. You can listen, but no station hears you.');
  assert.equal(problemRows(st())[0].action, 'Open Android settings');
  assert.equal(problemRows(st())[0].command, 'platform.requestMic');
});

test('A9: blocked also in PTT mode, and a hold transmits nothing', async () => {
  const { client, st } = boot('android-blocked');
  await settle();
  await client.cmd('mic.setMode', { mode: 'ptt' }); await settle();
  assert.deepEqual(blockedMicButton(st()), { label: 'Mic blocked', sub: 'Tap to allow' });
  await client.cmd('mic.ptt', { down: true }); await settle();
  assert.equal(st().mic.transmitting, false);
  assert.equal(callBandText(st()), 'Microphone blocked. You can listen, but no station hears you.');
});

test('A9: granted: the mic returns to the state the user left it in, and the blocked texts go away', async () => {
  const { client, st } = boot('android-blocked');
  await settle();
  assert.equal(st().mic.on, true, 'left open and on');
  await client.cmd('platform.requestMic'); await settle();   // the mock grants a prompt
  assert.equal(st().platform.micPermission, 'granted');
  assert.equal(st().mic.transmitting, true, 'live again, nobody had to turn it on');
  assert.equal(blockedMicButton(st()), null);
  assert.equal(callBandText(st()), null);
  assert.deepEqual(problemRows(st()), []);
});

test('A9: denied stays denied after requestMic (only Android Settings can change it)', async () => {
  const { client, st } = boot('android-denied');
  await settle();
  await client.cmd('platform.requestMic'); await settle();
  assert.equal(st().platform.micPermission, 'denied');
});

test('A9: a phone call wins over the blocked mic button; nothing blocked off Android or when granted', async () => {
  for (const sc of ['everyday', 'android']) {
    const { st } = boot(sc);
    await settle();
    assert.equal(blockedMicButton(st()), null);
    assert.equal(micBlocked(st()), false);
  }
  const { st } = boot('android-blocked');
  await settle();
  const s = clone(st());
  s.platform.phoneCall = true;
  assert.equal(blockedMicButton(s), null);
  assert.equal(callBandText(s), 'Mic off during your phone call.');
});

test('A9: requestMic is not_supported off Android', async () => {
  const { client } = boot('everyday');
  await settle();
  await assert.rejects(() => client.cmd('platform.requestMic'), /Android/);
});

test('review 2: one known station reads "{A} hears you", not "All 1 stations"', async () => {
  const one = boot('android-one');
  await settle();
  const o = Object.values(one.st().stations);
  assert.equal(allHearYouText(o.filter(s => s.hearsYou), o), 'VDI-ACCT-07 hears you'.replace('VDI-', ''));
  const many = boot('everyday');
  await settle();
  const m = Object.values(many.st().stations);
  assert.equal(allHearYouText(m, m), 'All 3 stations hear you');
});

test('review 3: echoRisk needs a station that hears you, not just an open mic', async () => {
  const { client, st } = boot('android-echo');
  await settle(10);
  assert.equal(st().platform.echoRisk, true);
  await client.cmd('station.setTalk', { station: 'VDI-ACCT-07', on: false }); await settle();
  assert.equal(st().mic.transmitting, true, 'the mic is still open');
  assert.equal(st().platform.echoRisk, false, 'but nobody hears it');
  await client.cmd('station.setTalk', { station: 'VDI-ACCT-07', on: true }); await settle();
  assert.equal(st().platform.echoRisk, true);
});
