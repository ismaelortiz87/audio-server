// Service worker for the installable PWA (P7.7). Caches the UI shell only:
// the control API (/api/), WebRTC signalling (/rtc/) and anything else always
// go to the network, so the app never shows stale engine state. When the
// engine can't be reached, the page itself shows "can't reach the Console
// server" (src/lib/boot.js); this worker just lets that page load offline.

const VERSION = 'crosspoint-shell-v1';
const SHELL = [
  './', './index.html', './agent.html', './manifest.webmanifest', './agent.webmanifest',
  './src/styles/tokens.css', './src/styles/base.css', './src/styles/console.css', './src/styles/agent.css',
  './src/lib/boot.js', './src/lib/dom.js', './src/lib/format.js', './src/lib/patch.js', './src/lib/store.js', './src/lib/tokens.js',
  './src/api/client.js', './src/api/mock.js', './src/api/scenarios.js',
  './src/console/app.js', './src/console/components.js', './src/console/activity.js',
  './src/agent/app.js',
  './icons/pwa-192.png', './icons/pwa-512.png', './icons/pwa-maskable-512.png', './icons/crosspoint-32.png',
];

self.addEventListener('install', event => {
  event.waitUntil(caches.open(VERSION).then(c => c.addAll(SHELL)).then(() => self.skipWaiting()));
});

self.addEventListener('activate', event => {
  event.waitUntil(caches.keys()
    .then(keys => Promise.all(keys.filter(k => k !== VERSION).map(k => caches.delete(k))))
    .then(() => self.clients.claim()));
});

// Network first for the shell (so an updated UI is picked up immediately when
// online), falling back to the cache when offline. Never cache API traffic.
self.addEventListener('fetch', event => {
  const url = new URL(event.request.url);
  if (event.request.method !== 'GET' || url.origin !== self.location.origin) return;
  if (url.pathname.includes('/api/') || url.pathname.includes('/rtc/')) return;
  event.respondWith(
    fetch(event.request)
      .then(res => {
        if (res.ok) { const copy = res.clone(); caches.open(VERSION).then(c => c.put(event.request, copy)); }
        return res;
      })
      .catch(() => caches.match(event.request, { ignoreSearch: true }).then(r => r ?? caches.match('./index.html'))),
  );
});
