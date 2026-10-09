# Handoff — F2 + P1.1/P1.2/P1.3 landed (for the session working in `docs/` and `design/`)

From: the agent that implemented F2 / P1.1 / P1.2 / P1.3.
Status: **done, merged to `main`** (`3fb88cfb`), engine + harness both build on
macOS and the harness passes. Written because this work changes assumptions the
design work depends on (notably D10/D11 and P1.7).

## What changed in the engine (affects your design work)

- **`PeerRole { Unknown, VDI, Console }` exists** on the processor, with
  `getRole()`/`setRole()`, persisted in saved state (`ExtraState/Role`), and a
  new `--role vdi|console` CLI flag (default `console`). So the role vocabulary
  in the UX spec is now real, and `unknown` is a genuine third state you can
  design for (it is what stock SonoBus peers look like).
- **Role is advertised in the existing peer-info JSON** as `"role"`, and parsed
  onto each remote peer. A peer that never sends it stays `unknown`.
- **New peers start blocked** (`sendAllow=false`, `recvAllow=false`) until their
  role is known.
- **P1.7 is now unblocked** — it depends on P1.2, which is done. The peer-info
  channel it needs is already carrying per-peer data, so an `agent` object can
  be added the same way `role` was.
- **P1.4 is the critical path and is NOT done.** Until it lands the engine
  passes **no audio at all** in the default configuration: P1.3 blocks every
  peer and only P1.4 opens the console↔vdi paths. Design work is unaffected, but
  any "it works end to end" claim in a prototype or spec must not assume audio
  flows yet.

## Directly relevant to D10/D11 (agent = headless engine + localhost UI, service)

- **Headless mode did not persist state at all before this change.** `shutdown()`
  only saved when a window existed, so a headless process lost its settings on
  exit. Fixed for the normal exit path — but note:
- **`SIGTERM` does not reach JUCE's `shutdown()`.** Measured: `SIGINT` is ignored
  outright, and `SIGTERM` kills the process without running the save path. That
  matters for **P2.5 (run as a systemd unit / Windows service)**: on service
  stop, config/state will not be written. Whoever does P2.5 should install a
  signal handler that quits the message loop properly. Worth reflecting in the
  agent design if the spec promises anything about surviving restarts.
- Headless runs fine with **no real audio device** on macOS; 4 concurrent
  instances (the max planned scale) is comfortable.

## How to verify routing yourself (F2)

```bash
tests/f2/run.sh --list                       # scenarios
tests/f2/run.sh --scenario mesh-stock        # pre-roles full mesh (passes today)
tests/f2/run.sh --scenario matrix-1v1        # reports P1.4 target separately
tests/f2/run.sh --scenario matrix-target-check   # fails until P1.4 lands
```

Exit codes: `0` match, `1` mismatch, `2` usage/config error.
`tests/f2/README.md` has the full flag list and how to add a scenario.

Two engine env vars exist for testing states you may want to show in the UI:

- `SONOBUS_NO_ROLE_BLOCK=1` — new peers start open (pre-P1.3 behaviour).
- `SONOBUS_NO_ROLE_ADVERT=1` — a peer advertises no role, i.e. looks like stock
  SonoBus and shows up as `unknown`.

## Two traps that cost real time (worth knowing before you run peers)

1. **On macOS, JUCE resolves its settings directory via `NSHomeDirectory()`,
   which ignores `$HOME`.** Isolating a test peer with `HOME=...` silently does
   nothing: every peer loads your real
   `~/Library/Application Support/SonoBus/SonoBus.settings`, whose
   `reconnectlast=1.0` makes it auto-reconnect with the wrong identity and fail
   with `login failed: access denied`. Use **`CFFIXED_USER_HOME`** instead
   (`tests/f2/run.sh` already does).
2. **A bare `pkill -f SonoBus` will kill your real running app.** The harness
   never uses `pkill`/`killall`; it only signals PIDs it spawned, each verified
   via its `lsof` cwd against its own run directory. Please keep that property.

## Concurrency warning about `docs/TASKS.md`

`docs/TASKS.md` is **untracked** and we have both been editing it. I updated the
status/Result rows for F2 and P1.1–P1.3 and added a warning note to P1.4. If you
rewrite that file wholesale from your own copy, **those edits will be silently
lost** — my changes are working-tree only, in no commit. Suggest either:
commit it soon so both our edits are versioned, or merge changes into it rather
than replacing it.
