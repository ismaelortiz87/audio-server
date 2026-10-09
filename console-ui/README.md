# console-ui

The Crosspoint web UIs, from one codebase:

- **Console** (`index.html`): the mixer and talk-back UI. It is served by the
  engine in Console role: the Mac shell (P6.1), the web container (P7) and the
  PWA (P7.7).
- **Agent** (`agent.html`): the VDI agent's localhost UI (P2.6).

Built to [docs/design/spec.md](../docs/design/spec.md), talking to the engine
over [docs/control-api.md](../docs/control-api.md).

**No build step and no dependencies.** Plain ES modules and CSS, served as-is
by the engine or by the dev server below.

## Run against the mock engine

```bash
cd console-ui
node scripts/serve.mjs
open "http://localhost:5173/?mock=everyday"
open "http://localhost:5173/agent.html?mock=listening"
```

Console scenarios: `everyday`, `twocalls`, `problem`, `four`, `connecting`,
`empty`, `failed`. Agent scenarios: `listening`, `talking`, `waiting`, `paused`,
`reconnecting`, `password`, `nodevice`. Without `?mock=` the page connects to
the engine at `/api/v1/ws` on its own origin.

In the browser console, `__crosspoint` is the API client and
`__crosspointMock` the mock engine.

## Test

```bash
npm test                      # unit + protocol tests (node --test, no browser)
node scripts/serve.mjs &      # then:
node scripts/smoke.mjs        # drives real Chrome: clicks, keys, PTT, agent picker
```

## Layout

| Path | What |
|---|---|
| `src/api/client.js` | control API client: handshake, auth, state/patch with resync, commands, meters, reconnect |
| `src/api/mock.js` | in-page mock engine. **The executable reference for the API doc:** change both in the same PR |
| `src/api/scenarios.js` | mock scenarios (same situations as the design prototypes) |
| `src/lib/` | store, JSON-pointer patches, DOM helpers, formatting, boot, generated `tokens.js` |
| `src/console/` | Console layout, components (spec §3) and keyboard map (§4.2) |
| `src/agent/` | agent page (spec §6) |
| `src/styles/` | `tokens.css` (generated), `base.css`, `console.css`, `agent.css` |
| `scripts/gen-tokens.mjs` | regenerates `tokens.css` and `tokens.js` from `docs/design/tokens.json` and copies icons |
| `scripts/serve.mjs` | dev server (ES modules don't load from `file://`) |
| `scripts/smoke.mjs` | browser smoke test over the DevTools protocol |

## Rules

- **Never hand-edit colours or sizes.** Change `docs/design/tokens.json`, then
  run `npm run tokens`.
- **The UI never decides routing.** Show `hearsYou` from the engine and send
  commands; state changes arrive as patches.
- UI work is `design review` (spec §9): attach screenshots of every state you
  touched, desktop and phone.
