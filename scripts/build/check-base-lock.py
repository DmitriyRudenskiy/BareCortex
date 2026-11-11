#!/usr/bin/env python3
"""check-base-lock.py — is `base.lock` an account for the base system?

Reads `class-map.toml` for the provenance classes and their required fields,
then checks `base.lock` against it. Fails naming the file and the entry.

  check-base-lock.py [--lock base.lock] [--map class-map.toml]
  check-base-lock.py --explain          the rules, in words
"""

import argparse
import pathlib
import sys
import tomllib

EXPLAIN = """\
base.lock is the base system's account of itself, so a check can ask whether any
part of the image is unaccounted. Three rules, all read from class-map.toml:

1. Every entry names a class that is a row of the class map. The map's
   [schema] enumerates field is the only declaration that a field lists the
   classes; a document naming classes in prose is not a declaration.
2. Every entry carries the fields its own class requires — the class map's
   `requires`, not a list kept here.
3. Every source-built entry cites the book: the file names the edition it was
   derived from, and the entry names at least one page of it. A line with no
   page is a recipe nobody wrote down, which is the thing this file exists to
   catch.

The class map is read fail-closed: absent, unparseable, or holding no class row
is a failure naming class-map.toml, never an empty loop that passes.
"""


def fail(msg, fails):
    fails.append(msg)
    print(f"FAIL {msg}")


def load_map(path, fails):
    """The class map, or a failure naming it. Never an empty set of classes."""
    if not path.is_file():
        fail(f"class map absent: {path} — the classes are its rows, and an "
             f"absent map would check nothing", fails)
        return None
    try:
        data = tomllib.loads(path.read_text())
    except tomllib.TOMLDecodeError as e:
        fail(f"class map unparseable: {path}: {e}", fails)
        return None
    classes = data.get("class")
    if not classes:
        fail(f"class map holds no class row: {path} — refusing to pass by "
             f"iterating zero classes", fails)
        return None
    for c in classes:
        if not c.get("requires"):
            fail(f"class {c.get('name', '?')} carries no required fields "
                 f"(row owned by {c.get('owner', 'nobody')}) in {path}", fails)
    return data


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--lock", default="base.lock")
    ap.add_argument("--map", dest="map", default="class-map.toml")
    ap.add_argument("--explain", action="store_true", help="print the rules and exit")
    a = ap.parse_args()

    if a.explain:
        print(EXPLAIN)
        return 0

    fails = []
    lock_path = pathlib.Path(a.lock)
    if not lock_path.is_file():
        fail(f"lock file absent: {lock_path} — derive it with "
             f"scripts/build/derive-base-lock.py", fails)
        return 1
    lock = tomllib.loads(lock_path.read_text())

    cmap = load_map(pathlib.Path(a.map), fails)
    if cmap is None:
        return 1

    required = {c["name"]: c.get("requires", []) for c in cmap["class"]}
    owners = {c["name"]: c.get("owner", "nobody") for c in cmap["class"]}
    field = cmap.get("schema", {}).get("enumerates")
    if not field:
        fail(f"class map declares no [schema] enumerates field: {a.map} — "
             f"without it no field can be checked against the classes", fails)
        return 1

    edition = lock.get("edition", {}).get("name")
    if not edition:
        fail(f"{lock_path} names no [edition]: a lock file that does not say "
             f"which book revision it was derived from cannot be cited", fails)

    # One machine-readable line per class: an incomplete map is known from these
    # lines, not from a document claiming the check is red.
    for name in sorted(required):
        entries = [e for e in lock.get("package", []) if e.get(field) == name]
        short = sum(1 for e in entries if set(required[name]) - set(e))
        print(f"class: {name} owner={owners[name]} entries={len(entries)} "
              f"entries_missing_fields={short}")

    for e in lock.get("package", []):
        cls = e.get(field)
        where = f"{e.get('name', '?')} {e.get('version', '?')}"
        if cls not in required:
            fail(f"{lock_path}: entry {where} names class {cls!r}, which is not a "
                 f"row of {a.map}; a class enters the vocabulary as a map row "
                 f"naming the change that owns it", fails)
            continue
        if missing := set(required[cls]) - set(e):
            fail(f"{lock_path}: entry {where} is class {cls} but carries no "
                 f"{', '.join(sorted(missing))}", fails)
        # The citation rule, stated for the one class that cites the book: the
        # page is what a reviewer opens to check the recipe was followed.
        if cls == "source-built":
            pages = [p for p in e.get("pages", []) if p != "UNMATCHED"]
            if not edition or not pages:
                fail(f"{lock_path}: entry {where} carries no book revision and "
                     f"page — added or moved without citing where the book "
                     f"builds it", fails)

    for u in lock.get("unattributed-download", []):
        print(f"unattributed: {u['name']} {u['version']} — {u['reason']}")

    print(f"base-lock: {'FAIL ' + str(len(fails)) if fails else 'ok'} "
          f"packages={len(lock.get('package', []))} edition={edition}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
