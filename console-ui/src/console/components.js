// Console components (spec §3). Each is built once and updated in place from
// state, so focus, drags and hover survive patches. Views never change state
// directly: they send commands (api §5) and re-render from the patches.

import { h, setText, setAttr, setHTML, ICONS } from '../lib/dom.js';
import { dbText, panText, panLabel, shortName, listNames, hears, doesnt, lastSeenText, echoNoticeShown, ECHO_NOTICE, allHearYouText } from '../lib/format.js';
import { AUDIO } from '../lib/tokens.js';
import { latest } from '../lib/boot.js';
import { callBandText, callMicButton, blockedMicButton, micBlocked, problemRows } from '../lib/platform.js';

const STATION_VAR = i => `var(--s${(i % 4) + 1})`;
const ordered = st => st.stationOrder.map(id => st.stations[id]).filter(Boolean);
const online = s => s.presence === 'online';
const anySolo = st => Object.values(st.stations).some(s => s.solo);
const isDimmed = (st, s) => anySolo(st) && !s.solo && !s.mute;
const soloHeld = (st, s) => anySolo(st) && !s.solo;

// android.md section 2: touch-first devices get "Tap…" copy and no keyboard
// reference. `matchMedia` is absent under the test harness, so default to the
// desktop copy there. `(pointer: coarse)` also covers a phone PWA (P7.7), which
// is the point: this is not Android-specific behaviour.
export const coarsePointer = () =>
  typeof globalThis.matchMedia === 'function' && globalThis.matchMedia('(pointer: coarse)').matches;
// The Android shell's emulator (and some devices with a stylus or a paired mouse)
// report a fine pointer, so a phone engine (`self.kind === 'android'`) counts as
// touch-first too (A7).
export const touchFirst = st => coarsePointer() || st?.self?.kind === 'android';
const tapCopy = (st, touch, desktop) => (touchFirst(st) ? touch : desktop);

// --------------------------------------------------------------- 3.1 top bar
export function topBar(ctx) {
  const conn = h('span.conn');
  const fix = h('button.btn.small', { hidden: true, onclick: () => ctx.openSettings() }, 'Settings');
  const gear = h('button.iconbtn', { 'aria-label': 'Settings', title: 'Settings', onclick: () => ctx.openSettings() });
  gear.innerHTML = ICONS.gear;
  ctx.settingsButton = gear;
  const el = h('header.top', h('span.brand', 'Crosspoint'), conn, fix, h('span.spacer'), gear);
  return {
    el,
    update(st) {
      const c = st.connection;
      const known = st.stationOrder.length, up = ordered(st).filter(online).length;
      let html;
      if (c.state === 'connecting') html = `<span class="dot warn pulse"></span>Connecting to&nbsp;<b>${esc(c.group)}</b>…`;
      else if (c.state === 'reconnecting') html = `<span class="dot warn pulse"></span>Reconnecting… next try in <span class="num">${c.retryInSec ?? 0}s</span>`;
      else if (c.state === 'failed') html = `<span class="dot crit"></span>${esc(c.reason ?? 'Not connected')}`;
      else html = `<span class="dot good"></span><span>Connected</span><span class="grp">· ${esc(c.group)} · ${up} of ${known} stations</span>`;
      setHTML(conn, html);
      fix.hidden = c.state !== 'failed';
    },
  };
}

// --------------------------------------------------------------- 3.2 live band
export function liveBand() {
  const text = h('span');
  const el = h('div.liveband', { role: 'status', 'aria-live': 'polite' }, h('span.pill-live', 'LIVE'), text);
  return {
    el,
    update(st) {
      const c = st.connection.state;
      if (c === 'connecting' || c === 'failed') { el.hidden = true; return; }
      // A14 (android.md section 6): a phone call, or the mic staying off after one, replaces the other quiet texts.
      const callText = callBandText(st);
      if (callText) { el.hidden = false; el.classList.add('quiet'); setText(text, callText); return; }
      // Count every known station (spec §3.2): lost/offline ones are named as
      // not hearing you, so "All n" only appears when truly everyone hears.
      const all = ordered(st);
      const targets = all.filter(s => s.hearsYou);
      const tx = st.mic.transmitting;
      let quiet = !(tx && targets.length), msg;
      if (!tx) {
        msg = st.mic.mode === 'ptt'
          ? tapCopy(st, 'Push to talk: hold the mic button. Nobody hears you now.',
                    'Push to talk: hold the mic button or Space. Nobody hears you now.')
          : 'Mic off. No station hears you.';
      } else if (!targets.length) msg = 'Mic open, but no station is set to hear you.';
      else if (targets.length === all.length) msg = allHearYouText(targets, all);
      else if (anySolo(st)) msg = `Solo: only ${listNames(targets.map(s => shortName(s.name)))} ${hears(targets.length)} you`;
      else {
        const rest = all.filter(s => !s.hearsYou);
        msg = `${listNames(targets.map(s => shortName(s.name)))} ${hears(targets.length)} you · ${listNames(rest.map(s => shortName(s.name)))} ${doesnt(rest.length)}`;
      }
      el.hidden = false;
      el.classList.toggle('quiet', quiet);
      setText(text, msg);
    },
  };
}

