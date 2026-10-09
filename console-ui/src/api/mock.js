// In-page mock engine. It speaks docs/control-api.md exactly (hello, state,
// patch, meters, cmd/ack) and is the executable reference for that document:
// change both in the same PR. Used by the UI with `?mock=<scenario>` and by
// the tests in console-ui/test.

import { diff, clone } from '../lib/patch.js';
import { CONSOLE_SCENARIOS, AGENT_SCENARIOS, AGENT_INPUT_ACTIVITY } from './scenarios.js';

const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));

export class MockEngine {
  /**
   * @param {'console'|'vdi'} role
   * @param {string} scenario
   * @param {{ meters?: boolean, timers?: boolean, auth?: 'none'|'token', token?: string }} opts
   *   timers=false: no intervals at all (tests drive `tick()` / `secondTick()` by hand)
   */
  constructor(role, scenario, opts = {}) {
    this.role = role;
    this.opts = { meters: true, timers: true, auth: 'none', token: 'secret', ...opts };
    const table = role === 'console' ? CONSOLE_SCENARIOS : AGENT_SCENARIOS;
    const make = table[scenario] ?? Object.values(table)[0];
    this.state = make();
    this.activity = {};
    if (role === 'console') {
      for (const s of Object.values(this.state.stations)) {
        this.activity[s.id] = { target: s._activity, env: 0, burst: 0 };
        delete s._activity;
      }
    } else {
      for (const [node, a] of Object.entries(AGENT_INPUT_ACTIVITY)) this.activity[node] = { target: a, env: 0, burst: 0 };
      this.voice = { env: 0, burst: 0 };
    }
    this.rev = 1;
    /** @type {Set<Conn>} */ this.conns = new Set();
    this.derive();
    this.timers = [];
    if (this.opts.timers) {
      this.timers.push(setInterval(() => this.secondTick(), 1000));
      if (this.opts.meters) this.timers.push(setInterval(() => this.tick(), 33));
    }
  }

  dispose() {
    this.timers.forEach(clearInterval);
    for (const c of this.conns) c.close(1001);
  }

  /** Open a client connection: returns a Transport (see client.js). */
  connect() {
    const conn = new Conn(this);
    this.conns.add(conn);
    queueMicrotask(() => {
      conn.deliver({ t: 'hello', v: 1, app: 'Crosspoint', version: '1.7.2-mock', role: this.role,
        auth: this.opts.auth, features: ['meters', 'ptt', 'mock'] });
      if (this.opts.auth === 'none') { conn.authed = true; conn.deliver({ t: 'state', rev: this.rev, state: clone(this.state) }); }
    });
    return conn.transport;
  }

  // -------------------------------------------------------------- derived

  /** Engine-computed fields (api §3.2): mic.transmitting, pttHeld, hearsYou. */
  derive() {
    if (this.role !== 'console') return;
    const st = this.state;
    const held = [...this.conns].some(c => c.pttHeld);
    st.mic.pttHeld = st.mic.mode === 'ptt' && held;
    st.mic.transmitting = st.mic.mode === 'open' ? st.mic.on : st.mic.pttHeld;
    const anySolo = Object.values(st.stations).some(s => s.solo);
    for (const s of Object.values(st.stations)) {
      s.hearsYou = s.talk && s.presence === 'online' && st.mic.transmitting && !(anySolo && !s.solo);
    }
  }

  /** Run a mutation, then broadcast the resulting patch. */
  change(fn) {
    const before = clone(this.state);
    fn(this.state);
    this.derive();
    const ops = diff(before, this.state);
    if (!ops.length) return;
    this.rev++;
    for (const c of this.conns) if (c.authed) c.deliver({ t: 'patch', rev: this.rev, ops });
  }

  // -------------------------------------------------------------- timers

  /** Once a second: counters that move on their own. */
  secondTick() {
    this.change(st => {
      if (this.role === 'console') {
        for (const s of Object.values(st.stations)) if (s.presence === 'lost') s.lostForSec++;
      } else if (st.connection.state === 'reconnecting') {
        if (st.connection.retryInSec <= 1) { st.connection.attempt++; st.connection.retryInSec = 16; }
        else st.connection.retryInSec--;
      }
    });
  }

  /** ~30 fps: meters frame for subscribed clients. */
  tick() {
    const subs = [...this.conns].filter(c => c.authed && c.topics.has('meters') && !c.hidden);
    if (!subs.length) return;
    const msg = this.role === 'console' ? this.consoleMeters() : this.agentMeters();
    for (const c of subs) {
      if (this.role === 'vdi' && !c.topics.has('deviceMeters')) {
        const { devices, ...rest } = msg;
        c.deliver(rest);
      } else c.deliver(msg);
    }
  }

