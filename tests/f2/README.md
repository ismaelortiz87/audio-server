# tests/f2 — local multi-peer SonoBus harness

Starts a local AOO connection server and N headless SonoBus peers, waits for the
group to settle, then compares what each peer reports about the others against a
scenario expectation. Exits non-zero with a per-peer diff on mismatch.

It runs entirely locally, on a dynamically chosen free port, and only ever
signals processes it started itself.

```
tests/f2/
  run.sh             orchestrator (server, peers, settle, evaluate, teardown)
  evaluate.py        the expectation model — standalone and unit-tested
  scenarios.json     scenario definitions (peers, roles, routing, env)
  fake-peer.sh       stand-in peer so the flow can be tested without the app
  ports.py           finds a free TCP+UDP port
  test-evaluate.sh   unit tests for evaluate.py
```

## Quick start

```bash
# List scenarios
tests/f2/run.sh --list

# Against the real built app (default; needs build/desktop-release + aooserver-release)
tests/f2/run.sh --scenario mesh-stock

# Against the stub, with no C++ binary required at all
tests/f2/run.sh --scenario matrix-2v2 --app tests/f2/fake-peer.sh

# Unit-test the evaluator
tests/f2/test-evaluate.sh
```

## Flags

| Flag | Meaning |
| --- | --- |
| `--scenario NAME` | Scenario from `scenarios.json` (default `mesh-stock`). |
| `--app PATH` | SonoBus binary, or the stub. Default: the worktree's `build/desktop-release/.../Standalone/SonoBus`. |
| `--server PATH` | `aooserver` binary. Default: `build/aooserver-release/aooserver`. |
| `--count N` | Launch only the first N peers of the scenario. |
| `--peers a,b` | Evaluate only this subset (must be a subset of the scenario's peers). |
| `--timeout SEC` | Settle timeout (default 30). |
| `--interval SEC` | Settle poll interval (default 1). |
| `--peer-arg ARG` | Extra per-peer argument; `%NAME%`, `%ROLE%`, `%ROUTING%`, `%EXPECT%`, `%PORT%`, `%GROUP%` are substituted. Repeatable. Defaults to `--fake-role %ROLE% --fake-routing %EXPECT%` for the stub. |
| `--expect MODE` | Override the expectation for this run: `mesh`, `matrix`, `all-blocked`. |
| `--fault NAME` | `FAKE_PEER_FAULT` for every peer (stub only). |
| `--strict-fields` | Also require `sendActive`/`recvActive`/`receivingAudio` and cross-check `hasRole`. |
| `--require-connected` | Also assert `connected: true` (off by default — see below). |
| `--run-dir DIR` | Use this run directory instead of a fresh `mktemp -d`. |
| `--keep` | Keep the run directory and peer logs afterwards. |
| `--list` | List scenarios and exit. |
| `--verbose` | Verbose harness, peer commands, and evaluator output. |

Exit codes: `0` match, `1` mismatch or did not settle, `2` usage/config error.

## How the run works

1. **Build check** — verifies the app and server are executable, with a clear
   message pointing at `scripts/build-desktop.sh` or at the stub.
2. **Free port** — `ports.py` binds an ephemeral port for TCP *and* UDP (the
   server binds both). Nothing is hardcoded; port 10999 was occupied in testing.
3. **Server** — `aooserver -p <port> -l <logdir>` in its own process group.
   Readiness is confirmed from the CSV `ServerStart,<port>` log line *and* a real
   TCP connect, since the line is written before `listen()` completes.
4. **Peers** — one per scenario peer, each with:
   - its own **`CFFIXED_USER_HOME`** private directory (see below),
   - a distinct `-n/--username`, the shared `-g/--group`,
   - `-c 127.0.0.1:<port>` and `--dump-peers <dir>/<name>.json`,
   - `--role` from the scenario when one is specified,
   - the scenario's env vars (`SONOBUS_NO_ROLE_BLOCK`, `SONOBUS_NO_ROLE_ADVERT`).
5. **Settle** — `evaluate.py settle` polls the dump directory until every peer
   agrees, or the timeout expires.
6. **Teardown** — always runs (EXIT/INT/TERM trap): TERM then KILL each tracked
   process group, sweep any remaining process whose cwd is inside the run tree,
   then remove the run directory unless `--keep`.

### Why `CFFIXED_USER_HOME`

On macOS, JUCE resolves its settings through `NSHomeDirectory()`, which
**ignores `HOME`**. So setting `HOME` does not isolate anything: every peer would
load the real `~/Library/Application Support/SonoBus/SonoBus.settings`, whose
`reconnectlast=1.0` and recent-connection list make the app auto-reconnect with
the wrong identity and fail with `login failed: access denied`.

Setting **`CFFIXED_USER_HOME`** to a fresh private directory per peer is what
actually isolates settings. With it, peers log in cleanly and the server log
shows `UserJoin` + `GroupJoin`.

## Scenario model

```json
{
  "group": "f2-matrix-2v2",
  "routing": "matrix",
  "expect": "all-blocked",
  "pending": "P1.4 is not implemented; P1.3 blocks all new peers.",
  "env": { "SONOBUS_NO_ROLE_BLOCK": "1" },
  "peers": [
    { "name": "c1", "role": "console" },
    { "name": "u1", "role": null, "advertise": false }
  ]
}
```

- **`routing`** is the *target* model; **`expect`** is what the run compares
  against today (defaults to `routing`). When they differ, `pending` explains
  why, and the report prints the target model's status separately
  (`target: matrix -> target not yet satisfied (4 mismatch(es))`). This keeps the
  harness honest instead of encoding a false expectation or silently skipping.
- **`role: null`** means no `--role` flag. The real app then defaults to
  `console`, which is what the harness expects.
- **`advertise: false`** makes a peer look like stock SonoBus: its own
  `selfRole` is still its real role, but other peers observe `"unknown"` with
  `hasRole: false`. Implemented via `SONOBUS_NO_ROLE_ADVERT=1`.
- **`env`** may be set per scenario and per peer.

### Routing models

`sendAllow` in peer A's dump for peer B means "A may send to B".

| `expect` | Meaning |
| --- | --- |
| `mesh` | Every peer may send to and receive from every other peer. |
| `matrix` | `console -> vdi` allowed, `vdi -> console` allowed, same role blocked, either side `unknown` blocked. `recvAllow` is the reverse direction. |
| `all-blocked` | Nothing allowed either way (the real app's state before P1.4). |

## The dump contract

Each peer writes `<dumps>/<name>.json`, refreshed ~1/s, atomically (temp file +
rename), so a reader never sees a partial file:

```json
{
  "self": "p1",
  "selfRole": "console",
  "peers": [
    { "name": "p2", "role": "vdi", "connected": true,
      "sendAllow": true, "recvAllow": true,
      "sendActive": true, "recvActive": false, "receivingAudio": false }
  ]
}
```

The real app additionally writes `hasRole` per peer; the evaluator accepts and
type-checks it, and cross-checks it in `--strict-fields` mode. Peers not yet
discovered may be absent from `peers`. Parsing is defensive: a dump that is
missing, empty, or half-written is reported as `UNREADABLE` and retried.

### `connected` is deliberately not asserted by default

`connected` is a *stream-level* flag tracking AOO invite/uninvite timing, not a
routing property. Observed against the real app it is **asymmetric and
unstable**: e.g. c1 reports c2 as disconnected while c2 reports c1 as connected,
and which pairs are affected changes between runs. Asserting `connected: true`
therefore produced flaky failures, so by default the harness checks only that the
field is present and boolean. Use `--require-connected` to assert the value.

## Verified behaviour (real binaries, this worktree)

Built from the current worktree sources (`build/desktop-release` and
`build/aooserver-release`), with `--role` and `--dump-peers`:

| Scenario | Peer config | Result |
| --- | --- | --- |
| `mesh-stock` | 3 role-less peers + `SONOBUS_NO_ROLE_BLOCK=1` | **PASS** — all flags true both ways |
| `matrix-1v1` | 1 `--role console` + 1 `--role vdi` | **PASS** — all blocked (P1.3); target model reported as not yet satisfied |
| `matrix-2v2` | 2 console + 2 vdi | **PASS** — all blocked; same as above |
| `blocked-unknown` | 2 peers + `SONOBUS_NO_ROLE_ADVERT=1` | **PASS** — observed role `unknown`, all blocked |
| `matrix-target-check` | 1 console + 1 vdi, `expect: matrix` | **FAIL (intended)** — proves the harness still catches a real mismatch; passes only once P1.4 lands |

Observed raw dumps (abridged):

```
# mesh-stock, peer p1
{"self":"p1","selfRole":"console","peers":[
  {"name":"p2","role":"console","hasRole":true,"connected":true,
   "sendAllow":true,"recvAllow":true,"sendActive":true,"recvActive":true,
   "receivingAudio":true}, ...]}

# matrix-1v1, peer c1 (P1.4 absent: roles known, nothing routed)
{"self":"c1","selfRole":"console","peers":[
  {"name":"v1","role":"vdi","hasRole":true,"connected":true,
   "sendAllow":false,"recvAllow":false,"sendActive":false,"recvActive":false,
   "receivingAudio":false}]}

# blocked-unknown, peer u1 (selfRole stays console; u2 is observed as unknown)
{"self":"u1","selfRole":"console","peers":[
  {"name":"u2","role":"unknown","hasRole":false,"connected":true,
   "sendAllow":false,"recvAllow":false,...}]}
```

## Fault injection (stub)

`--fault NAME` sets `FAKE_PEER_FAULT` on every stub peer:

| Fault | Effect |
| --- | --- |
| `never-appear` | Never lists other peers (never settles). |
| `no-dump` | Never writes a dump (clean timeout). |
| `empty-dump` | Always leaves an empty, mid-rewrite file. |
| `bad-send-allow` | Answers `sendAllow=true` for every peer (caught wherever traffic should be blocked). |
| `self-role-unknown` | Advertises `selfRole: "unknown"` regardless. |

## Safety

- Only PIDs this script spawned are ever signalled, each verified via `lsof`
  cwd to still live inside the run directory before a forced kill.
- Each child runs in its **own process group** (`set -m`), so teardown signals
  the whole tree with one negative-PID kill and can never reach the harness.
- A final sweep kills anything still alive whose cwd is inside the run tree,
  catching grandchildren that outlived a wrapper.
- **No `pkill`/`killall`** and no pattern-based killing is used anywhere, so a
  running `/Applications/SonoBus.app` is never at risk.
- The run directory is a `mktemp -d` under `TMPDIR` (or `build/`), canonicalised
  with `pwd -P` because on macOS `/tmp` is a symlink to `/private/tmp`.
- `ps` is not used (blocked in this sandbox); liveness uses `kill -0` and `lsof`.