// --------------------------------------------------------------- 3.3 placement
export function placement(ctx) {
  const field = h('div.field');
  field.append(h('div.axis'));
  for (const v of AUDIO.panSnaps) field.append(h(`div.tick${v === 0 ? '.c' : ''}`, { style: { left: fieldX(v) } }));
  field.append(h('span.end', { style: { left: '0' } }, 'L'), h('span.end', { style: { right: '0' } }, 'R'));
  const el = h('section.stage', { 'aria-label': 'Stereo placement' },
    h('div.stagehead', h('span.label', 'Placement'), h('span.spacer'),
      h('button.btn.small', { title: 'Spread stations evenly from left to right', onclick: () => ctx.client.fire('stations.spread') }, 'Spread'),
      h('button.btn.small', { title: 'Put every station in the centre', onclick: () => ctx.client.fire('stations.centerAll') }, 'All centre')),
    field);
  /** @type {Map<string, HTMLElement>} */ const pucks = new Map();

  function makePuck(s) {
    const sw = h('span.swatch');
    const name = h('span');
    const mi = h('span'); mi.innerHTML = ICONS.mic; mi.firstChild.classList.add('mi');
    const p = h('div.puck', { tabindex: 0, role: 'slider', 'aria-valuemin': -100, 'aria-valuemax': 100 }, sw, name, mi.firstChild);
    p._sw = sw; p._name = name;
    const send = latest(v => ctx.client.fire('station.setPan', { station: s.id, pan: v }));
    p.addEventListener('pointerdown', e => {
      e.preventDefault(); ctx.select(s.id); p.setPointerCapture(e.pointerId); p.classList.add('dragging');
      const move = ev => {
        const r = field.getBoundingClientRect(), pad = parseFloat(getComputedStyle(field).getPropertyValue('--pad')) || 0;
        let v = ((ev.clientX - r.left - pad) / (r.width - 2 * pad)) * 2 - 1;
        v = Math.max(-1, Math.min(1, v));
        const snap = AUDIO.panSnaps.find(x => Math.abs(x - v) < AUDIO.panSnapRadius);
        v = snap !== undefined ? snap : Math.round(v * 100) / 100;
        ctx.ui.dragPan = { id: s.id, pan: v };
        send(v);
        ctx.render();
      };
      const up = () => {
        p.classList.remove('dragging');
        p.removeEventListener('pointermove', move); p.removeEventListener('pointerup', up); p.removeEventListener('pointercancel', up);
        setTimeout(() => { ctx.ui.dragPan = null; ctx.render(); }, 120);
      };
      p.addEventListener('pointermove', move); p.addEventListener('pointerup', up); p.addEventListener('pointercancel', up);
    });
    p.addEventListener('keydown', e => {
      if (e.key !== 'ArrowLeft' && e.key !== 'ArrowRight') return;
      e.preventDefault(); e.stopPropagation();
      ctx.nudge(s.id, e.key === 'ArrowLeft' ? -1 : 1);
    });
    field.append(p);
    return p;
  }

  return {
    el,
    update(st, ui) {
      const list = ordered(st);
      for (const [id, p] of pucks) if (!st.stations[id]) { p.remove(); pucks.delete(id); }
      for (const s of list) {
        const p = pucks.get(s.id) ?? pucks.set(s.id, makePuck(s)).get(s.id);
        const pan = ui.dragPan?.id === s.id ? ui.dragPan.pan : s.pan;
        p._pan = pan;
        p._sw.style.background = STATION_VAR(s.colorIndex);
        setText(p._name, shortName(s.name));
        p.classList.toggle('dim', isDimmed(st, s));
        p.classList.toggle('muted', s.mute);
        p.classList.toggle('live', s.hearsYou);
        p.classList.toggle('sel', ui.selected === s.id);
        setAttr(p, 'aria-label', `${s.name} placement`);
        setAttr(p, 'aria-valuenow', Math.round(pan * 100));
        setAttr(p, 'aria-valuetext', panText(pan));
        p.style.left = fieldX(pan);
      }
      layoutPucks(field, list.map(s => pucks.get(s.id)));
    },
  };
}

