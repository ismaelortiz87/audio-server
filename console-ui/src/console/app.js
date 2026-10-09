// Console entry point: layout (spec §2), UI-only state (selection, drag,
// settings), keyboard map (spec §4.2) and push-to-talk sources.

import { boot } from '../lib/boot.js';
import { h } from '../lib/dom.js';
import { AUDIO } from '../lib/tokens.js';
import { ActivityHub } from './activity.js';
import { topBar, liveBand, placement, notices, stations, youBar, settingsSheet, ordered } from './components.js';

boot('console', client => {
  const ui = { selected: null, dragPan: null, settingsOpen: false };
  let state = null;
  let pttDown = false;

  const ctx = {
    client,
    ui,
    activity: new ActivityHub(client),
    render: () => state && render(state),
    select(id) { if (ui.selected !== id) { ui.selected = id; ctx.render(); } },
    openSettings() { ui.settingsOpen = true; ctx.render(); },
    closeSettings() { ui.settingsOpen = false; ctx.render(); },
    nudge(id, dir) {
      const s = state.stations[id]; if (!s) return;
      const next = dir > 0 ? AUDIO.panSnaps.find(x => x > s.pan + 1e-6) ?? 1 : [...AUDIO.panSnaps].reverse().find(x => x < s.pan - 1e-6) ?? -1;
      client.fire('station.setPan', { station: id, pan: next });
    },
    /** One logical PTT hold per page, from the mic button or Space. */
    ptt(down) {
      if (down === pttDown) return;
      if (down && state?.mic.mode !== 'ptt') return;
      pttDown = down;
      client.fire('mic.ptt', { down });
    },
  };

  const parts = {
    top: topBar(ctx), band: liveBand(), stage: placement(ctx), notices: notices(), stations: stations(ctx),
    you: youBar(ctx), settings: settingsSheet(ctx),
  };
  const app = h('div.app',
    parts.top.el, parts.band.el,
    h('main', parts.stage.el, parts.notices.el, parts.stations.el),
    parts.you.el, parts.settings.el);
  document.body.append(app);

  function render(st) {
    const ids = st.stationOrder.filter(id => st.stations[id]);
    if (!ids.includes(ui.selected)) ui.selected = ids[0] ?? null;
    for (const p of Object.values(parts)) p.update(st, ui);
  }

  client.store.subscribe(st => { state = st; render(st); });
  client.subscribe('meters');
  addEventListener('resize', () => ctx.render());

  // spec §4.2 keyboard map
  addEventListener('keydown', e => {
    if (!state || e.metaKey || e.ctrlKey || e.altKey) return;
    if (e.target instanceof HTMLElement && e.target.matches('input, select, textarea')) return;
    if (e.key === 'Escape' && ui.settingsOpen) { ctx.closeSettings(); return; }
    if (e.key === ' ' && state.mic.mode === 'ptt') { e.preventDefault(); if (!e.repeat) ctx.ptt(true); return; }
    if (e.key === '`') { if (state.mic.mode === 'open') client.fire('mic.setOn', { on: !state.mic.on }); return; }
    const list = ordered(state);
    if (/^[1-4]$/.test(e.key) && list[Number(e.key) - 1]) { ctx.select(list[Number(e.key) - 1].id); return; }
    const s = state.stations[ui.selected];
    if (!s) return;
    const k = e.key.toLowerCase();
    const anySolo = list.some(x => x.solo);
    if (k === 'm') client.fire('station.setMute', { station: s.id, on: !s.mute });
    else if (k === 's') client.fire('station.setSolo', { station: s.id, on: !s.solo });
    else if (k === 't') { if (!(anySolo && !s.solo)) client.fire('station.setTalk', { station: s.id, on: !s.talk }); }
    else if (e.key === 'ArrowLeft' || e.key === 'ArrowRight') {
      if (e.target instanceof HTMLElement && e.target.closest('.pan, .puck')) return;
      e.preventDefault(); ctx.nudge(s.id, e.key === 'ArrowLeft' ? -1 : 1);
    }
  });
  addEventListener('keyup', e => { if (e.key === ' ') ctx.ptt(false); });
  // Losing focus while holding Space must not leave the mic on.
  addEventListener('blur', () => ctx.ptt(false));
});
