#!/usr/bin/env python3
"""check-amd64-only.py — is every ELF file in the image 64-bit x86_64?

  check-amd64-only.py --root /path/to/image/root
  check-amd64-only.py --explain        the rules, in words

`base-image`: the image SHALL carry no 32-bit code. Every ELF file under the
image root SHALL be `ELFCLASS64` for the `EM_X86_64` machine, and the image SHALL
NOT contain a `lib32` or `libx32` directory. This is the command behind that
claim; the claim without the command is a build that is 64-bit only for as long
as nobody adds the old libraries.

Two bytes answer the question, and both are at a fixed offset in the first
twenty: `EI_CLASS` at byte 4 (`1` = 32-bit, `2` = 64-bit) and `e_machine` at
bytes 18-19 (`62` = `EM_X86_64`). `readelf` is not consulted — it answers
`check-no-systemd.py`'s question, which is which libraries a binary names, and
that question has no fixed offset. Here a 20-byte read per file is the whole
scan, and the test needs no binutils to prove the classification.

Names are not evidence. `usr/lib/gcc/x86_64-pc-linux-gnu/16/crtprec32.o` in a
Gentoo stage3 is `ELFCLASS64 EM_X86_64`: it selects a vector width, it is not
32-bit code (`artifacts/handoff-vm/gentoo-probe/evidence/elf-census.json`). So
nothing here matches on a filename, and nothing here excludes one either — a
file this scan did not clear is a failure, not an exception, which is why a
vendor package that installs a 32-bit compatibility blob has to stop installing
it rather than be added to a list in this file.

The count is of ELF files read, symlinks included not at all, so it agrees with
`find <root> -type f` and with the probe's census: 1 487 on the stage3 in
`artifacts/handoff-vm/gentoo-probe/`, which is the check's real test.
"""

import argparse
import pathlib
import struct
import sys

ELF_MAGIC = b"\x7fELF"
HEADER_BYTES = 20  # e_ident[16] + e_type + e_machine
EM_X86_64 = 62
MACHINES = {3: "EM_386", 8: "EM_M68K", 40: "EM_ARM", 62: "EM_X86_64",
            167: "EM_PPC64", 183: "EM_AARCH64", 243: "EM_RISCV"}
CLASSES = {1: "ELFCLASS32", 2: "ELFCLASS64"}
TYPES = {1: "ET_REL", 2: "ET_EXEC", 3: "ET_DYN"}
MULTILIB_DIRS = ("lib32", "libx32")

EXPLAIN = """\
Every ELF file under the image root is read for twenty bytes and classified from
EI_CLASS and e_machine; every directory is checked for the name lib32 or libx32.
A file that is not ELF is ignored, a file with ELF magic that cannot be read is a
failure, and no path is ever excluded. A symbolic link is not a second object:
the file it points at is scanned where it lives. Exit is non-zero if any file is
not ELFCLASS64 EM_X86_64 or any multilib directory exists; the count of ELF files
read is printed either way, so a scan of nothing cannot be read as a pass."""


def classify(head):
    """(class, machine, type) from the offsets the ELF specification fixes."""
    return (CLASSES.get(head[4], f"class{head[4]}"),
            MACHINES.get(struct.unpack_from("<H", head, 18)[0],
                         f"EM_{struct.unpack_from('<H', head, 18)[0]}"),
            TYPES.get(struct.unpack_from("<H", head, 16)[0], f"type{head[16]}"))


def scan_root(root):
    """(elf files read, failures) — failures name the path and what it is."""
    elfs, fails = 0, []
    for path in sorted(root.rglob("*")):
        if path.is_dir():
            if path.name in MULTILIB_DIRS:
                fails.append(f"{path.relative_to(root)} is a multilib directory "
                             f"({path.name}); the image has no 32-bit code to hold")
            continue
        if not path.is_file():
            continue
        if path.is_symlink():
            # The object at the other end is scanned where it lives. Counting a
            # link as well would make the count disagree with `find -type f`
            # and with the probe's census, and it would not find anything the
            # target does not already give up: an absolute link inside an image
            # root resolves inside that root once the image is booted.
            continue
        try:
            with path.open("rb") as f:
                head = f.read(HEADER_BYTES)
        except OSError as e:
            fails.append(f"{path.relative_to(root)} could not be read: {e}")
            continue
        if not head.startswith(ELF_MAGIC):
            continue
        elfs += 1
        rel = path.relative_to(root)
        if len(head) < HEADER_BYTES:
            fails.append(f"{rel} has ELF magic and {len(head)} bytes: an object "
                         f"this scan could not classify is not a clean object")
            continue
        klass, mach, typ = classify(head)
        if klass != "ELFCLASS64" or mach != "EM_X86_64":
            fails.append(f"{rel} is {klass} {mach} {typ}; the image is 64-bit "
                         f"x86_64 only")
    return elfs, fails


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", help="the image root to scan, e.g. $LFS")
    ap.add_argument("--explain", action="store_true", help="print the rules and exit")
    a = ap.parse_args()
    if a.explain:
        print(EXPLAIN)
        return 0
    if not a.root:
        print("FAIL no --root: scanning the source tree proves nothing about the "
              "image, and passing here would be a claim about binaries this "
              "command never read", file=sys.stderr)
        return 1

    root = pathlib.Path(a.root)
    if not root.is_dir():
        print(f"FAIL image root is not a directory: {root}", file=sys.stderr)
        return 1

    elfs, fails = scan_root(root)
    for f in fails:
        print(f"FAIL {f}", file=sys.stderr)
    print(f"amd64-only: root={root} elf={elfs} findings={len(fails)}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
