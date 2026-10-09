#!/usr/bin/env python3
"""Evaluate SonoBus peer dumps against a scenario expectation.

This script is intentionally standalone: it never launches processes and never
touches the network. Give it a directory of dump files and a scenario name and
it prints a per-peer diff. It is used by run.sh for both the settle poll and the
final verdict, and it is unit-tested directly by test-evaluate.sh.

DUMP CONTRACT (one JSON file per peer, refreshed ~1/s, written atomically by
the app as <dumps>/<name>.json):

    {
      "self": "p1",
      "selfRole": "console",
      "peers": [
        { "name": "p2", "role": "vdi", "connected": true,
          "sendAllow": true, "recvAllow": true,
          "sendActive": true, "recvActive": false, "receivingAudio": false }
      ]
    }

`role`/`selfRole` are one of "unknown", "vdi", "console". A peer that has not
yet been discovered may be absent from "peers". A dump may be missing, empty,
or half-written; parsing is defensive and callers retry. The real app also
writes an extra "hasRole" boolean; it is accepted and type-checked but never
required, so both the stub and the real app are valid producers.

TWO ROLES PER PEER (this matters):
  * effective role -- the peer's own role, reported as `selfRole`. The real app
    defaults to "console" when no --role is given.
  * observed role  -- what *other* peers see for it. A peer that does not
    advertise (SONOBUS_NO_ROLE_ADVERT=1, or stock SonoBus) is seen as
    "unknown" even though its own selfRole is a real role.

ROUTING MODELS (`expect`):
  mesh        : every peer may send to and receive from every other peer.
  matrix      : sendAllow in A's dump for B means "A may send to B", where
                  console -> vdi    allowed
                  vdi     -> console allowed
                  same role         blocked
                  either unknown    blocked
                recvAllow in A's dump for B is allowed(observed(B) -> own(A)).
  all-blocked : nothing is allowed in either direction. This is the state of the
                real app before P1.4 applies the matrix to open console<->vdi
                paths; such a scenario declares `expect: "all-blocked"`, records
                the target in `routing`, and explains itself in `pending`.

Exit codes: 0 = match, 1 = mismatch / did not settle, 2 = usage or config error.
"""

import argparse
import json
import os
import sys
import time

# Roles that may appear in a dump. A scenario role of null means "no --role flag
# was passed", which the real app resolves to its default role.
UNKNOWN = "unknown"
VDI = "vdi"
CONSOLE = "console"
ROLES = (UNKNOWN, VDI, CONSOLE)
DEFAULT_ROLE = CONSOLE

ROUTING_MODES = ("mesh", "matrix")
EXPECT_MODES = ("mesh", "matrix", "all-blocked")

# Fields every peer entry must carry for the routing verdict.
REQUIRED_ENTRY_FIELDS = ("role", "connected", "sendAllow", "recvAllow")
# Contract fields: presence required only with --strict-fields, but always
# type-checked when present.
OPTIONAL_ENTRY_FIELDS = ("sendActive", "recvActive", "receivingAudio")
# Recognised extras written by the real app; never required, only type-checked.
EXTRA_ENTRY_FIELDS = ("hasRole",)

# `connected` is a stream-level flag, not a routing property. Observed against
# the real app it is asymmetric and unstable (e.g. c1 sees c2 disconnected while
# c2 sees c1 connected, and the set changes between runs) because it tracks AOO
# invite/uninvite timing rather than the routing matrix. Asserting "connected is
# true" therefore produced flaky failures, so it is only checked for presence
# and boolean type by default; --require-connected opts into the strict check
# for callers who know their build reports it deterministically.

EXIT_OK = 0
EXIT_MISMATCH = 1
EXIT_USAGE = 2


class UsageError(Exception):
    """Bad invocation or bad scenario file (exit 2)."""


class DumpError(Exception):
    """A dump could not be read/parsed (transient during settle)."""


# --------------------------------------------------------------------------
# scenario loading and the routing model
# --------------------------------------------------------------------------

