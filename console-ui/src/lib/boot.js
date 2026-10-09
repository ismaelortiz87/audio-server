// Shared start-up for both entry points: pick the transport, start the
// client, and show full-screen states (wrong app, auth failure) when needed.
//
//   ?mock=<scenario>   use the in-page mock engine (also the default on file://)
//   ?token=<secret>    token for non-loopback engines (api §2.2; normally a cookie)

import { Client, webSocketTransport, defaultWsUrl } from '../api/client.js';
import { MockEngine } from '../api/mock.js';
import { h } from './dom.js';

/**
 * @param {'console'|'vdi'} role
 * @param {(client: Client) => void} mount
 */
export function boot(role, mount) {
  const q = new URLSearchParams(location.search);
  const mockName = q.get('mock') ?? (location.protocol === 'file:' ? '' : null);
  let open;
  if (mockName !== null) {
    const engine = new MockEngine(role, mockName);
    open = () => engine.connect();
    Object.assign(globalThis, { __crosspointMock: engine });
  } else {
    open = webSocketTransport(defaultWsUrl());
  }

  let overlay = null;
  const show = (title, text) => {
    overlay?.remove();
    overlay = h('div.overlay', { role: 'alert' }, h('div.box', h('h1', title), h('p', text)));
    document.body.append(overlay);
  };

  // Can't reach the engine (off VPN, container down): say so plainly instead
  // of an endless spinner, and keep retrying underneath (spec §3.1).
  let unreachableTimer = null;
  const unreachable = () => show("Can't reach the Console server",
    'Check that you are on the VPN and that the Crosspoint server is running. Retrying…');

  const client = new Client(open, {
    expectRole: role,
    token: q.get('token') ?? undefined,
    onStatus(s, detail) {
      if (s === 'ready') { clearTimeout(unreachableTimer); unreachableTimer = null; overlay?.remove(); overlay = null; }
      if ((s === 'reconnecting' || s === 'connecting') && !unreachableTimer && !overlay) {
        unreachableTimer = setTimeout(() => { unreachableTimer = null; if (client.status !== 'ready') unreachable(); }, 4000);
      }
      if (s === 'wrong-role') {
        show('This is a different Crosspoint app',
          detail.role === 'vdi' ? 'This engine is a VDI agent. Open /agent.html instead.' : 'This engine is a Console. Open / instead.');
      }
      if (s === 'fatal') {
        show("Can't connect to the engine",
          detail.code === 4401 ? 'The access token was rejected.' : detail.code === 4403 ? 'This page is not allowed to control the engine (origin refused).' : 'The engine speaks a different API version.');
      }
    },
  });

  document.addEventListener('visibilitychange', () => client.setHidden(document.hidden));

  // PWA (P7.7): installable shell on secure origins (https, or localhost).
  if ('serviceWorker' in navigator && isSecureContext && location.protocol !== 'file:' && mockName === null) {
    navigator.serviceWorker.register('sw.js').catch(e => console.warn('[crosspoint] service worker not registered:', e.message));
  }

  Object.assign(globalThis, { __crosspoint: client });
  mount(client);
  client.start();
  return client;
}

/**
 * Throttle a continuous control (sliders, drag): at most one call per `ms`,
 * and the last value is always sent (api §5: ≤30/s).
 */
export function latest(fn, ms = 33) {
  let timer = null, pending = null, last = 0;
  return (...args) => {
    pending = args;
    const now = Date.now();
    if (!timer && now - last >= ms) { last = now; fn(...pending); pending = null; return; }
    if (!timer) timer = setTimeout(() => { timer = null; last = Date.now(); if (pending) { fn(...pending); pending = null; } }, ms - (now - last));
  };
}
