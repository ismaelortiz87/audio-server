// Design-review screenshots (spec §9): every mock scenario of both UIs, at
// desktop and phone width, plus the settings sheet.
//   node scripts/serve.mjs &   node scripts/screenshots.mjs [outDir]
// Needs Chrome (CHROME=/path/to/chrome to override). Output: PNGs + index.html.

import { execFileSync } from 'node:child_process';
import { mkdirSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';

const CHROME = process.env.CHROME ?? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const UI = process.env.UI ?? 'http://localhost:5173';
const out = resolve(process.argv[2] ?? 'screenshots');
mkdirSync(out, { recursive: true });

const consoleScenarios = ['everyday', 'twocalls', 'problem', 'four', 'connecting', 'empty', 'failed'];
const agentScenarios = ['listening', 'talking', 'waiting', 'paused', 'reconnecting', 'password', 'nodevice'];
const shots = [];

function shoot(name, url, w, h) {
  const file = join(out, `${name}.png`);
  const args = ['--headless=new', '--disable-gpu', '--hide-scrollbars', `--window-size=${w},${h}`,
    '--virtual-time-budget=3000', `--screenshot=${file}`, url];
  try { execFileSync(CHROME, args, { stdio: 'ignore' }); }
  catch { execFileSync(CHROME, args, { stdio: 'ignore' }); } // headless Chrome occasionally exits early; retry once
  shots.push({ name, file: `${name}.png`, w });
}

// Phone shots go through a 390-wide iframe: headless Chrome won't make a
// window narrower than ~500 px, and the UI uses container queries anyway.
function phone(name, path) {
  const html = join(out, `_${name}.html`);
  writeFileSync(html, `<html><body style="margin:0;background:#07090a"><iframe src="${UI}${path}" width="390" height="844" style="border:0"></iframe></body></html>`);
  shoot(name, `file://${html}`, 390, 844);
}

for (const s of consoleScenarios) {
  shoot(`console-${s}-desktop`, `${UI}/?mock=${s}`, 1280, 860);
  phone(`console-${s}-phone`, `/?mock=${s}`);
}
shoot('console-settings-desktop', `${UI}/?mock=everyday&open=settings`, 1280, 860);
phone('console-settings-phone', '/?mock=failed&open=settings');
for (const s of agentScenarios) shoot(`agent-${s}`, `${UI}/agent.html?mock=${s}`, 520, 900);

writeFileSync(join(out, 'index.html'), `<!doctype html><meta charset="utf-8"><title>Crosspoint review shots</title>
<body style="background:#111;color:#ccc;font:13px system-ui;padding:16px">
${shots.map(s => `<figure style="display:inline-block;margin:8px;vertical-align:top"><img src="${s.file}" width="${Math.min(s.w, 640)}"><figcaption>${s.name}</figcaption></figure>`).join('\n')}
</body>`);
console.log(`${shots.length} screenshots → ${out}/index.html`);
