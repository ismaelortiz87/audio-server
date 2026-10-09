// Control API client (docs/control-api.md). Transport-agnostic: the real
// WebSocket and the in-page mock engine both implement the same tiny
// interface, so every view runs unchanged against either.

import { Store } from '../lib/store.js';

/**
 * @typedef {{ send(msg:object):void, close():void,
 *   onmessage:(msg:object)=>void, onclose:(code:number)=>void }} Transport
 */

const FATAL_CLOSES = new Set([4401, 4403, 4409]);

export class Client {
  /**
   * @param {() => Transport} openTransport  creates a fresh connection
   * @param {{ expectRole?: 'console'|'vdi', token?: string, onStatus?: (s:string, detail?:any)=>void }} opts
   */
  constructor(openTransport, opts = {}) {
    this.openTransport = openTransport;
    this.opts = opts;
    this.store = new Store();
    this.hello = null;
    this.nextId = 1;
    /** @type {Map<string,{resolve:Function,reject:Function}>} */ this.pending = new Map();
    this.topics = new Set();
    /** @type {Set<(m:any)=>void>} */ this.meterListeners = new Set();
    this.backoff = 500;
    this.stopped = false;
    this.status = 'connecting';
  }

  start() {
    this.stopped = false;
    this.connect();
    return this;
  }

  stop() {
    this.stopped = true;
    this.transport?.close();
  }

  connect() {
    this.setStatus('connecting');
    const t = this.openTransport();
    this.transport = t;
    t.onmessage = msg => this.handle(msg);
    t.onclose = code => this.closed(code);
  }

  closed(code) {
    for (const { reject } of this.pending.values()) reject(new ApiError('disconnected', 'Connection lost'));
    this.pending.clear();
    if (this.stopped) return;
    if (FATAL_CLOSES.has(code)) {
      this.setStatus('fatal', { code });
      return;
    }
    this.setStatus('reconnecting', { inMs: this.backoff });
    setTimeout(() => this.connect(), this.backoff);
    this.backoff = Math.min(this.backoff * 2, 8000);
  }

  setStatus(s, detail) {
    this.status = s;
    this.opts.onStatus?.(s, detail);
  }

  send(msg) {
    this.transport?.send(msg);
  }

  handle(msg) {
    switch (msg.t) {
      case 'hello':
        this.hello = msg;
        if (this.opts.expectRole && msg.role !== this.opts.expectRole) {
          this.setStatus('wrong-role', { role: msg.role });
          this.stop();
          return;
        }
        if (msg.auth === 'token') this.send({ t: 'auth', token: this.opts.token ?? '' });
        break;
      case 'auth':
        if (!msg.ok) this.setStatus('fatal', { code: 4401 });
        break;
      case 'state':
        this.store.reset(msg.state, msg.rev);
        this.backoff = 500;
        this.setStatus('ready');
        if (this.topics.size) this.send({ t: 'sub', topics: [...this.topics] });
        break;
      case 'patch':
        if (!this.store.patch(msg.ops, msg.rev)) this.send({ t: 'resync' });
        break;
      case 'meters':
        for (const fn of this.meterListeners) fn(msg);
        break;
      case 'ack': {
        const p = this.pending.get(msg.id);
        if (!p) break;
        this.pending.delete(msg.id);
        if (msg.ok) p.resolve(msg.result ?? {});
        else p.reject(new ApiError(msg.error?.code ?? 'internal', msg.error?.message ?? ''));
        break;
      }
      default:
        // Unknown message types are ignored (api §2.1).
        break;
    }
  }

  /**
   * Send a command; resolves with the ack's result, rejects with ApiError.
   * @param {string} cmd @param {object} args
   */
  cmd(cmd, args = {}) {
    const id = 'c' + this.nextId++;
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      this.send({ t: 'cmd', id, cmd, args });
    });
  }

  /** Fire-and-forget variant for continuous controls; errors are logged. */
  fire(cmd, args = {}) {
    this.cmd(cmd, args).catch(e => console.warn(`[crosspoint] ${cmd} failed:`, e.code, e.message));
  }

  subscribe(topic) {
    this.topics.add(topic);
    if (this.status === 'ready') this.send({ t: 'sub', topics: [topic] });
  }

  unsubscribe(topic) {
    this.topics.delete(topic);
    if (this.status === 'ready') this.send({ t: 'unsub', topics: [topic] });
  }

  onMeters(fn) {
    this.meterListeners.add(fn);
    return () => this.meterListeners.delete(fn);
  }

  setHidden(hidden) {
    if (this.status === 'ready') this.send({ t: 'visibility', hidden });
  }
}

export class ApiError extends Error {
  constructor(code, message) {
    super(message || code);
    this.code = code;
  }
}

/** Real transport: one WebSocket to the engine (same origin). */
export function webSocketTransport(url) {
  return () => {
    const ws = new WebSocket(url);
    /** @type {Transport} */
    const t = {
      send: msg => ws.readyState === WebSocket.OPEN && ws.send(JSON.stringify(msg)),
      close: () => ws.close(1000),
      onmessage: () => {},
      onclose: () => {},
    };
    ws.onmessage = ev => {
      try { t.onmessage(JSON.parse(ev.data)); } catch (e) { console.warn('[crosspoint] bad message', e); }
    };
    ws.onclose = ev => t.onclose(ev.code);
    return t;
  };
}

/** ws(s)://<same origin>/api/v1/ws */
export const defaultWsUrl = () =>
  (location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/api/v1/ws';
