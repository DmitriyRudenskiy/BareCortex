#!/usr/bin/env python3
"""test_check_cycle_plan.py — does the cycle-plan check fail on the right thing?

Run: python3 test_check_cycle_plan.py
Exit 0 = the check passes a checkable plan, and fails on prose instead of an
exit command, on an owning change that does not exist, and on a plan with no
cycles rather than passing it vacuously.
"""

import importlib.util
import pathlib
import sys

HERE = pathlib.Path(__file__).parent
from importlib.machinery import SourceFileLoader
SPEC = importlib.util.spec_from_loader(
    "scan", SourceFileLoader("scan", str(HERE / "scripts/build/tests/test-all.sh")))
scan = importlib.util.module_from_spec(SPEC)
sys.modules["scan"] = scan
SPEC.loader.exec_module(scan)

# A plan every cycle in carries its exit command, its host class, and an
# owning change that exists in this tree.
GOOD = """\
# Plan

## Cycle 1 — a bootable base

Exit: `sh scripts/build/build-rootfs.sh status` reporting first-boot passed.

Host: (build)

Owns: `base-system-and-init`

## Cycle 2 — the gate

Exit: the driver loads, `nvidia-smi` exits 0, and a CUDA program completes.

Host: (gpu)

Owns: `nvidia-musl`

## Not a cycle

This section is prose about the invariant and owes the checker nothing.
"""

# The same plan, with the exit command replaced by a sentence.
NO_COMMAND = GOOD.replace(
    "Exit: `sh scripts/build/build-rootfs.sh status` reporting first-boot passed.",
    "Exit: the build eventually gets there, in practice.")

# The same plan, with the owning change renamed to a name that exists nowhere.
NO_OWNER = GOOD.replace("Owns: `base-system-and-init`", "Owns: `base-system`")

# A plan with no cycles at all.
NO_CYCLES = "# Plan\n\n## Not a cycle\n\nProse only.\n"

# A cycle that runs nvidia-smi but does not name (gpu).
SMI_NOT_GPU = """\
## Cycle 3 — the gate

Exit: the driver loads, `nvidia-smi` exits 0.

Host: (build)

Owns: `nvidia-musl`
"""

KNOWN = {"base-system-and-init", "nvidia-musl"}


def main():
    fails = []
    check = lambda label, cond: None if cond else fails.append(label)  # noqa: E731

    check("a checkable plan was rejected", scan.check_cycle_plan(GOOD, KNOWN) == [])
    check("prose instead of an exit command was not caught",
          any("Exit" in f for f in scan.check_cycle_plan(NO_COMMAND, KNOWN)))
    check("an owning change that does not exist was not caught",
          any("Owns" in f for f in scan.check_cycle_plan(NO_OWNER, KNOWN)))
    check("a plan with no cycles passed vacuously",
          scan.check_cycle_plan(NO_CYCLES, KNOWN) != [])
    check("an nvidia-smi cycle marked (build) was not caught",
          any("nvidia-smi" in f for f in scan.check_cycle_plan(SMI_NOT_GPU, KNOWN)))

    if fails:
        for f in fails:
            print(f"FAIL: {f}")
        return 1
    print("ok: the cycle-plan check passes the checkable plan and fails on "
          "prose, a missing owner, no cycles, and a mislabeled (gpu) cycle")
    return 0


if __name__ == "__main__":
    sys.exit(main())
