// A10 / A14: phone calls and settings problem rows (android.md sections 6, 7).
import test from 'node:test';
import assert from 'node:assert/strict';
import { clone } from '../src/lib/patch.js';
import { Client } from '../src/api/client.js';
import { MockEngine } from '../src/api/mock.js';
import { callBandText, callMicButton, problemRows } from '../src/lib/platform.js';

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