const fieldX = v => `calc(var(--pad) + (100% - 2 * var(--pad)) * ${(v + 1) / 2})`;

/** Stack pucks whose pixel extents would overlap (spec §3.3). */
function layoutPucks(field, pucks) {
  const pad = parseFloat(getComputedStyle(field).getPropertyValue('--pad')) || 0;
  const span = field.clientWidth - 2 * pad;
  const px = v => ((v + 1) / 2) * span;
  const rows = [];
  for (const p of [...pucks].sort((a, b) => a._pan - b._pan)) {
    const half = (p.offsetWidth || 90) / 2 + 6;
    let r = 0;
    while (rows[r] !== undefined && px(p._pan) - half < rows[r] && r < 3) r++;
    rows[r] = px(p._pan) + half;
    p._row = r;
  }
  const n = Math.max(1, rows.length);
  for (const p of pucks) p.style.top = n === 1 ? '50%' : `${22 + p._row * (56 / (n - 1))}%`;
}

// --------------------------------------------------------------- 3.9 web audio link
// Only in the web Console (a gateway on this origin). Hidden once connected.
export function audioLink(ctx) {
  const text = h('span');
  const btn = h('button.btn.small.primary', { onclick: () => ctx.rtc.start() });
  const el = h('div.audiolink', { role: 'status', 'aria-live': 'polite', hidden: true }, h('span.dot'), text, btn);
  ctx.rtc.on(r => {
    const show = r.state !== 'unavailable' && !(r.state === 'connected' && !r.micBlocked);
    el.hidden = !show;
    if (!show) return;
    const dot = el.firstChild;
    dot.className = 'dot' + (r.state === 'failed' ? ' crit' : r.state === 'starting' ? ' warn pulse' : r.micBlocked ? ' warn' : '');
    const [msg, label] =
      r.state === 'idle' ? ["Audio isn't connected in this browser.", 'Start audio']
      : r.state === 'starting' ? ['Connecting audio…', null]
      : r.state === 'failed' ? [`Audio connection failed: ${r.reason}. Retrying…`, 'Retry now']
      : ["Microphone blocked: you can listen, but stations can't hear you. Allow the mic in this site's settings.", null];
    setText(text, msg);
    btn.hidden = !label;
    if (label) setText(btn, label);
  });
  return { el, update() {} };
}

// --------------------------------------------------------------- 3.4 notices
export function notices() {
  const el = h('div', { style: { display: 'contents' } });
  return {
    el,
    update(st) {
      const html = st.unknownPeers.map(n =>
        `<div class="notice"><span class="dot"></span><div><b>${esc(n)}</b> joined the group without a role, so no audio is exchanged with it. <em>It's probably running stock SonoBus.</em></div></div>`).join('');
      setHTML(el, html);
    },
  };
}

// android.md §6: the phone speaker feeds the mic and the platform has no echo canceller.
// Not blocking, no button; the engine decides (platform.echoRisk), so it costs nothing
// when the echo canceller works. Sits at the top of the main area.
export function echoNotice() {
  const el = h('div', { style: { display: 'contents' } });
  return {
    el,
    update(st) {
      setHTML(el, echoNoticeShown(st)
        ? `<div class="notice" role="status"><span class="dot"></span><div><b>${ECHO_NOTICE.lead}</b>${ECHO_NOTICE.body}<em>${ECHO_NOTICE.hint}</em></div></div>`
        : '');
    },
  };
}