def normalize_role(role, where):
    """Map a scenario role (str or None) onto one of ROLES.

    None means "no --role flag"; the real app then uses its default role, so it
    resolves here to DEFAULT_ROLE rather than to "unknown".
    """
    if role is None:
        return DEFAULT_ROLE
    if not isinstance(role, str):
        raise UsageError("%s: role must be a string or null, got %r" % (where, role))
    low = role.strip().lower()
    if low not in ROLES:
        raise UsageError("%s: role %r is not one of %s" % (where, role, ", ".join(ROLES)))
    return low


def load_scenarios(path):
    """Load and lightly validate the scenario data file."""
    try:
        with open(path, "r") as fh:
            data = json.load(fh)
    except OSError as exc:
        raise UsageError("cannot read scenarios file %s: %s" % (path, exc))
    except ValueError as exc:
        raise UsageError("scenarios file %s is not valid JSON: %s" % (path, exc))

    if not isinstance(data, dict) or not isinstance(data.get("scenarios"), dict):
        raise UsageError("scenarios file %s must contain a 'scenarios' object" % path)
    return data["scenarios"]


def get_scenario(scenarios, name):
    if name not in scenarios:
        known = ", ".join(sorted(scenarios)) or "(none)"
        raise UsageError("unknown scenario %r; known scenarios: %s" % (name, known))
    scen = scenarios[name]
    if not isinstance(scen, dict):
        raise UsageError("scenario %r must be an object" % name)
    return scen


def scenario_env(entry, where):
    """Validate an optional env object into a flat {str: str} dict."""
    env = entry.get("env")
    if env is None:
        return {}
    if not isinstance(env, dict):
        raise UsageError("%s: env must be an object" % where)
    out = {}
    for key, value in env.items():
        if not isinstance(key, str) or not key:
            raise UsageError("%s: env keys must be non-empty strings" % where)
        if isinstance(value, bool):
            value = "1" if value else "0"
        elif isinstance(value, (int, float)):
            value = str(value)
        elif not isinstance(value, str):
            raise UsageError("%s: env[%s] must be a string" % (where, key))
        out[key] = value
    return out


def scenario_peers(scen, name):
    """Return {peer_name: spec} for a scenario, validating structure.

    Each spec has:
      effective : the peer's own role (its selfRole)
      observed  : what other peers see for it (effective, or unknown when it
                  does not advertise)
      advertise : bool
      env       : per-peer environment variables
    """
    peers = scen.get("peers")
    if not isinstance(peers, list) or not peers:
        raise UsageError("scenario %r must define a non-empty 'peers' list" % name)

    scen_env = scenario_env(scen, "scenario %r" % name)
    out = {}
    for idx, entry in enumerate(peers):
        where = "scenario %r peers[%d]" % (name, idx)
        if not isinstance(entry, dict):
            raise UsageError("%s must be an object" % where)
        pname = entry.get("name")
        if not isinstance(pname, str) or not pname.strip():
            raise UsageError("%s needs a non-empty string 'name'" % where)
        if pname in out:
            raise UsageError("%s: duplicate peer name %r" % (where, pname))

        effective = normalize_role(entry.get("role"), where)
        advertise = entry.get("advertise", True)
        if not isinstance(advertise, bool):
            raise UsageError("%s: advertise must be true or false" % where)

        env = dict(scen_env)
        env.update(scenario_env(entry, where))
        # advertise:false is sugar for the app's no-advertise switch, so the
        # real app and the stub both honour it without special casing.
        if not advertise:
            env["SONOBUS_NO_ROLE_ADVERT"] = "1"
        elif env.get("SONOBUS_NO_ROLE_ADVERT", "") not in ("", "0", "false", "no"):
            advertise = False

        out[pname] = {
            "effective": effective,
            "observed": effective if advertise else UNKNOWN,
            "advertise": advertise,
            "env": env,
        }
    return out


def scenario_routing(scen, name):
    """The target model (what the scenario is ultimately about)."""
    routing = scen.get("routing", "mesh")
    if not isinstance(routing, str) or routing.strip().lower() not in ROUTING_MODES:
        raise UsageError("scenario %r routing must be one of %s" % (name, ", ".join(ROUTING_MODES)))
    return routing.strip().lower()


def scenario_expect(scen, name, override=None):
    """What to compare against right now (defaults to the target model)."""
    if override:
        value = override
    else:
        value = scen.get("expect")
        if value is None:
            return scenario_routing(scen, name)
    if not isinstance(value, str) or value.strip().lower() not in EXPECT_MODES:
        raise UsageError("scenario %r expect must be one of %s" % (name, ", ".join(EXPECT_MODES)))
    return value.strip().lower()