  consoleMeters() {
    const stations = {};
    for (const s of Object.values(this.state.stations)) {
      if (s.presence !== 'online') continue;
      const a = this.activity[s.id];
      stations[s.id] = toDb(step(a, s.health === 'unstable'));
    }
    const tx = this.state.mic.transmitting;
    const mic = toDb(Math.random() < 0.1 ? 0.1 : (tx ? 0.35 : 0.2) + Math.random() * 0.4);
    const out = toDb(Math.max(0, ...Object.values(stations).map(v => (v[0] === null ? 0 : (v[0] + 60) / 60))));
    return { t: 'meters', ts: Date.now(), stations, mic, output: out };
  }

  agentMeters() {
    const st = this.state;
    const devices = {};
    for (const [node, a] of Object.entries(this.activity)) devices[node] = toDb(step(a));
    const input = st.input.status === 'missing' ? [null, null] : (devices[st.input.node] ?? [null, null]);
    const talking = st.consoles.some(c => c.talking);
    this.voice.target = talking ? 0.6 : 0;
    const output = toDb(this.tone ? 0.75 : step(this.voice));
    return { t: 'meters', ts: Date.now(), input, output, devices };
  }

  // -------------------------------------------------------------- commands

  handle(conn, msg) {
    switch (msg.t) {
      case 'auth':
        if (msg.token === this.opts.token) {
          conn.authed = true;
          conn.deliver({ t: 'auth', ok: true });
          conn.deliver({ t: 'state', rev: this.rev, state: clone(this.state) });
        } else { conn.deliver({ t: 'auth', ok: false }); conn.close(4401); }
        return;
      case 'resync':
        if (conn.authed) conn.deliver({ t: 'state', rev: this.rev, state: clone(this.state) });
        return;
      case 'sub': (msg.topics ?? []).forEach(t => conn.topics.add(t)); return;
      case 'unsub': (msg.topics ?? []).forEach(t => conn.topics.delete(t)); return;
      case 'visibility': conn.hidden = !!msg.hidden; return;
      case 'cmd': {
        if (!conn.authed) { conn.close(4401); return; }
        let result = {};
        try {
          result = (this.role === 'console' ? this.consoleCmd : this.agentCmd).call(this, conn, msg.cmd, msg.args ?? {}) ?? {};
          conn.deliver({ t: 'ack', id: msg.id, ok: true, result });
        } catch (e) {
          conn.deliver({ t: 'ack', id: msg.id, ok: false, error: { code: e.code ?? 'internal', message: e.message } });
        }
        return;
      }
      default:
        conn.close(4400);
    }
  }

  station(args) {
    const s = this.state.stations[args.station];
    if (!s) throw err('not_found', `No station ${args.station}`);
    return s;
  }

  consoleCmd(conn, cmd, a) {
    const num = (v, name) => { if (typeof v !== 'number' || Number.isNaN(v)) throw err('bad_request', `${name} must be a number`); return v; };
    const bool = (v, name) => { if (typeof v !== 'boolean') throw err('bad_request', `${name} must be true or false`); return v; };
    switch (cmd) {
      case 'connection.connect': {
        for (const k of ['server', 'group', 'password', 'name']) if (k in a && typeof a[k] !== 'string') throw err('bad_request', `${k} must be a string`);
        if (a.group === '') throw err('bad_request', 'Group is required');
        if (a.password === 'wrong') { this.change(st => { st.connection.state = 'failed'; st.connection.reason = 'Wrong group password'; }); throw err('busy', 'Wrong group password'); }
        this.change(st => {
          if (a.server) st.connection.server = a.server;
          if (a.group) st.connection.group = a.group;
          if (a.name) st.self.name = a.name;
          if (a.password) st.connection.passwordSaved = true;
          st.connection.state = 'connected'; st.connection.reason = null;
        });
        return;
      }
      case 'connection.disconnect':
        this.change(st => { st.connection.state = 'failed'; st.connection.reason = 'Disconnected'; });
        return;
      case 'station.setLevel': { const s = this.station(a); const v = clamp(num(a.db, 'db'), -40, 6); this.change(() => { s.level = v; }); return; }
      case 'station.setPan': { const s = this.station(a); const v = clamp(num(a.pan, 'pan'), -1, 1); this.change(() => { s.pan = v; }); return; }
      case 'station.setMute': { const s = this.station(a); const v = bool(a.on, 'on'); this.change(() => { s.mute = v; }); return; }
      case 'station.setSolo': { const s = this.station(a); const v = bool(a.on, 'on'); this.change(() => { s.solo = v; }); return; }
      case 'station.setTalk': { const s = this.station(a); const v = bool(a.on, 'on'); this.change(() => { s.talk = v; }); return; }
      case 'station.forget': {
        const s = this.station(a);
        if (s.presence !== 'offline') throw err('busy', 'Only offline stations can be forgotten');
        this.change(st => { delete st.stations[s.id]; st.stationOrder = st.stationOrder.filter(id => id !== s.id); });
        return;
      }
      case 'stations.talkToAll': this.change(st => Object.values(st.stations).forEach(s => { s.talk = true; })); return;
      case 'stations.centerAll': this.change(st => Object.values(st.stations).forEach(s => { s.pan = 0; })); return;
      case 'stations.spread':
        this.change(st => {
          const ids = st.stationOrder, n = ids.length;
          ids.forEach((id, i) => { st.stations[id].pan = n === 1 ? 0 : Math.round((-1 + (2 * i) / (n - 1)) * 100) / 100; });
        });
        return;
      case 'mic.setMode':
        if (a.mode !== 'open' && a.mode !== 'ptt') throw err('bad_request', 'mode must be open or ptt');
        this.change(st => { st.mic.mode = a.mode; if (a.mode === 'ptt') for (const c of this.conns) c.pttHeld = false; });
        return;
      case 'mic.setOn':
        if (this.state.mic.mode !== 'open') throw err('wrong_mode', 'mic.setOn only works in open-mic mode');
        { const v = bool(a.on, 'on'); this.change(st => { st.mic.on = v; }); }
        return;
      case 'mic.ptt': { const v = bool(a.down, 'down'); conn.pttHeld = v; this.change(() => {}); return; }
      case 'output.setLevel': { const v = clamp(num(a.db, 'db'), -40, 6); this.change(st => { st.output.level = v; }); return; }
      case 'devices.setInput': case 'devices.setOutput': {
        const list = cmd === 'devices.setInput' ? this.state.devices.inputs : this.state.devices.outputs;
        const d = list.find(x => x.id === a.id);
        if (!d) throw err('not_found', `No device ${a.id}`);
        this.change(st => { if (cmd === 'devices.setInput') st.mic.device = d.name; else st.output.device = d.name; });
        return;
      }
      case 'settings.set':
        this.change(st => { for (const k of ['soloDimDb', 'pttHotkey', 'codec', 'bitrateKbps', 'networkBuffer']) if (k in a) st.settings[k] = a[k]; });
        return;
      default:
        throw err('bad_request', `Unknown command ${cmd}`);
    }
  }