// --------------------------------------------------------------- 3.5 stations
export function stations(ctx) {
  const el = h('section.stations', { 'aria-label': 'Stations' });
  /** @type {Map<string, ReturnType<typeof stationCard>>} */ const cards = new Map();
  let placeholder = null;
  return {
    el,
    update(st, ui) {
      for (const [id, c] of cards) if (!st.stations[id]) { c.el.remove(); cards.delete(id); ctx.activity.detach(id); }
      const list = ordered(st);
      placeholder?.remove(); placeholder = null;
      if (!list.length) {
        placeholder = st.connection.state === 'connecting'
          ? h('div', { style: { display: 'contents' } }, [0, 1, 2].map(() => h('div.skel', 'Waiting for stations…')))
          : h('div.skel', st.connection.group
              ? h('span', 'No stations connected yet. VDIs appear here when their agent joins ', h('b', st.connection.group), '.')
              : h('span', 'No stations connected yet. Connect to a group in Settings; VDIs appear here when their agent joins it.'));
        el.append(placeholder);
      }
      list.forEach((s, i) => {
        let c = cards.get(s.id);
        if (!c) { c = stationCard(ctx, s.id); cards.set(s.id, c); }
        if (el.children[i] !== c.el) el.insertBefore(c.el, el.children[i] ?? null);
        c.update(st, s, ui);
      });
    },
  };
}

