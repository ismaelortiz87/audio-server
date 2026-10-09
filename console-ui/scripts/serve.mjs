// Dev server for console-ui (no dependencies). Browsers won't load ES modules
// from file://, so open the UIs through this:
//   node scripts/serve.mjs            → http://localhost:5173/?mock=everyday
//   PORT=8080 node scripts/serve.mjs
// The real engine serves the same files itself (api §1); this is for the mock.

import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { extname, join, normalize, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const port = Number(process.env.PORT ?? 5173);
const types = {
  '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.mjs': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8', '.json': 'application/json', '.webmanifest': 'application/manifest+json',
  '.png': 'image/png', '.svg': 'image/svg+xml',
};

createServer(async (req, res) => {
  const url = new URL(req.url ?? '/', 'http://x');
  let path = decodeURIComponent(url.pathname);
  if (path === '/') path = '/index.html';
  const file = normalize(join(root, path));
  if (!file.startsWith(root) || file.includes('/node_modules/')) { res.writeHead(403).end(); return; }
  try {
    const body = await readFile(file);
    res.writeHead(200, { 'content-type': types[extname(file)] ?? 'application/octet-stream', 'cache-control': 'no-store' });
    res.end(body);
  } catch {
    res.writeHead(404).end('not found');
  }
}).listen(port, '127.0.0.1', () => {
  console.log(`Crosspoint UI dev server: http://localhost:${port}/?mock=everyday  ·  agent: http://localhost:${port}/agent.html?mock=listening`);
});
