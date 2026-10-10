// Local reverse proxy for verifying the Crosspoint web Console end to end.
//
// Why: browsers expose navigator.mediaDevices ONLY in a secure context (HTTPS
// or localhost). The container serves plain HTTP on a LAN IP, so pointing
// Chrome straight at it means it can receive audio but can never capture the
// mic. Serving the same UI from http://localhost:<port> and proxying to the
// container gives Chrome a secure context, so the full audio path can be
// tested without touching certificates.
//
//   node tools/local-console-proxy.mjs --upstream 192.168.0.71 --port 8080
//
// /rtc/ -> upstream:8090, everything else -> upstream:7070, including the
// /api/v1/ws WebSocket upgrade.
import http from 'node:http';

const arg = (n, d) => { const i = process.argv.indexOf('--' + n); return i > 0 ? process.argv[i + 1] : d; };
const UP = arg('upstream', '192.168.0.71');
const PORT = Number(arg('port', '8080'));
const ENGINE = { host: UP, port: 7070 };
const GATEWAY = { host: UP, port: 8090 };

const pick = url => (url.startsWith('/rtc/') ? GATEWAY : ENGINE);

const server = http.createServer((req, res) => {
  const target = pick(req.url);
  const proxy = http.request(
    { ...target, path: req.url, method: req.method, headers: { ...req.headers, host: `${target.host}:${target.port}` } },
    up => { res.writeHead(up.statusCode, up.headers); up.pipe(res); });
  proxy.on('error', e => { res.writeHead(502); res.end('proxy error: ' + e.message); });
  req.pipe(proxy);
});

// WebSocket / upgrade passthrough (control API /api/v1/ws).
server.on('upgrade', (req, socket, head) => {
  const target = pick(req.url);
  const proxy = http.request(
    { ...target, path: req.url, method: req.method, headers: { ...req.headers, host: `${target.host}:${target.port}` } });
  proxy.on('upgrade', (up, upSocket, upHead) => {
    socket.write('HTTP/1.1 101 Switching Protocols\r\n' +
      Object.entries(up.headers).map(([k, v]) => `${k}: ${v}`).join('\r\n') + '\r\n\r\n');
    if (upHead?.length) socket.unshift(upHead);
    upSocket.pipe(socket); socket.pipe(upSocket);
    upSocket.on('error', () => socket.destroy());
    socket.on('error', () => upSocket.destroy());
  });
  proxy.on('error', () => socket.destroy());
  if (head?.length) proxy.write(head);
  proxy.end();
});

server.listen(PORT, () => console.log(`local console proxy: http://localhost:${PORT} -> ${UP}:7070 (+8090 for /rtc/)`));