function stationCard(ctx, id) {
  const send = cmd => args => ctx.client.fire(cmd, { station: id, ...args });
  const sw = h('span.swatch'), name = h('span.sname'), badges = h('span.badges'), health = h('span.health');
  const details = h('div.hdetail.num', { hidden: true });
  health.setAttribute('role', 'button'); health.tabIndex = 0;
  const toggleDetails = () => { ctx.ui.details[id] = !ctx.ui.details[id]; ctx.render(); };
  health.addEventListener('click', toggleDetails);
  health.addEventListener('keydown', e => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); e.stopPropagation(); toggleDetails(); } });
  const issue = h('div.issue', { hidden: true });
  const canvas = h('canvas'), over = h('div.over');
  const activity = h('div.activity', canvas, over);
  ctx.activity.attach(id, canvas);

  const thumb = h('span.thumb');
  const panBtns = AUDIO.panSnaps.map(v => h('button', { role: 'radio', 'aria-label': panLabel(v), onclick: () => send('station.setPan')({ pan: v }) },
    v === -1 ? 'L' : v === 1 ? 'R' : v === 0 ? 'C' : '·'));
  const panGroup = h('div.pan', { role: 'radiogroup' }, thumb, panBtns);
  const panVal = h('span.val.num');

  const level = h('input', { type: 'range', min: AUDIO.levelMinDb, max: AUDIO.levelMaxDb, step: AUDIO.levelStepDb });
  const levelVal = h('span.val.num');
  const setLevel = latest(db => send('station.setLevel')({ db }));
  level.addEventListener('input', () => { setLevel(Number(level.value)); paintRange(level); setText(levelVal, dbText(Number(level.value))); });
  level.addEventListener('pointerdown', () => { level._active = true; });
  level.addEventListener('pointerup', () => { level._active = false; });
  level.addEventListener('dblclick', () => send('station.setLevel')({ db: 0 }));

  const mute = h('button.tog.mute', { 'aria-pressed': 'false' }, 'Mute ', h('kbd', 'M'));
  const solo = h('button.tog.solo', { 'aria-pressed': 'false' }, 'Solo ', h('kbd', 'S'));
  const talk = h('button.tog.talk');
  let cur = null;
  mute.onclick = () => send('station.setMute')({ on: !cur.mute });
  // P5.7: soloing makes this the only solo; ⌘-click (or Shift-click) adds to the solos.
  solo.onclick = e => send('station.setSolo')(cur.solo ? { on: false }
    : { on: true, exclusive: !(e.metaKey || e.ctrlKey || e.shiftKey) });
  talk.onclick = () => { if (talk.dataset.state !== 'held') send('station.setTalk')({ on: !cur.talk }); };

  const el = h('article.station', { onpointerdown: () => ctx.select(id) },
    h('div.shead', sw, name, badges, health),
    details, issue, activity,
    h('div.row', h('span.label', 'Place'), panGroup, panVal),
    h('div.row', h('span.label', 'Level'), level, levelVal),
    h('div.actions', mute, solo, talk));

  return {
    el,
    update(st, s, ui) {
      cur = s;
      const down = s.presence !== 'online';
      el.classList.toggle('sel', ui.selected === id);
      el.classList.toggle('down', down);
      sw.style.background = STATION_VAR(s.colorIndex);
      setText(name, s.name);
      setAttr(panGroup, 'aria-label', `Placement for ${s.name}`);
      setAttr(level, 'aria-label', `Level for ${s.name}`);

      // badges
      const dim = isDimmed(st, s);
      setHTML(badges, s.mute ? '<span class="badge muted">MUTED</span>'
        : dim ? `<span class="badge">DIMMED ${dbText(st.settings.soloDimDb).replace(' dB', '').replace('.0', '')}</span>` : '');

      // health line (spec §3.5 table)
      let hHtml, tip;
      if (s.presence === 'offline') {
        hHtml = `<span class="dot"></span>Offline <span class="num">last seen ${lastSeenText(s.lastSeen)}</span>`;
        tip = "A station this Console knows, but not connected now. Its settings are remembered.";
      } else if (s.presence === 'lost') {
        hHtml = `<span class="dot crit pulse"></span>Lost <span class="num">reconnecting ${s.lostForSec ?? 0}s</span>`;
        tip = 'No audio from this station. Retrying automatically.';
      } else if (s.health === 'unstable') {
        hHtml = `<span class="dot warn"></span>Unstable <span class="num">${s.latencyMs} ms · ${s.lossPct}% loss</span>`;
        tip = 'Audio may break up. The network buffer is growing to compensate.';
      } else {
        hHtml = `<span class="dot good"></span>Clear <span class="num">${s.latencyMs} ms</span>`;
        tip = `Latency ${s.latencyMs} ms · loss ${s.lossPct}% · buffer ${s.jitterBufferMs} ms`;
      }
      setHTML(health, hHtml); setAttr(health, 'title', tip);
      setAttr(health, 'aria-expanded', !!ui.details[id]);
      setAttr(health, 'aria-label', `${s.name} link details`);
      details.hidden = !ui.details[id];
      if (ui.details[id]) setText(details, tip);

      // VDI self-report (P1.7)
      const msg = agentIssue(s.agent);
      issue.hidden = !msg;
      if (msg) setHTML(issue, `<b>VDI reports:</b> ${esc(msg)}`);

      // activity look + overlay
      ctx.activity.setLook(id, { color: s.colorIndex, grey: down, alpha: down ? 0.5 : s.mute ? 0.18 : dim ? 0.4 : 0.9 });
      setText(over, s.presence === 'offline' ? 'Not connected' : s.presence === 'lost' ? 'No audio' : s.mute ? 'Muted' : '');

      // place
      const pan = ui.dragPan?.id === id ? ui.dragPan.pan : s.pan;
      const nearest = AUDIO.panSnaps.reduce((a, b) => (Math.abs(b - pan) < Math.abs(a - pan) ? b : a));
      panBtns.forEach((b, i) => setAttr(b, 'aria-checked', AUDIO.panSnaps[i] === nearest));
      thumb.style.left = `calc(${AUDIO.panSnaps.indexOf(nearest) * 20}% + 3px)`;
      setText(panVal, panText(pan));

      // level (don't fight the user's drag)
      if (!level._active && document.activeElement !== level) { level.value = String(s.level); setText(levelVal, dbText(s.level)); }
      paintRange(level);

      setAttr(mute, 'aria-pressed', s.mute);
      setAttr(solo, 'aria-pressed', s.solo);
      setAttr(solo, 'title', `Solo this station: dims the others by ${Math.abs(st.settings.soloDimDb)} dB, and only soloed stations hear your mic. ⌘-click or Shift+S adds it to the stations already soloed`);

      // talk (spec §3.5 talk table)
      const held = s.talk && soloHeld(st, s);
      const state = held ? 'held' : !s.talk ? 'off' : s.hearsYou ? 'live' : 'will';
      talk.dataset.state = state;
      setAttr(talk, 'aria-pressed', s.talk && !held);
      setAttr(talk, 'aria-disabled', held ? 'true' : null);
      setAttr(talk, 'title', held ? 'Another station is soloed, so only it hears you. Un-solo to restore.'
        : s.talk ? 'Click so this station stops hearing your mic' : 'Click so this station hears your mic');
      const label = { held: 'Paused by solo', off: "Doesn't hear", live: 'Hears you', will: 'Will hear you' }[state];
      setHTML(talk, (state === 'held' || state === 'off' ? ICONS.micOff : ICONS.mic) + esc(label) + (state === 'held' ? '' : ' <kbd>T</kbd>'));
    },
  };
}