def allowed(sender_role, receiver_role):
    """The routing matrix: may `sender_role` send to `receiver_role`?"""
    if sender_role == UNKNOWN or receiver_role == UNKNOWN:
        return False
    if sender_role == receiver_role:
        return False
    # Only console <-> vdi remain, which is allowed both ways.
    return True


def expected_for(expect, own_effective, other_observed):
    """Expected (sendAllow, recvAllow) in A's dump for B."""
    if expect == "mesh":
        return True, True
    if expect == "all-blocked":
        return False, False
    # matrix: we know our own role; we can only act on the role we observe.
    return (allowed(own_effective, other_observed),
            allowed(other_observed, own_effective))


# --------------------------------------------------------------------------
# dump reading
# --------------------------------------------------------------------------

def parse_dump(path):
    """Read one dump defensively. Raises DumpError if unusable right now."""
    try:
        with open(path, "r") as fh:
            raw = fh.read()
    except OSError as exc:
        raise DumpError("cannot read: %s" % exc)

    if not raw.strip():
        raise DumpError("file is empty")
    try:
        doc = json.loads(raw)
    except ValueError as exc:
        raise DumpError("invalid JSON (mid-rewrite?): %s" % exc)
    if not isinstance(doc, dict):
        raise DumpError("top-level JSON is %s, expected an object" % type(doc).__name__)
    return doc


def _fmt(value):
    return json.dumps(value)


def _type_name(value):
    return type(value).__name__


