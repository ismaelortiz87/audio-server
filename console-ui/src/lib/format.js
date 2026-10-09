// Text formatting shared by the Console and agent UIs (spec §3, §6).

const MINUS = '−';

/** "−6.0 dB", "+2.5 dB", "0.0 dB" */
export const dbText = d => (d > 0 ? '+' : d < 0 ? MINUS : '') + Math.abs(d).toFixed(1) + ' dB';

/** "Centre", "L 50", "R 100" */
export const panText = p => (Math.abs(p) < 0.005 ? 'Centre' : (p < 0 ? 'L ' : 'R ') + Math.round(Math.abs(p) * 100));

/** Long form for screen readers: "Left 50", "Centre", "Right 100" */
export const panLabel = p => (Math.abs(p) < 0.005 ? 'Centre' : (p < 0 ? 'Left ' : 'Right ') + Math.round(Math.abs(p) * 100));

/** Station names shown in the live band and pucks drop a leading "VDI-". */
export const shortName = name => name.replace(/^VDI-/i, '');

/** ["A"] → "A", ["A","B"] → "A and B", ["A","B","C"] → "A, B and C" */
export function listNames(names) {
  if (names.length <= 1) return names.join('');
  return names.slice(0, -1).join(', ') + ' and ' + names[names.length - 1];
}

/** Agrees the verb with the list: hear(s), doesn't/don't. */
export const hears = n => (n === 1 ? 'hears' : 'hear');
export const doesnt = n => (n === 1 ? "doesn't" : "don't");

/** "09:12" today, "8 Oct" otherwise (spec §3.5 offline line). */
export function lastSeenText(iso, now = new Date()) {
  const d = new Date(iso);
  if (Number.isNaN(d.getTime())) return '';
  const sameDay = d.toDateString() === now.toDateString();
  return sameDay
    ? d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', hour12: false })
    : d.toLocaleDateString([], { day: 'numeric', month: 'short' });
}

/** dBFS (or null) → 0..1 for drawing; −60 dBFS maps to 0. */
export const dbToUnit = db => (db === null || db === undefined ? 0 : Math.max(0, Math.min(1, (db + 60) / 60)));

/** Meter readout: "−16 dB", or "−∞ dB" for silence. */
export const meterDbText = db => (db === null || db === undefined || db < -89 ? MINUS + '∞ dB' : MINUS + Math.round(Math.abs(db)) + ' dB');