function agentIssue(a) {
  if (!a) return null;
  if (a.configError) return `Config problem: ${a.configError}`;
  if (a.input === 'missing') return `Input device "${a.inputNode}" isn't on the VDI. No system audio is being sent.`;
  if (a.input === 'silent') return `Input silent for ${a.silentForMin} min. The loopback device on the VDI may be wrong or the VDI is muted.`;
  if (a.output === 'missing') return `Virtual mic "${a.outputNode}" isn't on the VDI. Apps there can't hear you.`;
  if (a.paused) return 'Sending is paused on the VDI.';
  return null;
}

// --------------------------------------------------------------- 3.6 you bar
export function youBar(ctx) {
  const mic = h('button.mic');
  const meterFill = h('i'); const meter = h('div.meter.micmeter', { 'aria-hidden': 'true' }, meterFill);
  const modeOpen = h('button', { onclick: () => ctx.client.fire('mic.setMode', { mode: 'open' }) }, 'Open mic');
  const modePtt = h('button', { onclick: () => ctx.client.fire('mic.setMode', { mode: 'ptt' }) }, 'Push to talk');
  const targets = h('span.targets');
  const allTalk = h('button.btn.small', { onclick: () => ctx.client.fire('stations.talkToAll') }, 'Talk to all');
  const out = h('input', { type: 'range', min: AUDIO.levelMinDb, max: AUDIO.levelMaxDb, step: AUDIO.levelStepDb, 'aria-label': 'Output level' });
  const outVal = h('span.val.num');
  const setOut = latest(db => ctx.client.fire('output.setLevel', { db }));
  out.addEventListener('input', () => { setOut(Number(out.value)); paintRange(out); setText(outVal, dbText(Number(out.value))); });
  out.addEventListener('pointerdown', () => { out._active = true; });
  out.addEventListener('pointerup', () => { out._active = false; });

  let st = null;
  mic.addEventListener('click', () => {
    if (!st) return;
    if (micBlocked(st)) { ctx.client.fire('platform.requestMic'); return; }   // android.md section 3, in both modes
    if (st.mic.mode === 'open') ctx.client.fire('mic.setOn', { on: !st.mic.on });
  });
  mic.addEventListener('pointerdown', () => { if (st?.mic.mode === 'ptt' && !micBlocked(st)) ctx.ptt(true); });
  for (const ev of ['pointerup', 'pointerleave', 'pointercancel']) mic.addEventListener(ev, () => ctx.ptt(false));
  mic.addEventListener('contextmenu', e => e.preventDefault());

  ctx.client.onMeters(m => {
    if (!m.mic) return;
    const v = m.mic[0] === null || (st && micBlocked(st)) ? 0 : Math.max(0, Math.min(1, (m.mic[0] + 60) / 60));
    meterFill.style.width = `${v * 100}%`;
  });

  const el = h('footer.you', { 'aria-label': 'Your microphone and output' },
    mic, meter, h('div.seg', { role: 'group', 'aria-label': 'Talk mode' }, modeOpen, modePtt), targets, allTalk,
    h('span.spacer'),
    h('div.master', h('span.label', 'Output'), out, outVal),
    h('div.hint', 'Keys: ', h('kbd', '1'), '–', h('kbd', '4'), ' pick station · ', h('kbd', '←'), h('kbd', '→'), ' place · ',
      h('kbd', 'M'), ' mute · ', h('kbd', 'S'), ' solo (', h('kbd', '⇧S'), ' add) · ', h('kbd', 'T'), ' hears you · ', h('kbd', '`'), ' mic on/off · hold ',
      h('kbd', 'Space'), ' to talk (push-to-talk mode)'));

  return {
    el,
    update(state) {
      st = state;
      const all = ordered(st);
      const tg = all.filter(s => s.hearsYou);
      const tx = st.mic.transmitting;
      mic.dataset.state = tx && tg.length ? 'hot' : tx ? 'open-idle' : 'idle';
      setAttr(mic, 'aria-pressed', tx);
      const [icon, label, sub] = st.mic.mode === 'open'
        ? (st.mic.on ? [ICONS.mic, 'Mic live', tapCopy(st, 'Tap to turn off', 'Click to turn off')]
                     : [ICONS.micOff, 'Mic off', tapCopy(st, 'Tap to open mic', 'Click to open mic')])
        : (tx ? [ICONS.mic, 'Talking…', 'Release to stop']
              : [ICONS.micOff, 'Hold to talk', tapCopy(st, 'Press and hold', 'or hold Space')]);
      const call = callMicButton(st);     // A14: disabled during a phone call
      const blocked = blockedMicButton(st);   // A9: the mic is not granted
      if (blocked) {
        mic.dataset.state = 'idle';
        setAttr(mic, 'aria-pressed', false);
        setHTML(mic, `${ICONS.micOff}<span>${blocked.label}<span class="sub">${blocked.sub}</span></span>`);
      } else if (call) {
        mic.dataset.state = 'idle';
        setAttr(mic, 'aria-pressed', false);
        setHTML(mic, `${ICONS.micOff}<span>${call.label}<span class="sub">${call.sub}</span></span>`);
      } else setHTML(mic, `${icon}<span>${label}<span class="sub">${sub}</span></span>`);
      mic.disabled = !!call;
      meter.classList.toggle('hot', tx && tg.length > 0);
      setAttr(modeOpen, 'aria-pressed', st.mic.mode === 'open');
      setAttr(modePtt, 'aria-pressed', st.mic.mode === 'ptt');
      const n = st.stationOrder.length;
      setHTML(targets, !n ? '' : tg.length === all.length && all.length ? 'Talking to <b>all stations</b>'
        : !tg.length ? 'Talking to <b>nobody</b>' : `Talking to <b>${esc(listNames(tg.map(s => shortName(s.name))))}</b>`);
      allTalk.hidden = !ordered(st).some(s => !s.talk);
      if (!out._active && document.activeElement !== out) { out.value = String(st.output.level); setText(outVal, dbText(st.output.level)); }
      paintRange(out);
    },
  };
}