def check_dump(peer_name, doc, peers, expect, strict_fields, require_connected=False):
    """Compare one peer's parsed dump against expectations.

    `peers` maps every participant name to its spec. Returns (findings,
    settled) where findings is a list of human readable mismatch strings, and
    settled is False when the dump is incomplete in a way a retry could fix
    (absent peers) rather than plainly wrong.
    """
    findings = []
    spec = peers[peer_name]
    own_role = spec["effective"]
    others = sorted(n for n in peers if n != peer_name)

    # -- identity ---------------------------------------------------------
    self_name = doc.get("self")
    if self_name != peer_name:
        findings.append("self: expected %s, got %s" % (_fmt(peer_name), _fmt(self_name)))

    if "selfRole" not in doc:
        findings.append("selfRole: missing (expected %s)" % _fmt(own_role))
    else:
        got = doc["selfRole"]
        if got not in ROLES:
            findings.append("selfRole: expected one of %s, got %s"
                            % (_fmt(list(ROLES)), _fmt(got)))
        elif got != own_role:
            findings.append("selfRole: expected %s, got %s" % (_fmt(own_role), _fmt(got)))

    # -- peer entries -----------------------------------------------------
    entries = doc.get("peers")
    if entries is None:
        findings.append("peers: missing")
        return findings, False
    if not isinstance(entries, list):
        findings.append("peers: expected a list, got %s" % _type_name(entries))
        return findings, False

    by_name = {}
    for idx, entry in enumerate(entries):
        if not isinstance(entry, dict):
            findings.append("peers[%d]: expected an object, got %s" % (idx, _type_name(entry)))
            continue
        entry_name = entry.get("name")
        if not isinstance(entry_name, str) or not entry_name:
            findings.append("peers[%d].name: missing or not a string (%s)"
                            % (idx, _fmt(entry_name)))
            continue
        if entry_name == peer_name:
            findings.append("peers[%d]: dump lists itself (%s) in peers"
                            % (idx, _fmt(peer_name)))
            continue
        if entry_name in by_name:
            findings.append("peers: duplicate entry for %s" % _fmt(entry_name))
            continue
        by_name[entry_name] = entry

    # Entries we were not expecting at all.
    for extra in sorted(set(by_name) - set(others)):
        findings.append("%s -> %s: unexpected peer (not in this run)" % (peer_name, extra))

    # Compare each expected peer.
    for other in others:
        other_observed = peers[other]["observed"]
        exp_send, exp_recv = expected_for(expect, own_role, other_observed)
        entry = by_name.get(other)
        if entry is None:
            findings.append("%s -> %s: missing from peers (expected role %s, "
                            "sendAllow %s, recvAllow %s)"
                            % (peer_name, other, _fmt(other_observed),
                               _fmt(exp_send), _fmt(exp_recv)))
            continue

        # role, as this peer observes it (the advertised role, not selfRole)
        if "role" not in entry:
            findings.append("%s -> %s role: missing (expected %s)"
                            % (peer_name, other, _fmt(other_observed)))
        else:
            got = entry["role"]
            if got not in ROLES:
                findings.append("%s -> %s role: expected one of %s, got %s"
                                % (peer_name, other, _fmt(list(ROLES)), _fmt(got)))
            elif got != other_observed:
                findings.append("%s -> %s role: expected %s, got %s"
                                % (peer_name, other, _fmt(other_observed), _fmt(got)))

        # connected: presence and boolean type always; value only with
        # --require-connected (see the note near the top of this file).
        if "connected" not in entry:
            findings.append("%s -> %s connected: missing (expected true)" % (peer_name, other))
        elif not isinstance(entry["connected"], bool):
            findings.append("%s -> %s connected: expected a boolean, got %s"
                            % (peer_name, other, _type_name(entry["connected"])))
        elif require_connected and entry["connected"] is not True:
            findings.append("%s -> %s connected: expected true, got %s"
                            % (peer_name, other, _fmt(entry["connected"])))

        # routing flags
        for field, expected in (("sendAllow", exp_send), ("recvAllow", exp_recv)):
            if field not in entry:
                findings.append("%s -> %s %s: missing (expected %s)"
                                % (peer_name, other, field, _fmt(expected)))
            elif entry[field] is not expected:
                findings.append("%s -> %s %s: expected %s, got %s"
                                % (peer_name, other, field, _fmt(expected), _fmt(entry[field])))

        # Contract fields: presence only required in strict mode, but a present
        # field must always be a boolean.
        for field in OPTIONAL_ENTRY_FIELDS:
            if field not in entry:
                if strict_fields:
                    findings.append("%s -> %s %s: missing (contract field)"
                                    % (peer_name, other, field))
            elif not isinstance(entry[field], bool):
                findings.append("%s -> %s %s: expected a boolean, got %s"
                                % (peer_name, other, field, _type_name(entry[field])))

        # Recognised extras written by the real app.
        for field in EXTRA_ENTRY_FIELDS:
            if field not in entry:
                continue
            if not isinstance(entry[field], bool):
                findings.append("%s -> %s %s: expected a boolean, got %s"
                                % (peer_name, other, field, _type_name(entry[field])))
            elif strict_fields and entry[field] is not (other_observed != UNKNOWN):
                # hasRole should agree with whether a role is actually known.
                findings.append("%s -> %s %s: expected %s for a peer observed as %s, got %s"
                                % (peer_name, other, field,
                                   _fmt(other_observed != UNKNOWN),
                                   _fmt(other_observed), _fmt(entry[field])))

    return findings, True


def evaluate_dir(dumps_dir, peers, expect, strict_fields, require_connected=False):
    """Evaluate every peer in `peers`.

    Returns (results, all_settled) where results maps peer -> (status, findings)
    and status is one of "ok", "mismatch", "unreadable".
    """
    results = {}
    all_settled = True
    for peer in sorted(peers):
        path = os.path.join(dumps_dir, peer + ".json")
        try:
            doc = parse_dump(path)
        except DumpError as exc:
            results[peer] = ("unreadable", ["dump %s: %s" % (os.path.basename(path), exc)])
            all_settled = False
            continue

        findings, settled = check_dump(peer, doc, peers, expect, strict_fields,
                                       require_connected)
        if not settled:
            all_settled = False
        results[peer] = ("ok" if not findings else "mismatch", findings)
    return results, all_settled


def total_findings(results):
    return sum(len(f) for _, f in results.values())


