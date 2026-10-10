// Agent localhost UI (spec §6): status, who's listening, input/output nodes
// with live levels and test tones, pause, reload. Runs against the agent's
// own engine (role "vdi").

import { boot } from '../lib/boot.js';
import { h, setText, setHTML, setAttr } from '../lib/dom.js';
import { meterDbText, dbToUnit } from '../lib/format.js';
import { esc } from '../console/components.js';

boot('vdi', client => {
  const ui = { open: null, toast: null, tone: null };
  let st = null;

  const name = h('span.name');
  const stateBox = h('div.state', { role: 'status', 'aria-live': 'polite' });
  const listeners = h('div.sect', h('span.label', 'Listening now'), h('div'));
  const devIn = h('div.dev'), devOut = h('div.dev');
  const toast = h('div.toast', { hidden: true });
  const pause = h('button.btn', { 'aria-pressed': 'false', onclick: () => client.fire(st.sending === 'paused' ? 'agent.resume' : 'agent.pause') });
  const reload = h('button.btn', { onclick: () => { ui.toast = null; client.fire('agent.reloadConfig'); } }, 'Reload config');
  const path = h('span.path.num');

  document.body.append(h('div.agent',
    h('div.ident', h('span.swatch'), name, h('span.role', 'Crosspoint agent')),
    stateBox, listeners,
    h('div.sect', h('span.label', 'Audio'), devIn, devOut, toast),
    h('div.foot', pause, reload, path)));

  // live levels: one meters subscription; per-node levels only while the input picker is open
  const levels = { input: null, output: null, devices: {} };
  client.subscribe('meters');
  client.onMeters(m => {
    levels.input = m.input?.[0] ?? null;
    levels.output = m.output?.[0] ?? null;
    if (m.devices) levels.devices = Object.fromEntries(Object.entries(m.devices).map(([k, v]) => [k, v[0]]));
    paintMeters();
  });

  function paintMeters() {
    const set = (sel, db) => { const i = document.querySelector(sel); if (i) i.style.width = `${dbToUnit(db) * 100}%`; };
    set('#m-in > i', levels.input);
    set('#m-out > i', levels.output);
    const r = document.querySelector('#db-in'); if (r) setText(r, meterDbText(levels.input));
    for (const [node, db] of Object.entries(levels.devices)) set(`[data-meter="${CSS.escape(node)}"] > i`, db);
  }

  // Secondary line: only what the name doesn't already say (spec §6). On macOS
  // the node id is the device name and there's no hint, so the line is omitted.
  function hintLine(d) {
    const parts = [d.node !== d.description ? d.node : null, d.hint || null].filter(Boolean);
    return parts.length ? `<span class="hint">${esc(parts.join(' · '))}</span>` : '';
  }

  function stateInfo() {
    const c = st.connection;
    if (c.state === 'reconnecting') return { cls: 'warn', dot: 'warn pulse', big: 'Reconnecting…',
      sub: "Can't reach the connection server. Consoles can't hear this VDI until it's back.",
      meta: `${esc(c.server)} · attempt ${c.attempt} · next try in ${c.retryInSec}s`, acts: '<button class="btn small" data-act="retry">Retry now</button>' };
    if (c.state === 'error') {
      const pw = c.error?.code === 'bad_password';
      return { cls: 'crit', dot: 'crit', big: 'Needs attention', sub: esc(c.error?.message ?? 'Something went wrong.'),
        meta: pw ? `Fix <code>password:</code> in ${esc(st.configPath)}, then reload. Nothing is sent until then.` : esc(st.configError ?? ''),
        acts: '<button class="btn small" data-act="reload">Reload config</button>' };
    }
    if (st.input.status === 'missing') return { cls: 'crit', dot: 'crit', big: 'Needs attention',
      sub: `The input node in vdi.yaml, "${esc(st.input.node)}", isn't on this machine.`,
      meta: "Pick the device that carries this VDI's system audio below. Your choice is saved to vdi.yaml.", acts: '' };
    if (st.sending === 'paused') return { cls: 'warn', dot: 'warn', big: 'Sending paused',
      sub: 'Consoles hear silence from this VDI. You can still be heard through the virtual mic.', meta: 'Resumes on "Resume sending".', acts: '' };
    if (!st.consoles.length) return { cls: '', dot: '', big: 'Waiting for a Console',
      sub: 'Connected and ready. Audio starts the moment a Console joins.', meta: `Group ${esc(c.group)} · ${esc(c.server)}`, acts: '' };
    return { cls: '', dot: 'good', big: 'Connected',
      sub: `Sending this VDI's audio to ${st.consoles.length === 1 ? '1 Console' : st.consoles.length + ' Consoles'}.`,
      meta: `Group ${esc(c.group)} · ${esc(c.server)} · mono`, acts: '' };
  }

  async function choose(kind, node) {
    ui.open = null;
    // Undo back to a device that isn't there would just fail: don't offer it.
    const prevBroken = (kind === 'in' ? st.input : st.output).status === 'missing';
    try {
      const { previous } = await client.cmd(kind === 'in' ? 'agent.setInput' : 'agent.setOutput', { node });
      ui.toast = { kind, previous: prevBroken ? null : previous };
    } catch (e) {
      ui.toast = { error: e.message };
    }
    render();
  }

  function tone(node) {
    ui.tone = node; render();
    client.cmd('agent.testTone', { node }).catch(() => {});
    setTimeout(() => { ui.tone = null; render(); }, 1500);
  }

  function render() {
    // The engine's API can answer before the engine registers its state, so the
    // first snapshot may be the placeholder ({ self } only). Wait for the full one.
    if (!st || !st.connection || !st.input || !st.output) return;
    setText(name, st.self.name);
    document.title = `${st.self.name} · Crosspoint agent`;
    const info = stateInfo();
    stateBox.className = `state ${info.cls}`;
    setHTML(stateBox, `<span class="dot ${info.dot}"></span><div><div class="big">${info.big}</div><div class="sub">${info.sub}</div>
      <div class="meta">${info.meta}</div>${info.acts ? `<div class="acts">${info.acts}</div>` : ''}</div>`);
    stateBox.querySelector('[data-act="retry"]')?.addEventListener('click', () => client.fire('agent.retryNow'));
    stateBox.querySelector('[data-act="reload"]')?.addEventListener('click', () => client.fire('agent.reloadConfig'));

    const offline = st.connection.state !== 'connected';
    listeners.hidden = offline;
    setHTML(listeners.lastChild, st.consoles.length ? st.consoles.map(c => `
      <div class="lrow ${c.talking ? 'talking' : ''}">${c.talking ? '<span class="pill-talk">TALKING</span>' : '<span class="dot good"></span>'}
        <span class="who">${esc(c.name)}</span><span class="kind">Console · ${c.kind === 'mac' ? 'Mac' : c.kind === 'web' ? 'web' : 'other'}</span>
        <span class="lat num">${c.latencyMs} ms</span></div>`).join('') : '<div class="empty">No Console connected right now.</div>');

    // Sends (input)
    const missing = st.input.status === 'missing';
    const sending = st.sending === 'active' && !offline;
    const inFoot = st.sending === 'paused' ? 'Paused: local level only, Consoles hear silence'
      : offline ? 'Not sending: local level only' : !st.consoles.length ? 'Local level: sending starts when a Console joins' : 'Mono to Consoles';
    devIn.className = `dev${missing ? ' bad' : ''}`;
    const inOpen = ui.open === 'in' || missing;
    setHTML(devIn, `
      <div class="devhead"><span class="what">Sends</span><span class="why">system audio → Consoles</span></div>
      <button class="devsel" data-toggle="in" aria-expanded="${inOpen}"><span class="dn ${missing ? 'missing' : ''}">${esc(missing ? st.input.node : st.input.description)}</span><span class="chev">${inOpen ? '▴' : 'Change ▾'}</span></button>
      ${missing ? '' : `<div class="meter send ${sending ? '' : 'local'}" id="m-in" title="Coloured when the audio is being sent; grey when it's only the local level"><i></i></div>
      <div class="devfoot"><span>${inFoot}</span><span class="num" id="db-in"></span></div>`}
      ${inOpen ? `<div class="picker" role="radiogroup" aria-label="Input device">${st.devices.inputs.map(d => `
        <button class="opt" role="radio" aria-checked="${d.node === st.input.node && !missing}" data-in="${esc(d.node)}"><span class="radio"></span>
          <span class="on">${esc(d.description)}${hintLine(d)}</span>
          <span class="meter" data-meter="${esc(d.node)}"><i></i></span></button>`).join('')}
        <div class="devfoot">Live level per device: pick the one that moves when the VDI plays sound.</div></div>` : ''}`);

    // Plays your voice into (output)
    const talking = st.consoles.find(c => c.talking);
    const outOpen = ui.open === 'out';
    devOut.className = `dev${st.output.status === 'missing' ? ' bad' : ''}`;
    setHTML(devOut, `
      <div class="devhead"><span class="what">Plays your voice into</span><span class="why">virtual mic for apps on this VDI</span></div>
      <button class="devsel" data-toggle="out" aria-expanded="${outOpen}"><span class="dn ${st.output.status === 'missing' ? 'missing' : ''}">${esc(st.output.description)}</span><span class="chev">${outOpen ? '▴' : 'Change ▾'}</span></button>
      <div class="meter voice" id="m-out"><i></i></div>
      <div class="devfoot"><span>${talking ? `${esc(talking.name)} is talking` : 'Silent: no Console is talking'}</span><span class="spacer"></span>
        <button class="btn small" data-tone="${esc(st.output.node)}">${ui.tone === st.output.node ? 'Playing…' : 'Play test tone'}</button></div>
      ${outOpen ? `<div class="picker" role="radiogroup" aria-label="Output device">${st.devices.outputs.map(d => `
        <div class="opt" role="radio" tabindex="0" aria-checked="${d.node === st.output.node}" data-out="${esc(d.node)}"><span class="radio"></span>
          <span class="on">${esc(d.description)}${hintLine(d)}</span>
          <button class="btn small tone" data-tone="${esc(d.node)}">${ui.tone === d.node ? 'Playing…' : 'Test tone'}</button></div>`).join('')}
        <div class="devfoot">Outputs can't show a level. Play a tone and check it arrives in the VDI app's mic input.</div></div>` : ''}`);

    for (const b of document.querySelectorAll('[data-toggle]')) b.onclick = () => { ui.open = ui.open === b.dataset.toggle ? null : b.dataset.toggle; syncDeviceMeters(); render(); };
    for (const b of document.querySelectorAll('[data-in]')) b.onclick = () => choose('in', b.dataset.in);
    for (const b of document.querySelectorAll('[data-out]')) {
      b.onclick = e => { if (!(e.target instanceof HTMLElement && e.target.closest('[data-tone]'))) choose('out', b.dataset.out); };
      b.onkeydown = e => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); choose('out', b.dataset.out); } };
    }
    for (const b of document.querySelectorAll('[data-tone]')) b.onclick = e => { e.stopPropagation(); tone(b.dataset.tone); };
    syncDeviceMeters();

    // toast with Undo (spec §6)
    toast.hidden = !ui.toast;
    if (ui.toast?.error) setHTML(toast, `<span>Couldn't switch: ${esc(ui.toast.error)}</span>`);
    else if (ui.toast) {
      setHTML(toast, `<span>✓ Saved to vdi.yaml</span>${ui.toast.previous ? '<button data-undo>Undo</button>' : ''}`);
      const undo = toast.querySelector('[data-undo]');
      if (undo) undo.onclick = () => {
        const { kind, previous } = ui.toast; ui.toast = null;
        client.fire(kind === 'in' ? 'agent.setInput' : 'agent.setOutput', { node: previous });
        render();
      };
    }

    setAttr(pause, 'aria-pressed', st.sending === 'paused');
    setText(pause, st.sending === 'paused' ? 'Resume sending' : 'Pause sending');
    pause.disabled = offline;
    // LRM marks keep the path's slashes in place under the rtl truncation trick.
    setText(path, '\u200E' + st.configPath + '\u200E');
    setAttr(path, 'title', st.configPath);
    paintMeters();
  }

  let deviceMeters = false;
  function syncDeviceMeters() {
    const want = ui.open === 'in' || st?.input.status === 'missing';
    if (want === deviceMeters) return;
    deviceMeters = want;
    if (want) client.subscribe('deviceMeters'); else client.unsubscribe('deviceMeters');
  }

  client.store.subscribe(s => { st = s; render(); });
});