// --------------------------------------------------------------- 3.7 settings
export function settingsSheet(ctx) {
  // Built once; values are refreshed from state only when a field isn't being
  // edited, so typing survives incoming patches (spec §3.7).
  const field = (label, control, note) => h('label.field2', h('span', label), control, note ?? null);
  const input = (type, attrs = {}) => h('input', { type, autocomplete: 'off', spellcheck: 'false', ...attrs });
  const server = input('text', { placeholder: 'host:port' });
  const group = input('text');
  const password = input('password', { placeholder: 'Saved' });
  const name = input('text');
  const connErr = h('div.connerr', { role: 'alert', hidden: true });
  const connectBtn = h('button.btn.primary', { type: 'submit' });
  const form = h('form.connform', { onsubmit: e => { e.preventDefault(); submit(); } },
    field('Server', server), field('Group', group), field('Password', password, h('small.pwnote')),
    field('Your name', name), connErr, h('div.formrow', connectBtn));

  const micSel = h('select', { onchange: () => ctx.client.fire('devices.setInput', { id: micSel.value }) });
  const outSel = h('select', { onchange: () => ctx.client.fire('devices.setOutput', { id: outSel.value }) });
  const dimSel = h('select', { onchange: () => ctx.client.fire('settings.set', { soloDimDb: Number(dimSel.value) }) },
    [-12, -18, -24, -30].map(v => h('option', { value: v }, `−${Math.abs(v)} dB`)));
  const ptt = h('div.num.readonly'); const pttField = field('Push-to-talk key (works in other apps)', ptt);
  const codec = h('div'), buffer = h('div');
  const remembered = h('div.remembered'); const rememberedField = field('Remembered stations', remembered);

  const closeBtn = h('button.iconbtn', { 'aria-label': 'Close settings', onclick: () => ctx.closeSettings() }, '✕');
  const scrim = h('div.scrim', { onclick: () => ctx.closeSettings() });
  const problems = h('div.problems');   // android.md section 7: only when something needs fixing
  const sheet = h('aside.sheet', { role: 'dialog', 'aria-label': 'Settings', 'aria-modal': 'true' },
    h('div.head', h('b', 'Settings'), h('span.spacer'), closeBtn),
    problems,
    h('span.label', 'Connection'), form,
    h('span.label', 'Audio'),
    field('Microphone', micSel), field('Output', outSel), field('Solo dims others by', dimSel), pttField,
    h('details', h('summary', 'Advanced'), h('div.adv', field('Codec', codec), field('Network buffer', buffer))),
    rememberedField);
  const el = h('div', { style: { display: 'contents' } }, scrim, sheet);

  let st = null, wasOpen = false;
  function submit() {
    const c = st.connection;
    if (c.state === 'connected') { ctx.client.fire('connection.disconnect'); return; }
    const args = { server: server.value.trim(), group: group.value.trim(), name: name.value.trim() };
    if (password.value) args.password = password.value;
    ctx.client.cmd('connection.connect', args).then(() => { password.value = ''; }).catch(e => {
      connErr.hidden = false; setText(connErr, e.message || "Couldn't connect.");
    });
  }
  const editing = el2 => document.activeElement === el2;
  const setVal = (el2, v) => { if (!editing(el2) && el2.value !== v) el2.value = v; };
  const options = (sel, list, cur) => {
    const html = list.map(d => `<option value="${esc(d.id)}"${d.name === cur ? ' selected' : ''}>${esc(d.name)}</option>`).join('');
    if (sel._html !== html) { sel.innerHTML = html; sel._html = html; }
  };

  return {
    el,
    update(state, ui) {
      st = state;
      scrim.hidden = sheet.hidden = !ui.settingsOpen;
      if (ui.settingsOpen && !wasOpen) queueMicrotask(() => (st.connection.state === 'connected' ? closeBtn : server).focus());
      if (!ui.settingsOpen && wasOpen) ctx.focusSettingsButton();
      wasOpen = ui.settingsOpen;
      if (!ui.settingsOpen) return;
      const c = st.connection, s = st.settings;
      const rows = problemRows(st);
      const key = JSON.stringify(rows.map(r => r.id));
      if (problems._key !== key) {
        problems._key = key;
        problems.replaceChildren(...rows.map(r => h('div.problem', { role: 'group', 'aria-label': 'Problem' },
          h('span.warncircle', { 'aria-hidden': 'true' }, '!'),
          h('div', h('p', r.text),
            h('button.btn', { type: 'button', onclick: () => ctx.client.fire(r.command, r.args) }, r.action)))));
      }
      problems.hidden = !rows.length;
      const connected = c.state === 'connected';
      for (const f of [server, group, password, name]) f.disabled = connected;
      setVal(server, c.server ?? ''); setVal(group, c.group ?? ''); setVal(name, st.self.name ?? '');
      setText(form.querySelector('.pwnote'), c.passwordSaved ? 'A password is saved. Leave empty to keep it.' : 'No password saved.');
      setText(connectBtn, connected ? 'Disconnect' : c.state === 'connecting' || c.state === 'reconnecting' ? 'Connecting…' : 'Connect');
      connectBtn.classList.toggle('primary', !connected);
      connErr.hidden = !(c.state === 'failed' && c.reason);
      if (c.state === 'failed' && c.reason) setText(connErr, c.reason);
      options(micSel, st.devices.inputs, st.mic.device);
      options(outSel, st.devices.outputs, st.output.device);
      setVal(dimSel, String(s.soloDimDb));
      pttField.hidden = !s.pttHotkey; setText(ptt, s.pttHotkey ?? '');
      setText(codec, `${s.codec === 'opus' ? 'Opus' : s.codec} · ${s.bitrateKbps} kbps`);
      setHTML(buffer, `${s.networkBuffer.mode === 'auto' ? 'Auto' : 'Fixed'} (currently <span class="num">${s.networkBuffer.currentMs} ms</span>)`);
      const offline = ordered(st).filter(x => x.presence === 'offline');
      rememberedField.hidden = !offline.length;
      setHTML(remembered, offline.map(o => `<div class="r"><span class="swatch" style="background:${STATION_VAR(o.colorIndex)}"></span>${esc(o.name)}<span class="spacer"></span><button class="btn small" type="button" data-forget="${esc(o.id)}">Forget</button></div>`).join(''));
      remembered.querySelectorAll('[data-forget]').forEach(b => { b.onclick = () => ctx.client.fire('station.forget', { station: b.dataset.forget }); });
    },
  };
}

// --------------------------------------------------------------- helpers
export function paintRange(input) {
  const min = Number(input.min), max = Number(input.max);
  input.style.setProperty('--p', `${((Number(input.value) - min) / (max - min)) * 100}%`);
}

export function esc(s) {
  return String(s ?? '').replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

export { ordered };