def print_report(scenario_name, expect, routing, group, dumps_dir, peers, results, verbose):
    print("scenario:  %s (expect=%s, target=%s, group=%s)"
          % (scenario_name, expect, routing, group or "(none)"))
    print("peers:     %s" % ", ".join(
        "%s:%s%s" % (p, peers[p]["effective"],
                     "" if peers[p]["advertise"] else "(no-advert)")
        for p in sorted(peers)))
    print("dump dir:  %s" % dumps_dir)
    print("")
    bad_peers = 0
    for peer in sorted(peers):
        status, findings = results[peer]
        if status == "ok":
            print("peer %-12s OK" % peer)
            if verbose:
                for other in sorted(n for n in peers if n != peer):
                    exp_send, exp_recv = expected_for(
                        expect, peers[peer]["effective"], peers[other]["observed"])
                    print("    %s -> %s: role=%s connected=true sendAllow=%s recvAllow=%s"
                          % (peer, other, peers[other]["observed"], exp_send, exp_recv))
        else:
            bad_peers += 1
            label = "UNREADABLE" if status == "unreadable" else "MISMATCH"
            print("peer %-12s %s" % (peer, label))
            for line in findings:
                print("    %s" % line)
    print("")
    nfind = total_findings(results)
    if nfind == 0:
        print("PASS: %d peer(s) match the %s expectation" % (len(peers), expect))
    else:
        print("FAIL: %d mismatch(es) across %d peer(s)" % (nfind, bad_peers))
    return nfind


def print_pending_note(scen, name, dumps_dir, peers, expect, strict_fields,
                       require_connected=False):
    """Explain how the target model stands when it differs from `expect`.

    This is how a scenario records "the matrix model is not implemented yet"
    without encoding a false expectation: the run passes against the documented
    current state, and the target's status is reported separately.
    """
    routing = scenario_routing(scen, name)
    pending = scen.get("pending")
    if routing == expect and not pending:
        return
    results, _ = evaluate_dir(dumps_dir, peers, routing, strict_fields, require_connected)
    nfind = total_findings(results)
    verdict = "TARGET MODEL HOLDS" if nfind == 0 else "target not yet satisfied"
    print("pending:   %s"
          % (pending if isinstance(pending, str) and pending
             else "expect (%s) differs from target (%s)" % (expect, routing)))
    print("target:    %s -> %s (%d mismatch(es) against the target model)"
          % (routing, verdict, nfind))


# --------------------------------------------------------------------------
# subcommands
# --------------------------------------------------------------------------

def resolve_peers(scen, name, requested):
    """Scenario peer specs restricted to the participants of this run."""
    peers = scenario_peers(scen, name)
    if requested is None:
        return peers
    names = [n.strip() for n in requested.split(",") if n.strip()]
    if not names:
        raise UsageError("--peers was given but lists no names")
    missing = [n for n in names if n not in peers]
    if missing:
        raise UsageError("--peers names not in scenario %r: %s"
                         % (name, ", ".join(missing)))
    return {n: peers[n] for n in names}


def build_common_parser(parser):
    parser.add_argument("--dir", required=True,
                        help="directory holding <peer>.json dump files")
    parser.add_argument("--scenario", required=True, help="scenario name to evaluate")
    parser.add_argument("--scenarios", default=None,
                        help="path to scenarios.json (default: next to this script)")
    parser.add_argument("--peers", default=None,
                        help="comma separated participants; default: all scenario peers")
    parser.add_argument("--expect", default=None, choices=EXPECT_MODES,
                        help="override the scenario's 'expect' model")
    parser.add_argument("--strict-fields", action="store_true",
                        help="also require sendActive/recvActive/receivingAudio "
                             "and cross-check hasRole")
    parser.add_argument("--require-connected", action="store_true",
                        help="assert connected is true; off by default because the "
                             "real app reports it asymmetrically (see the module docstring)")
    parser.add_argument("--verbose", action="store_true",
                        help="also print matched expectations")
    return parser


def load_context(args):
    scen_path = args.scenarios
    if scen_path is None:
        scen_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "scenarios.json")
    scenarios = load_scenarios(scen_path)
    scen = get_scenario(scenarios, args.scenario)
    peers = resolve_peers(scen, args.scenario, args.peers)
    expect = scenario_expect(scen, args.scenario, args.expect)
    routing = scenario_routing(scen, args.scenario)
    return peers, expect, routing, scen