  agentCmd(conn, cmd, a) {
    const st = this.state;
    switch (cmd) {
      case 'agent.setInput': case 'agent.setOutput': {
        const isIn = cmd === 'agent.setInput';
        const d = (isIn ? st.devices.inputs : st.devices.outputs).find(x => x.node === a.node);
        if (!d) throw err('not_found', `No PipeWire node ${a.node}`);
        const previous = isIn ? st.input.node : st.output.node;
        this.change(s => {
          const target = isIn ? s.input : s.output;
          target.node = d.node; target.description = d.description; target.status = 'ok';
        });
        return { previous };
      }
      case 'agent.testTone':
        if (!st.devices.outputs.some(x => x.node === a.node)) throw err('not_found', `No PipeWire node ${a.node}`);
        this.tone = true;
        setTimeout(() => { this.tone = false; }, 1500);
        return;
      case 'agent.pause': this.change(s => { s.sending = 'paused'; }); return;
      case 'agent.resume': this.change(s => { s.sending = s.consoles.length ? 'active' : 'idle'; }); return;
      case 'agent.reloadConfig':
      case 'agent.retryNow':
        this.change(s => {
          s.connection = { ...s.connection, state: 'connected', attempt: null, retryInSec: null, error: null };
          if (!s.consoles.length) s.consoles = [{ name: 'maelo-mac', kind: 'mac', latencyMs: 32, talking: false }];
          if (s.sending === 'idle') s.sending = 'active';
        });
        return;
      default:
        throw err('bad_request', `Unknown command ${cmd}`);
    }
  }
}

class Conn {
  constructor(engine) {
    this.engine = engine;
    this.topics = new Set();
    this.hidden = false;
    this.authed = false;
    this.pttHeld = false;
    this.open = true;
    this.transport = {
      send: msg => this.open && queueMicrotask(() => engine.handle(this, clone(msg))),
      close: () => this.close(1000),
      onmessage: () => {},
      onclose: () => {},
    };
  }

  deliver(msg) {
    if (this.open) this.transport.onmessage(clone(msg));
  }

  close(code) {
    if (!this.open) return;
    this.open = false;
    this.engine.conns.delete(this);
    // api §2.4: a dropped client releases its push-to-talk hold.
    if (this.pttHeld) { this.pttHeld = false; this.engine.change(() => {}); }
    queueMicrotask(() => this.transport.onclose(code));
  }
}

function err(code, message) {
  return Object.assign(new Error(message), { code });
}

/** Speech-like envelope, as in the prototypes. */
function step(a, choppy = false) {
  if (a.burst <= 0 && Math.random() < a.target * 0.08) a.burst = 20 + Math.random() * 60;
  const target = a.burst > 0 ? (0.35 + Math.random() * 0.65) * (a.target > 0.6 ? 1 : 0.8) : Math.random() * 0.04;
  a.burst--;
  a.env += (target - a.env) * 0.35;
  if (choppy && Math.random() < 0.12) a.env = 0;
  if (a.target === 0) a.env *= 0.5;
  return a.env;
}

/** 0..1 envelope → [peak, rms] dBFS, null below the floor (api §4). */
function toDb(env) {
  if (env < 0.02) return [null, null];
  const peak = Math.round((env * 60 - 60) * 10) / 10;
  return [peak, Math.round((peak - 7) * 10) / 10];
}
