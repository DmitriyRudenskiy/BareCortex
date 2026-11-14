#!/usr/bin/env python3
"""test_check_amd64_only.py — does the scan fail on the right thing?

Run: python3 test_check_amd64_only.py
Exit 0 = the header classification, the multilib directory rule, the refusal to
scan nothing, and the refusal to pass an unclassifiable object all behave. The
check reads twenty bytes per file, so every case here is a crafted header: no
built image and no binutils are needed to prove the decision. Proving the image
itself passes is a separate thing — `scripts/build/tests/test-arch.sh --root <stage3>` in
`openspec/changes/amd64-only-userspace/tasks.md` 2.3.
"""

import importlib.util
import pathlib
import struct
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).parent
from importlib.machinery import SourceFileLoader
SPEC = importlib.util.spec_from_loader(
    "scan", SourceFileLoader("scan", str(HERE / "scripts/build/tests/test-arch.sh")))
scan = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(scan)


def hdr(klass=2, machine=62, typ=3):
    """A twenty-byte ELF header: e_ident, then e_type and e_machine."""
    return (b"\x7fELF" + bytes([klass, 1, 1, 0]) + b"\0" * 8
            + struct.pack("<HH", typ, machine))


def main():
    fails = []
    check = lambda label, cond: None if cond else fails.append(label)  # noqa: E731

    check("a 64-bit x86_64 dynamic object was misclassified",
          scan.classify(hdr()) == ("ELFCLASS64", "EM_X86_64", "ET_DYN"))
    check("a 32-bit x86 object was not named as one",
          scan.classify(hdr(klass=1, machine=3)) == ("ELFCLASS32", "EM_386", "ET_DYN"))
    check("a relocatable object was skipped instead of classified — crtprec32.o "
          "is ET_REL and the census still has to count it",
          scan.classify(hdr(typ=1))[2] == "ET_REL")
    check("an unknown machine was reported as x86_64",
          scan.classify(hdr(machine=183))[1] == "EM_AARCH64")

    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        (root / "bin").mkdir()
        (root / "bin" / "rc").write_bytes(hdr())
        (root / "bin" / "openrc-run").write_bytes(hdr(typ=2))
        (root / "bin" / "rc.sh").write_text("#!/bin/sh\n")
        elfs, found = scan.scan_root(root)
        check(f"a clean root was reported dirty ({found})", elfs == 2 and found == [])

        (root / "lib" / "ld-linux.so.2").parent.mkdir(parents=True)
        (root / "lib" / "ld-linux.so.2").write_bytes(hdr(klass=1, machine=3))
        _, found = scan.scan_root(root)
        check("a 32-bit ld-linux was missed",
              any("lib/ld-linux.so.2" in f and "ELFCLASS32" in f for f in found))

        (root / "usr" / "lib" / "libfoo.so.1").parent.mkdir(parents=True)
        (root / "usr" / "lib" / "libfoo.so.1").write_bytes(hdr(machine=3))
        _, found = scan.scan_root(root)
        check("a 64-bit-class object for a 32-bit machine was missed",
              any("libfoo" in f and "EM_386" in f for f in found))

        (root / "usr" / "lib" / "truncated").write_bytes(b"\x7fELF\x02\x01")
        _, found = scan.scan_root(root)
        check("an ELF object too short to classify was passed",
              any("truncated" in f and "could not classify" in f for f in found))

        (root / "usr" / "lib32").mkdir(parents=True)
        _, found = scan.scan_root(root)
        check("a lib32 directory was missed",
              any("lib32" in f and "multilib" in f for f in found))

        r = subprocess.run([sys.executable, str(HERE / "scripts/build/tests/test-arch.sh"),
                            "--root", str(root)], capture_output=True, text=True)
        # elf=5: the count is files read, not files cleared — the offenders are
        # in it, which is what makes elf=0 on a real image root the alarm.
        check("a dirty root exited 0",
              r.returncode != 0 and "lib32" in r.stderr and "elf=5" in r.stdout)

    with tempfile.TemporaryDirectory() as tmp:
        # A link is not a second object: counting it would make elf= disagree
        # with the probe's census (1 487 on the stage3, not 1 908).
        links = pathlib.Path(tmp)
        (links / "rc").write_bytes(hdr())
        (links / "rc-alias").symlink_to(links / "rc")
        elfs, found = scan.scan_root(links)
        check(f"a symlink to an ELF file was counted as another object ({elfs})",
              elfs == 1 and found == [])

    r = subprocess.run([sys.executable, str(HERE / "scripts/build/tests/test-arch.sh")],
                       capture_output=True, text=True)
    check("scanning with no --root passed",
          r.returncode != 0 and "no --root" in r.stderr)

    with tempfile.TemporaryDirectory() as tmp:
        empty = pathlib.Path(tmp)
        (empty / "readme.txt").write_text("no binaries here\n")
        r = subprocess.run([sys.executable, str(HERE / "scripts/build/tests/test-arch.sh"),
                            "--root", str(empty)], capture_output=True, text=True)
        check("a tree of no ELF files was not reported as elf=0",
              r.returncode == 0 and "elf=0" in r.stdout)

    if fails:
        print(f"FAIL {len(fails)}")
        for f in fails:
            print("  " + f)
        return 1
    print("check-amd64-only: ok — EI_CLASS and e_machine decide it, a multilib "
          "directory fails the scan, an unclassifiable object is a finding, and "
          "a scan with no image root refuses")
    return 0


if __name__ == "__main__":
    sys.exit(main())
