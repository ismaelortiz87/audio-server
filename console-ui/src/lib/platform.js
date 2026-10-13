// Android-only state, as pure functions (android.md sections 6 and 7) so the
// copy can be tested without a DOM. `state.platform` is null off Android, and
// other tasks (A8, A9) add keys to it: read only what is named here.

const MIC_OFF_DURING_CALL = 'Mic off during your phone call.';
const MIC_OFF_AFTER_CALL = 'Mic off since your phone call. Tap the mic button to talk again.';

export const onPhoneCall = st => !!st.platform?.phoneCall;

// android.md section 3: the mic is "blocked" while Android has not granted it.
// `prompt`: the system dialog can still be shown. `denied`: only Android Settings can change it.
export const micBlocked = st => { const m = st.platform?.micPermission; return m === 'prompt' || m === 'denied'; };
// A22 (android.md 14): the active Bluetooth route — null off Android or when no
// Bluetooth headset/car is connected, else { name, stereo }.
export const btRoute = st => st.platform?.bluetoothRoute ?? null;
const MIC_BLOCKED_BAND = 'Microphone blocked. You can listen, but no station hears you.';

/**
 * The live band's quiet text for a phone call, or null. During the call the
 * band says so; after it, in open mode with the mic still off, it says the mic
 * stayed off. It clears the moment the mic is turned on or the mode changes
 * (the engine clears micOffByCall; the checks here make it hold for a stale patch too).
 */
export function callBandText(st) {
  if (onPhoneCall(st)) return MIC_OFF_DURING_CALL;
  if (micBlocked(st)) return MIC_BLOCKED_BAND;     // replaces the other quiet texts (section 3)
  if (st.platform?.micOffByCall && st.mic.mode === 'open' && !st.mic.on) return MIC_OFF_AFTER_CALL;
  return null;
}

/** Mic button while the mic is blocked (idle style, crossed mic), or null. Tap sends platform.requestMic. */
export function blockedMicButton(st) {
  if (!micBlocked(st) || onPhoneCall(st)) return null;
  return { label: 'Mic blocked', sub: st.platform.micPermission === 'denied' ? 'Allow in Android settings' : 'Tap to allow' };
}

/** Mic button override during a call (disabled), or null. */
export function callMicButton(st) {
  return onPhoneCall(st) ? { label: 'Mic off', sub: 'During your phone call', disabled: true } : null;
}

/**
 * Settings problem rows (android.md section 7), only the ones that exist now.
 * Each: { id, text, action, command, args }. The microphone row comes first.
 */
export function problemRows(st) {
  const p = st.platform;
  if (!p) return [];
  const rows = [];
  if (micBlocked(st)) {
    // Both rows send platform.requestMic: the engine shows the system dialog (prompt) or opens the app's settings page (denied).
    rows.push({ id: 'mic', text: "Microphone blocked. Stations can't hear you.",
      action: p.micPermission === 'denied' ? 'Open Android settings' : 'Allow microphone', command: 'platform.requestMic', args: {} });
  }
  if (p.notifications === 'denied') {
    rows.push({ id: 'notifications', text: "Notifications are off, so you can't see whether your mic is live outside the app.",
      action: 'Open Android settings', command: 'platform.openSettings', args: { page: 'notifications' } });
  }
  if (p.backgroundRestricted) {
    rows.push({ id: 'background', text: 'Android may stop Crosspoint in the background.',
      action: 'Allow background use', command: 'platform.allowBackground', args: {} });
  }
  return rows;
}
