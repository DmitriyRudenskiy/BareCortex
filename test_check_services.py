#!/usr/bin/env python3
"""test_check_services.py — does the service check fail the way it claims?

Run: python3 test_check_services.py
Exit 0 = every defect is named and non-zero, and the repository as it stands
passes. Builds throwaway trees rather than touching openrc/.
"""

import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).parent
CHECK = HERE / "scripts/build/tests/test-all.sh"

GUIDE = """\
```bash
rc-update add gpu-fancontrol default
```
"""

GOOD = """\
[inventory]
directory = "{tmp}/init.d"
enabled_by = "{tmp}/INSTALL_GUIDE.md"

[[service]]
name = "gpu-fancontrol"
runlevel = "default"
requires = ["nvidia-persistenced"]
establishes = "bmc-fan's exit status"
failure = "eend of bmc-fan's exit code"
owner = "gpu-stack"
"""


def run(tmp, inventory):
    path = pathlib.Path(tmp) / "services.toml"
    path.write_text(inventory.format(tmp=tmp))
    (pathlib.Path(tmp) / "INSTALL_GUIDE.md").write_text(GUIDE)
    (pathlib.Path(tmp) / "init.d").mkdir(exist_ok=True)
    (pathlib.Path(tmp) / "init.d" / "gpu-fancontrol").write_text("#!/sbin/openrc-run\n")
    r = subprocess.run([sys.executable, str(CHECK), "services", "--inventory", str(path)],
                       capture_output=True, text=True, cwd=tmp)
    return r.returncode, r.stdout + r.stderr


def main():
    fails = []

    def expect_fail(label, inventory, expect, prep=None):
        with tempfile.TemporaryDirectory() as tmp:
            code, out = run(tmp, inventory)
            if prep:
                prep(tmp)
                path = pathlib.Path(tmp) / "services.toml"
                path.write_text(inventory.format(tmp=tmp))
                r = subprocess.run([sys.executable, str(CHECK), "services", "--inventory", str(path)],
                                   capture_output=True, text=True, cwd=tmp)
                code, out = r.returncode, r.stdout + r.stderr
            if code == 0:
                fails.append(f"{label} passed:\n{out}")
            elif expect not in out:
                fails.append(f"{label} failed without naming {expect!r}:\n{out}")

    # The repository as it stands is the check's real subject.
    r = subprocess.run([sys.executable, str(CHECK), "services"], capture_output=True, text=True, cwd=HERE)
    if r.returncode != 0:
        fails.append(f"the repository as it stands was rejected:\n{r.stdout}{r.stderr}")

    def idle_file(tmp):
        (pathlib.Path(tmp) / "init.d" / "idle-daemon").write_text("#!/sbin/openrc-run\n")

    def stray(tmp):
        (pathlib.Path(tmp) / "init.d" / "uninvented").write_text("#!/sbin/openrc-run\n")

    expect_fail("a service file with no inventory entry", GOOD, "uninvented", prep=stray)
    expect_fail("an entry naming no file", GOOD.replace("gpu-fancontrol", "gone-away"),
                "gone-away")
    expect_fail("an entry with no failure mode",
                GOOD.replace('failure = "eend of bmc-fan\'s exit code"\n', ""), "failure")
    expect_fail("a runlevel the install guide disagrees with",
                GOOD.replace('runlevel = "default"', 'runlevel = "boot"'), "runlevel")
    expect_fail("an inventory with no rows", GOOD.split("[[service]]")[0], "zero services")
    expect_fail("a service declared not enabled while the guide enables it",
                GOOD.replace('runlevel = "default"', 'runlevel = "default"\nenabled = false'),
                "lie about the running system")
    # `idle-daemon` is shipped and named by no `rc-update` line, so the only
    # honest states left are "enable it" or "declare why it is not".
    expect_fail("a service nobody enables, declared not enabled with no reason",
                GOOD.replace("gpu-fancontrol", "idle-daemon").replace(
                    'owner = "gpu-stack"', 'owner = "gpu-stack"\nenabled = false'),
                "enabled_reason", prep=idle_file)

    if fails:
        print(f"FAIL {len(fails)}")
        for f in fails:
            print("  " + f)
        return 1
    print("check-services: ok — files and entries are compared both ways, the "
          "runlevel is checked against the line that enables it, and each defect "
          "is named")
    return 0


if __name__ == "__main__":
    sys.exit(main())
