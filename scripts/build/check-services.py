#!/usr/bin/env python3
"""check-services.py — is every shipped OpenRC service accounted for?

  check-services.py [--inventory openrc/services.toml]
  check-services.py --explain        the rules, in words

`init-system`: "Each OpenRC service shipped in this repository SHALL have an
inventory entry naming its runlevel, what it requires, and how it reports
failure … enforced by a command that fails when a service file has no entry — an
inventory maintained by hand is a list, not a check." Five rules:

  1. every file in the service directory has an entry named after it,
  2. every entry names a file that exists — an entry for a deleted service is a
     claim about a service, and a false one,
  3. every entry carries runlevel, requires, establishes and failure: the three
     the spec names, plus what the start path proves before it reports success,
     which is the difference between "started" and "did the work",
  4. the runlevel agrees with the `rc-update add <name> <runlevel>` line in the
     file that enables services. An init script does not contain its own runlevel
     — enabling is a separate act, and here it is a line in the install guide —
     so without this rule the inventory could state a runlevel nobody chose,
  5. a shipped service that no line enables is either enabled or explicitly
     declared not enabled with a reason. A service nobody enables never runs,
     and silence is how that stays invisible until a GPU is found unconfigured.

An empty or absent inventory fails rather than passing by iterating nothing, for
the same reason the class map does: a gate that compared nothing has not been
passed.
"""

import argparse
import pathlib
import re
import sys
import tomllib

EXPLAIN = """\
The service inventory is `openrc/services.toml`; the services are the files in
the directory it names. The check compares the two and reports one line per
service, so a service missing from the inventory is a gap in the output rather
than a document claiming coverage. See the module docstring for the five rules
and why each is the spec's."""

REQUIRED = ("runlevel", "requires", "establishes", "failure", "owner")


def fail(msg, fails):
    print(f"FAIL {msg}", file=sys.stderr)
    fails.append(msg)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--inventory", default="openrc/services.toml")
    ap.add_argument("--explain", action="store_true", help="print the rules and exit")
    a = ap.parse_args()
    if a.explain:
        print(EXPLAIN)
        return 0

    fails = []
    inv_path = pathlib.Path(a.inventory)
    if not inv_path.is_file():
        fail(f"inventory absent: {inv_path} — refusing to pass by comparing "
             f"nothing; every shipped service is unaccounted until it exists", fails)
        return 1
    try:
        inv = tomllib.loads(inv_path.read_text())
    except tomllib.TOMLDecodeError as e:
        fail(f"inventory unparseable: {inv_path}: {e}", fails)
        return 1

    services = inv.get("service", [])
    if not services:
        fail(f"inventory holds no [[service]] row: {inv_path} — refusing to pass "
             f"by iterating zero services", fails)
        return 1

    d = pathlib.Path(inv.get("inventory", {}).get("directory", "openrc/init.d"))
    if not d.is_dir():
        fail(f"service directory named by {inv_path} does not exist: {d}", fails)
        return 1
    files = sorted(p.name for p in d.iterdir() if p.is_file() and not p.name.endswith(".toml"))

    guide = pathlib.Path(inv.get("inventory", {}).get("enabled_by", "docs/INSTALL_GUIDE.md"))
    enabled = dict(re.findall(r"^\s*rc-update add (\S+) (\S+)", guide.read_text(), re.M)
                   ) if guide.is_file() else {}
    if not guide.is_file():
        fail(f"{inv_path} says services are enabled by {guide}, which does not "
             f"exist — no runlevel below can be checked against anything", fails)

    by_name = {}
    for s in services:
        if "name" not in s:
            fail(f"{inv_path}: a [[service]] row carries no name", fails)
            continue
        by_name[s["name"]] = s

    for orphan in sorted(set(files) - set(by_name)):
        fail(f"{d / orphan} is a shipped service with no inventory entry — "
             f"nothing states where it runs, what it needs, or how it fails", fails)
    for orphan in sorted(set(by_name) - set(files)):
        fail(f"{inv_path}: entry {by_name[orphan]['name']!r} names no file in {d}/", fails)

    for name, s in sorted(by_name.items()):
        if missing := [f for f in REQUIRED if not s.get(f)]:
            fail(f"{inv_path}: service {name} carries no {', '.join(missing)}", fails)
        runlevel = s.get("runlevel")
        declared = s.get("enabled")
        if name in enabled:
            state = enabled[name]
            if state != runlevel:
                fail(f"{name}: inventory says runlevel {runlevel!r} but {guide} "
                     f"enables it at {state!r}", fails)
            if declared is False:
                fail(f"{name}: the inventory declares it not enabled while "
                     f"{guide} enables it at {state!r} — one of the two is a lie "
                     f"about the running system", fails)
        elif declared is False:
            state = "not-enabled"
            if not s.get("enabled_reason"):
                fail(f"{name}: declared not enabled with no enabled_reason — "
                     f"why nothing enables a shipped service is the part worth "
                     f"reading in six months", fails)
        else:
            state = "nothing-enables-it"
            fail(f"{name}: shipped, and no {guide} line enables it — a service "
                 f"nobody enables never runs. Either enable it or declare "
                 f"enabled = false with the reason", fails)
        print(f"service: {name} runlevel={state} requires={','.join(s.get('requires', []))}")

    print(f"services: {'FAIL ' + str(len(fails)) if fails else 'ok'} "
          f"files={len(files)} entries={len(by_name)}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
