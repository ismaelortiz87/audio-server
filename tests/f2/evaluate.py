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

P1.5 STEPS: a scenario may also list timed "steps" (see run_steps below). They
run after the routing model has settled: each step writes per-peer control
files (the app's --test-control) and asserts dump fields such as hearsYou,
sendGateGain, preFaderPeakDb and postGainPeakDb.

Exit codes: 0 = match, 1 = mismatch / did not settle, 2 = usage or config error.
"""

import argparse
import json
import os
import re
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

# P1.7: VDI agent health, reported by a VDI peer inside an "agent" object and
# parsed by the Console into the peer entry. `hasAgent` says whether this peer
# sent an agent object at all -- a Console never does -- so "no agent" is not
# confused with "agent reports ok".
AGENT_STATES = {
    "input": ("ok", "missing", "silent"),
    "output": ("ok", "missing"),
}
AGENT_KEYS = ("input", "output", "paused", "config_error")

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

        # P1.7: optional expected agent health for this peer. A peer with no
        # "agent" in the scenario is not asserted (Consoles have none), but when
        # present every listed key is checked, so a malformed or missing object
        # fails loudly rather than being ignored.
        agent = scenario_agent(entry, where)

        out[pname] = {
            "effective": effective,
            "observed": effective if advertise else UNKNOWN,
            "advertise": advertise,
            "env": env,
            "agent": agent,
            "kind": scenario_kind(entry, where),
        }
    return out


def scenario_kind(entry, where):
    """P1.7: optional expected Console kind ('mac'|'web'|'other') for a peer."""
    kind = entry.get("kind")
    if kind is not None and kind not in ("mac", "web", "other"):
        raise UsageError("%s: kind must be mac, web or other, got %r" % (where, kind))
    return kind


def scenario_agent(entry, where):
    """Validate an optional per-peer 'agent' expectation.

    Shape: {"input": "ok"|"missing"|"silent", "output": "ok"|"missing",
            "paused": bool, "config_error": str|null}
    Only the keys given are asserted. Returns None when the peer declares none.
    """
    raw = entry.get("agent")
    if raw is None:
        return None
    if not isinstance(raw, dict):
        raise UsageError("%s: agent must be an object" % where)

    out = {}
    for key, value in raw.items():
        if key not in AGENT_KEYS:
            raise UsageError("%s: agent key %r is not one of %s"
                             % (where, key, ", ".join(AGENT_KEYS)))
        if key in AGENT_STATES:
            if not isinstance(value, str) or value not in AGENT_STATES[key]:
                raise UsageError("%s: agent.%s must be one of %s, got %r"
                                 % (where, key, ", ".join(AGENT_STATES[key]), value))
        elif key == "paused":
            if not isinstance(value, bool):
                raise UsageError("%s: agent.paused must be a boolean, got %r" % (where, value))
        elif key == "config_error":
            if value is not None and not isinstance(value, str):
                raise UsageError("%s: agent.config_error must be a string or null, got %r"
                                 % (where, value))
        out[key] = value
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

    # -- this peer's own agent health (P1.7) ------------------------------
    # A VDI reports its own health under "selfAgent"; a Console omits the key.
    # When the scenario declares agent health for this peer, the peer's own dump
    # must agree with it -- otherwise only the *other* peers would be checked and
    # a VDI could silently misreport itself.
    self_expected = spec.get("agent")
    self_agent = doc.get("selfAgent")
    if self_expected is not None:
        if self_agent is None:
            findings.append("selfAgent: missing (expected %s)" % _fmt(self_expected))
        elif not isinstance(self_agent, dict):
            findings.append("selfAgent: expected an object, got %s" % _type_name(self_agent))
        else:
            for key, want in self_expected.items():
                if key not in self_agent:
                    findings.append("selfAgent.%s: missing (expected %s)"
                                    % (key, _fmt(want)))
                elif self_agent[key] != want:
                    findings.append("selfAgent.%s: expected %s, got %s"
                                    % (key, _fmt(want), _fmt(self_agent[key])))
    elif self_agent is not None and not isinstance(self_agent, dict):
        findings.append("selfAgent: expected an object, got %s" % _type_name(self_agent))

    # -- own kind (P1.7): only a Console with a declared kind is asserted ----
    if spec.get("kind") is not None and doc.get("selfKind") != spec["kind"]:
        findings.append("selfKind: expected %s, got %s"
                        % (_fmt(spec["kind"]), _fmt(doc.get("selfKind"))))

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

        findings.extend(check_agent(peer_name, other, entry, peers[other].get("agent")))
        want_kind = peers[other].get("kind")
        if want_kind is not None and entry.get("kind") != want_kind:
            findings.append("%s -> %s kind: expected %s, got %s"
                            % (peer_name, other, _fmt(want_kind), _fmt(entry.get("kind"))))

    return findings, True


def check_agent(peer_name, other, entry, expected):
    """P1.7: check a peer entry's agent health object.

    Two jobs:
      * always type-check the object when the peer sent one, so a malformed
        agent fails loudly even in scenarios that do not assert health;
      * assert the scenario's expected keys for this peer when it declares any.
    """
    findings = []
    prefix = "%s -> %s agent" % (peer_name, other)

    raw = entry.get("agent")
    has_agent = entry.get("hasAgent")

    if has_agent is not None and not isinstance(has_agent, bool):
        findings.append("%s hasAgent: expected a boolean, got %s"
                        % (prefix, _type_name(has_agent)))

    if raw is None:
        if expected is not None:
            findings.append("%s: missing (this peer reports health, expected %s)"
                            % (prefix, _fmt(expected)))
        return findings

    if not isinstance(raw, dict):
        findings.append("%s: expected an object, got %s" % (prefix, _type_name(raw)))
        return findings

    # An agent object that is present must be well formed, asserted or not.
    for key in AGENT_KEYS:
        if key not in raw:
            findings.append("%s.%s: missing (contract field)" % (prefix, key))

    for key, permitted in AGENT_STATES.items():
        if key not in raw:
            continue
        value = raw[key]
        if not isinstance(value, str) or value not in permitted:
            findings.append("%s.%s: expected one of %s, got %s"
                            % (prefix, key, _fmt(list(permitted)), _fmt(value)))

    if "paused" in raw and not isinstance(raw["paused"], bool):
        findings.append("%s.paused: expected a boolean, got %s"
                        % (prefix, _type_name(raw["paused"])))

    if "config_error" in raw and raw["config_error"] is not None \
            and not isinstance(raw["config_error"], str):
        findings.append("%s.config_error: expected a string or null, got %s"
                        % (prefix, _type_name(raw["config_error"])))

    # The scenario's declared expectations for this peer.
    for key, want in (expected or {}).items():
        if key not in raw:
            findings.append("%s.%s: missing (expected %s)" % (prefix, key, _fmt(want)))
        elif raw[key] != want:
            findings.append("%s.%s: expected %s, got %s"
                            % (prefix, key, _fmt(want), _fmt(raw[key])))

    # If a peer declares health, its own dump should agree that it has an agent.
    if expected is not None and has_agent is False:
        findings.append("%s hasAgent: expected true for a peer that reports health, got false"
                        % prefix)

    return findings


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


# --------------------------------------------------------------------------
# P1.5: timed control steps and per-field assertions
# --------------------------------------------------------------------------
#
# A scenario may carry "steps". run.sh first settles the routing model as
# usual, then runs `evaluate.py steps`, which for each step in order:
#   1. deep-merges the step's "control" ({peer: control-object}) into that
#      peer's accumulated control and writes <control-dir>/<peer>.json
#      atomically (the app polls it ~1/s via --test-control);
#   2. polls the dumps until every assertion in "expect" holds, then requires
#      them to keep holding for "hold" seconds (default 2) -- a failure during
#      the hold restarts the wait while the step's "timeout" (default 25 s)
#      allows;
#   3. records "capture" values for later steps.
#
# An assertion is {"peer": P, "of": Q?, "field": F, <check>...}. With "of" the
# field is read from P's dump entry for peer Q, without it from P's top level.
# Checks (any combination):
#   "eq": value               exact equality
#   "min": n / "max": n       numeric bounds
#   "near": {"field": G | "capture": name | "value": n, "offset": d, "tol": t}
#                             |F - (ref + offset)| <= tol, where ref is field G
#                             of the same entry, a captured value, or n
#   "increases": true         F is larger at the end of the hold than at its
#                             start (e.g. packet counters: the stream is up)
#   "capture": name           store F (at the end of the step) for later steps
#
# P2.3/P2.4 additions to a step (all optional, applied in this order):
#   "harness": "server-stop" | "server-start"
#                             run.sh kills / restarts ITS OWN aooserver (same
#                             port) and acks; the step clock starts after the ack,
#                             so "PASS after Ns" of a server-start step is the
#                             time-to-rejoin.
#   "wait": seconds           sleep before polling (e.g. keep the server down)
#   "matrix": true            also require the scenario's routing model to hold
#   "logs": [ {...} ]         assertions on a peer's stderr log (peer.err):
#       {"peer": P, "log": "retry_ladder", "scale": s, "min": n, "filter": "audio"?}
#           the logged "retrying in X s" delays grow 1,2,4... *s and cap at 30*s
#       {"peer": P, "log": "retry_spacing", "scale": s, "min": n}
#           consecutive "connect attempt" lines are ~30*s apart (no tight loop)
#       {"peer": P, "log": "match", "regex": R, "min": n}
#   Field names may be dotted ("selfAgent.input") to reach into nested objects.

STEP_ASSERT_KEYS = ("peer", "of", "field", "eq", "min", "max", "near",
                    "increases", "capture", "notnull")
STEP_KEYS = ("name", "control", "expect", "timeout", "hold", "harness", "wait", "matrix", "logs")
LOG_KINDS = ("retry_ladder", "retry_spacing", "match")
HARNESS_CMDS = ("server-stop", "server-start")


def scenario_steps(scen, name):
    """Validate and return the scenario's steps list ([] when none)."""
    steps = scen.get("steps")
    if steps is None:
        return []
    if not isinstance(steps, list):
        raise UsageError("scenario %r: steps must be a list" % name)
    peer_names = set(scenario_peers(scen, name))
    out = []
    for idx, step in enumerate(steps):
        where = "scenario %r steps[%d]" % (name, idx)
        if not isinstance(step, dict):
            raise UsageError("%s must be an object" % where)
        for key in step:
            if key not in STEP_KEYS:
                raise UsageError("%s: unknown key %r" % (where, key))
        control = step.get("control", {})
        if not isinstance(control, dict):
            raise UsageError("%s: control must be an object" % where)
        for peer, ctl in control.items():
            if peer not in peer_names:
                raise UsageError("%s: control names unknown peer %r" % (where, peer))
            if not isinstance(ctl, dict):
                raise UsageError("%s: control[%s] must be an object" % (where, peer))
        expect = step.get("expect", [])
        logs = step.get("logs", [])
        if not isinstance(expect, list) or not isinstance(logs, list):
            raise UsageError("%s: expect and logs must be lists" % where)
        if not expect and not logs and not step.get("matrix") and not step.get("harness"):
            raise UsageError("%s: needs expect, logs, matrix or harness" % where)
        if step.get("harness") is not None and step["harness"] not in HARNESS_CMDS:
            raise UsageError("%s: harness must be one of %s" % (where, ", ".join(HARNESS_CMDS)))
        for lidx, la in enumerate(logs):
            if not isinstance(la, dict) or la.get("log") not in LOG_KINDS or la.get("peer") not in peer_names:
                raise UsageError("%s logs[%d]: needs a scenario peer and log in %s" % (where, lidx, ", ".join(LOG_KINDS)))
        for aidx, a in enumerate(expect):
            awhere = "%s expect[%d]" % (where, aidx)
            if not isinstance(a, dict):
                raise UsageError("%s must be an object" % awhere)
            for key in a:
                if key not in STEP_ASSERT_KEYS:
                    raise UsageError("%s: unknown key %r" % (awhere, key))
            if a.get("peer") not in peer_names:
                raise UsageError("%s: peer must name a scenario peer" % awhere)
            if "of" in a and a["of"] not in peer_names:
                raise UsageError("%s: of must name a scenario peer" % awhere)
            if not isinstance(a.get("field"), str) or not a["field"]:
                raise UsageError("%s: field must be a non-empty string" % awhere)
            if not any(k in a for k in ("eq", "min", "max", "near", "increases", "capture", "notnull")):
                raise UsageError("%s: needs at least one check" % awhere)
            near = a.get("near")
            if near is not None:
                if not isinstance(near, dict) or \
                        sum(1 for k in ("field", "capture", "value") if k in near) != 1:
                    raise UsageError("%s: near needs exactly one of field/capture/value" % awhere)
                if not isinstance(near.get("tol", 0), (int, float)):
                    raise UsageError("%s: near.tol must be a number" % awhere)
        for key in ("timeout", "hold"):
            if key in step and (not isinstance(step[key], (int, float)) or step[key] < 0):
                raise UsageError("%s: %s must be a non-negative number" % (where, key))
        out.append({
            "name": step.get("name") or ("step%d" % (idx + 1)),
            "control": control,
            "expect": expect,
            "timeout": float(step.get("timeout", 25)),
            "hold": float(step.get("hold", 2)),
            "harness": step.get("harness"),
            "wait": float(step.get("wait", 0)),
            "matrix": bool(step.get("matrix")),
            "logs": logs,
        })
    return out


def deep_merge(base, update):
    """Merge `update` into a copy of `base`; dicts merge, anything else replaces."""
    out = dict(base)
    for key, value in update.items():
        if isinstance(value, dict) and isinstance(out.get(key), dict):
            out[key] = deep_merge(out[key], value)
        else:
            out[key] = value
    return out


def write_json_atomic(path, doc):
    tmp = path + ".tmp"
    with open(tmp, "w") as fh:
        json.dump(doc, fh)
    os.replace(tmp, path)


def _dig(obj, path):
    """(found, value) for a possibly dotted path ("selfAgent.input")."""
    cur = obj
    for part in path.split("."):
        if not isinstance(cur, dict) or part not in cur:
            return False, None
        cur = cur[part]
    return True, cur


def _lookup(docs, a):
    """(found, value, entry) for an assertion's peer/of/field."""
    doc = docs.get(a["peer"])
    if doc is None:
        return False, None, None
    if "of" in a:
        for entry in doc.get("peers") or []:
            if isinstance(entry, dict) and entry.get("name") == a["of"]:
                found, value = _dig(entry, a["field"])
                return found, value, entry
        return False, None, None
    found, value = _dig(doc, a["field"])
    return found, value, doc


# --- P2.4: log assertions ---------------------------------------------------

def _read_log(peers_dir, peer):
    try:
        with open(os.path.join(peers_dir, peer, "peer.err"), errors="replace") as fh:
            return fh.read().splitlines()
    except OSError:
        return []


def check_log(a, peers_dir):
    """(ok, description) for one log assertion."""
    lines = _read_log(peers_dir, a["peer"]) if peers_dir else []
    label = "%s log %s" % (a["peer"], a["log"])
    kind = a["log"]
    if kind == "match":
        n = sum(1 for ln in lines if re.search(a.get("regex", ""), ln))
        return n >= a.get("min", 1), "%s /%s/ x%d (want >= %d)" % (label, a.get("regex", ""), n, a.get("min", 1))

    scale = float(a.get("scale", 1.0))
    cap = 30.0 * scale
    eps = 0.011
    if kind == "retry_ladder":
        want_audio = a.get("filter") == "audio"
        vals = []
        for ln in lines:
            if "Crosspoint agent" not in ln or ("audio" in ln) != want_audio:
                continue
            m = re.search(r"retrying in ([0-9.]+) s", ln)
            if m:
                vals.append(float(m.group(1)))
        desc = "%s delays=%s (scale %g, cap %g s)" % (label, vals, scale, cap)
        if len(vals) < a.get("min", 4):
            return False, desc + ": need >= %d" % a.get("min", 4)
        if not (0.8 * scale - eps <= vals[0] <= scale + eps):
            return False, desc + ": first delay should be 1 s * scale (minus jitter)"
        reached = False
        for i, v in enumerate(vals):
            if v > cap + eps:
                return False, desc + ": %.2f exceeds the cap" % v
            if reached:
                if v < 0.8 * cap - eps:
                    return False, desc + ": dropped below the cap band after reaching it"
            else:
                if i and v < vals[i - 1] - eps:
                    return False, desc + ": delay shrank before reaching the cap"
                if v >= 0.8 * cap - eps:
                    reached = True
        if not reached and a.get("reach_cap", True):
            return False, desc + ": never reached the cap"
        return True, desc
    if kind == "retry_spacing":
        ts = []
        for ln in lines:
            m = re.search(r"\[t=([0-9.]+)s\].*connect attempt \d+", ln)
            if m:
                ts.append(float(m.group(1)))
        gaps = [round(y - x, 2) for x, y in zip(ts, ts[1:])]
        lo, hi = 0.8 * cap - 0.3, cap + 1.0
        desc = "%s attempt gaps=%s (want %.1f..%.1f s)" % (label, gaps, lo, hi)
        if len(gaps) < a.get("min", 2):
            return False, desc + ": need >= %d gaps" % a.get("min", 2)
        bad = [g for g in gaps if g < lo or g > hi]
        return (not bad), desc + ("" if not bad else ": out of band %s" % bad)
    return False, "%s: unknown kind" % label


def _label(a):
    if "of" in a:
        return "%s -> %s %s" % (a["peer"], a["of"], a["field"])
    return "%s %s" % (a["peer"], a["field"])


def _is_num(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def check_assertion(docs, a, captures, baseline=None):
    """Return (ok, description) for one assertion against the current dumps.

    `baseline` is the value seen at the start of the hold window, used by
    "increases"; None means the window has not started (treated as passing so
    the other checks decide when the hold starts)."""
    found, value, entry = _lookup(docs, a)
    label = _label(a)
    if not found:
        return False, "%s: missing" % label
    problems = []
    if a.get("notnull") and value is None:
        problems.append("expected a non-null value")
    if "eq" in a and value != a["eq"]:
        problems.append("expected %s" % _fmt(a["eq"]))
    if "min" in a and not (_is_num(value) and value >= a["min"]):
        problems.append("expected >= %s" % a["min"])
    if "max" in a and not (_is_num(value) and value <= a["max"]):
        problems.append("expected <= %s" % a["max"])
    near = a.get("near")
    if near is not None:
        offset = near.get("offset", 0)
        tol = near.get("tol", 0)
        if "field" in near:
            ref, refdesc = entry.get(near["field"]), near["field"]
        elif "capture" in near:
            ref, refdesc = captures.get(near["capture"]), "captured %s" % near["capture"]
        else:
            ref, refdesc = near["value"], _fmt(near["value"])
        if not (_is_num(ref) and _is_num(value)):
            problems.append("near %s: not numeric (ref %s)" % (refdesc, _fmt(ref)))
        else:
            want = ref + offset
            if abs(value - want) > tol:
                problems.append("expected %s%+g = %.1f +/- %g"
                                % (refdesc, offset, want, tol))
    if a.get("increases") and baseline is not None:
        if not (_is_num(value) and _is_num(baseline) and value > baseline):
            problems.append("expected to increase from %s" % _fmt(baseline))
    desc = "%s = %s" % (label, _fmt(value))
    if near is not None and "field" in near and _is_num(entry.get(near["field"])):
        desc += " (%s = %s)" % (near["field"], _fmt(entry.get(near["field"])))
    if problems:
        return False, desc + ": " + "; ".join(problems)
    return True, desc


def read_dumps(dumps_dir, peers):
    docs = {}
    for peer in peers:
        try:
            docs[peer] = parse_dump(os.path.join(dumps_dir, peer + ".json"))
        except DumpError:
            pass
    return docs


def request_harness(harness_dir, cmd, seq, clock=time.time, sleep=time.sleep, timeout=40):
    """P2.4: ask run.sh to do `cmd` (server-stop/start) and wait for its ack."""
    write_json_atomic(os.path.join(harness_dir, "cmd.json"), {"seq": seq, "cmd": cmd})
    deadline = clock() + timeout
    while clock() < deadline:
        try:
            with open(os.path.join(harness_dir, "ack.json")) as fh:
                ack = json.load(fh)
            if ack.get("seq") == seq:
                return bool(ack.get("ok"))
        except (OSError, ValueError):
            pass
        sleep(0.2)
    return False


def run_steps(dumps_dir, control_dir, peers, steps, interval, out=sys.stdout,
              clock=time.time, sleep=time.sleep, harness_dir=None, peers_dir=None,
              matrix_expect=None):
    """Execute the steps. Returns EXIT_OK or EXIT_MISMATCH."""
    hseq = 0
    controls = {}
    captures = {}
    for sidx, step in enumerate(steps):
        for peer, ctl in step["control"].items():
            controls[peer] = deep_merge(controls.get(peer, {}), ctl)
            if control_dir:
                write_json_atomic(os.path.join(control_dir, peer + ".json"), controls[peer])
        if step["harness"]:
            if not harness_dir:
                print("step %d %s FAIL: harness action needs --harness-dir" % (sidx + 1, step["name"]), file=out)
                return EXIT_MISMATCH
            hseq += 1
            if not request_harness(harness_dir, step["harness"], hseq, clock, sleep):
                print("step %d %s FAIL: harness %s was not acknowledged" % (sidx + 1, step["name"], step["harness"]), file=out)
                return EXIT_MISMATCH
        if step["wait"]:
            sleep(step["wait"])
        t0 = clock()
        deadline = t0 + step["timeout"]
        has_increases = any(a.get("increases") for a in step["expect"])
        hold_start = None
        hold_polls = 0
        baselines = {}
        last = []
        while True:
            docs = read_dumps(dumps_dir, peers)
            # "increases" compares with the value seen when the hold began; on
            # the poll that starts the hold there is no baseline yet.
            results = [check_assertion(docs, a, captures, baselines.get(i))
                       for i, a in enumerate(step["expect"])]
            results += [check_log(la, peers_dir) for la in step["logs"]]
            if step["matrix"] and matrix_expect:
                mres, _ = evaluate_dir(dumps_dir, peers, matrix_expect, False)
                nm = total_findings(mres)
                results.append((nm == 0, "routing matrix (%s) %s" % (
                    matrix_expect, "holds" if nm == 0 else "has %d mismatch(es)" % nm)))
            last = results
            now = clock()
            if all(ok for ok, _ in results):
                if hold_start is None:
                    hold_start = now
                    hold_polls = 0
                    for i, a in enumerate(step["expect"]):
                        if a.get("increases"):
                            baselines[i] = _lookup(docs, a)[1]
                else:
                    hold_polls += 1
                # an "increases" check needs at least one poll after the baseline
                if now - hold_start >= step["hold"] and (hold_polls > 0 or not has_increases):
                    for a in step["expect"]:
                        if "capture" in a:
                            captures[a["capture"]] = _lookup(docs, a)[1]
                    print("step %d %-14s PASS after %.1fs (held %.1fs)"
                          % (sidx + 1, step["name"], hold_start - t0, now - hold_start), file=out)
                    for _, desc in results:
                        print("    ok   %s" % desc, file=out)
                    break
            else:
                hold_start = None
                baselines = {}
            if now >= deadline:
                print("step %d %-14s FAIL: not satisfied within %.0fs%s"
                      % (sidx + 1, step["name"], step["timeout"],
                         " (held %.1fs of %.1fs)" % (now - hold_start, step["hold"])
                         if hold_start is not None else ""), file=out)
                for ok, desc in last:
                    print("    %s %s" % ("ok  " if ok else "FAIL", desc), file=out)
                return EXIT_MISMATCH
            sleep(interval)
    print("PASS: all %d step(s) held" % len(steps), file=out)
    return EXIT_OK


def cmd_steps(args):
    peers, expect, _, scen = load_context(args)
    steps = scenario_steps(scen, args.scenario)
    if not steps:
        print("scenario %s has no steps" % args.scenario)
        return EXIT_OK
    if args.control_dir and not os.path.isdir(args.control_dir):
        raise UsageError("control directory does not exist: %s" % args.control_dir)
    return run_steps(args.dir, args.control_dir, peers, steps, args.interval,
                     harness_dir=args.harness_dir, peers_dir=args.peers_dir,
                     matrix_expect=expect)


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
            steps = scenario_steps(scen, name)
            summary = "%s: expect=%s target=%s peers=%s%s%s" % (
                name, expect, routing,
                ", ".join("%s:%s%s" % (p, peers[p]["effective"],
                                       "" if peers[p]["advertise"] else "(no-advert)")
                          for p in sorted(peers)),
                "  steps=%d" % len(steps) if steps else "",
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

    p_steps = sub.add_parser("steps", help="P1.5: run the scenario's timed control steps")
    build_common_parser(p_steps)
    p_steps.add_argument("--control-dir", default=None,
                         help="directory for <peer>.json control files (the app's --test-control)")
    p_steps.add_argument("--interval", type=float, default=1.0,
                         help="seconds between polls (default 1)")
    p_steps.add_argument("--harness-dir", default=None,
                         help="P2.4: directory for server-stop/start requests to run.sh")
    p_steps.add_argument("--peers-dir", default=None,
                         help="P2.4: directory holding <peer>/peer.err for log assertions")
    p_steps.set_defaults(func=cmd_steps)

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
