#!/usr/bin/env python3
"""test_base_lock_check.py — does the check fail the way it claims?

Run: python3 test_base_lock_check.py
Exit 0 = every defect is named and non-zero; a valid lock is accepted.
"""

import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).parent
CHECK = HERE / "scripts/build/tests/test-all.sh"
MAP = HERE / "class-map.toml"

GOOD = """\
[edition]
name = 'LFS 13.0-openrc'
url = 'https://example.invalid/lfs/13.0/'

[[package]]
name = 'Acl'
version = '2.3.2'
url = 'https://example.invalid/acl-2.3.2.tar.xz'
remote = 'https://example.invalid'
revision = '2.3.2'
md5 = '590765dee95907dbc3c856f7255bd669'
class = "source-built"
pages = ['chapter08/acl.html']
"""


def run(lock_text):
    with tempfile.TemporaryDirectory() as tmp:
        lock = pathlib.Path(tmp) / "base.lock"
        lock.write_text(lock_text)
        r = subprocess.run([sys.executable, str(CHECK), "base-lock", "--lock", str(lock),
                            "--map", str(MAP)], capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def main():
    fails = []

    code, out = run(GOOD)
    if code != 0:
        fails.append(f"a valid lock was rejected:\n{out}")

    # A class that is not a row of the map.
    code, out = run(GOOD.replace('class = "source-built"', 'class = "prebuilt-binary"'))
    if code == 0 or "prebuilt-binary" not in out or "Acl" not in out:
        fails.append(f"an unknown class passed or was not named:\n{out}")

    # A required field of the class, missing. Which fields are required is read
    # from the map, so this test breaks when the format changes — on purpose.
    for field in ("md5", "remote", "revision", "pages"):
        lock = "".join(l for l in GOOD.splitlines(True) if not l.startswith(field + " = "))
        code, out = run(lock)
        if code == 0 or field not in out or "Acl" not in out:
            fails.append(f"a source-built entry with no {field} passed:\n{out}")

    # A source-built entry with no book page cited.
    code, out = run(GOOD.replace("pages = ['chapter08/acl.html']", "pages = []"))
    if code == 0 or "book revision and page" not in out:
        fails.append(f"an entry citing no page passed:\n{out})")

    if fails:
        print(f"FAIL {len(fails)}")
        for f in fails:
            print("  " + f)
        return 1
    print("base-lock check: ok — the map's classes and fields are enforced, "
          "and each defect is named with its entry")
    return 0


if __name__ == "__main__":
    sys.exit(main())