def cmd_check(args):
    peers, expect, routing, scen = load_context(args)
    if not os.path.isdir(args.dir):
        print("error: dump directory does not exist: %s" % args.dir, file=sys.stderr)
        return EXIT_MISMATCH
    results, _ = evaluate_dir(args.dir, peers, expect, args.strict_fields,
                              args.require_connected)
    nfind = print_report(args.scenario, expect, routing, scen.get("group"), args.dir,
                         peers, results, args.verbose)
    print_pending_note(scen, args.scenario, args.dir, peers, expect, args.strict_fields,
                       args.require_connected)
    return EXIT_MISMATCH if nfind else EXIT_OK


def cmd_settle(args):
    peers, expect, routing, scen = load_context(args)
    deadline = time.time() + args.timeout
    attempt = 0
    last_results = None
    while True:
        attempt += 1
        if os.path.isdir(args.dir):
            results, all_settled = evaluate_dir(args.dir, peers, expect, args.strict_fields,
                                                args.require_connected)
        else:
            results, all_settled = (
                {p: ("unreadable", ["dump directory missing: %s" % args.dir]) for p in peers},
                False)
        last_results = results
        nfind = total_findings(results)
        if nfind == 0:
            print("settled after %d poll(s) (%.1fs): all %d peer(s) agree"
                  % (attempt, args.interval * max(0, attempt - 1), len(peers)))
            if args.verbose:
                print_report(args.scenario, expect, routing, scen.get("group"), args.dir,
                             peers, results, True)
            print_pending_note(scen, args.scenario, args.dir, peers, expect, args.strict_fields,
                       args.require_connected)
            return EXIT_OK
        remaining = deadline - time.time()
        if remaining <= 0:
            print("did not settle within %.1fs (%d poll(s))" % (args.timeout, attempt))
            print_report(args.scenario, expect, routing, scen.get("group"), args.dir,
                         peers, last_results, args.verbose)
            return EXIT_MISMATCH
        if args.progress:
            # One line per poll so a long settle is not a black box.
            worst = sorted(
                "%s:%d" % (p, len(f)) for p, (_, f) in results.items() if f)
            print("waiting (%.0fs left, poll %d): not settled: %s"
                  % (remaining, attempt, ", ".join(worst) or "unknown"),
                  file=sys.stderr)
        time.sleep(min(args.interval, max(0.0, remaining)))


def cmd_list(args):
    scen_path = args.scenarios
    if scen_path is None:
        scen_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "scenarios.json")
    scenarios = load_scenarios(scen_path)
    for name in sorted(scenarios):
        scen = scenarios[name]
        try:
            peers = scenario_peers(scen, name)
            routing = scenario_routing(scen, name)
            expect = scenario_expect(scen, name)
            pending = scen.get("pending")
            summary = "%s: expect=%s target=%s peers=%s%s" % (
                name, expect, routing,
                ", ".join("%s:%s%s" % (p, peers[p]["effective"],
                                       "" if peers[p]["advertise"] else "(no-advert)")
                          for p in sorted(peers)),
                "  [pending: %s]" % pending if pending else "")
        except UsageError as exc:
            summary = "%s: INVALID (%s)" % (name, exc)
        print(summary)
    return EXIT_OK


def main(argv):
    parser = argparse.ArgumentParser(
        prog="evaluate.py",
        description="Compare SonoBus peer dumps against a scenario expectation.")
    sub = parser.add_subparsers(dest="command")

    p_check = sub.add_parser("check", help="evaluate once and exit non-zero on mismatch")
    build_common_parser(p_check)
    p_check.set_defaults(func=cmd_check)

    p_settle = sub.add_parser("settle", help="poll until the group settles or times out")
    build_common_parser(p_settle)
    p_settle.add_argument("--timeout", type=float, default=30.0,
                          help="seconds to wait for settle (default 30)")
    p_settle.add_argument("--interval", type=float, default=1.0,
                          help="seconds between polls (default 1)")
    p_settle.add_argument("--progress", action="store_true",
                          help="print a line per poll to stderr")
    p_settle.set_defaults(func=cmd_settle)

    p_list = sub.add_parser("list", help="list scenarios")
    p_list.add_argument("--scenarios", default=None)
    p_list.set_defaults(func=cmd_list)

    args = parser.parse_args(argv)
    if not getattr(args, "func", None):
        parser.print_help(sys.stderr)
        return EXIT_USAGE
    try:
        return args.func(args)
    except UsageError as exc:
        print("error: %s" % exc, file=sys.stderr)
        return EXIT_USAGE


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
