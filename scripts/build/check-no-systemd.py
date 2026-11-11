#!/usr/bin/env python3
"""check-no-systemd.py — does anything in the image resolve a systemd library?

  check-no-systemd.py --root /path/to/image/root
  check-no-systemd.py --explain        the rules, in words

`init-system`: the build SHALL scan the finished image for runtime resolution of
systemd-provided shared libraries and SHALL fail when any is found, naming the
offending binary. Five names are covered: `libsystemd`, `libelogind`,
`libnss_systemd`, `pam_systemd`, and `systemd-hostnamed`-style D-Bus activation
is out of scope because nothing here starts it.

Three ways a library is resolved at runtime, and all three are checked:

  1. `DT_NEEDED` in an ELF binary — `readelf -d`, the dynamic section, nothing
     else. A string match on the file is not this: the pinned edition installs
     `systemd-hwdb` as `/usr/bin/udev-hwdb`, whose string table mentions systemd
     while its dynamic section names only `libudev`.
  2. `pam_*` modules named in `etc/pam.d/*`, which PAM `dlopen`s and no ELF
     header mentions.
  3. NSS modules named in `etc/nsswitch.conf`, resolved by libc at lookup time.

The claim this verifies is "no systemd as init or service manager, no `logind`" —
NOT "no systemd source is compiled". The pinned edition builds udev from
`systemd-259.1.tar.xz` (`chapter08/udev.html`), deliberately, with
`-D link-udev-shared=false` so udev does not resolve `libsystemd-shared`, and
with `-D logind=false -D vconsole=false`. A hit on a udev binary therefore means
that static-link assumption broke, and is reported as such rather than excepted:
the fix is the package's own build option, never an exclusion list.
"""

import argparse
import pathlib
import re
import subprocess
import sys

EXPLAIN = """\
Every ELF file under the image root is asked, via readelf, which libraries it
needs at runtime; etc/pam.d/* and etc/nsswitch.conf are asked which modules they
name. A match on libsystemd, libelogind, libnss_systemd or pam_systemd fails,
naming the file and the library. The command is the verification behind the
README's 'no systemd' claim; the claim without the command is the scenario this
file exists to make impossible."""

SYSTEMD_LIBS = ("libsystemd", "libelogind", "libnss_systemd", "pam_systemd")
# `readelf -d --wide` indents the tag, so the tag is not at the line start.
NEEDED_RE = re.compile(r"\(NEEDED\)\s+Shared library:\s*\[([^\]]+)\]")
ELF_MAGIC = b"\x7fELF"


def is_elf(path):
    try:
        with path.open("rb") as f:
            return f.read(4) == ELF_MAGIC
    except OSError:
        return False


def parse_needed(readelf_out):
    """The sonames in `readelf -d` output — the dynamic section and nothing else."""
    out = []
    for line in readelf_out.splitlines():
        if not (m := NEEDED_RE.search(line)):
            continue
        # Prefix, not equality: Debian's udevd resolves `libsystemd-shared-257.so`
        # and the pinned edition's `-D link-udev-shared=false` exists precisely to
        # stop ours resolving that family. A suffix like `-257` is a distro's, so
        # matching the whole soname would be matching a thing that varies.
        base = m.group(1).split(".so")[0]
        if base.startswith(SYSTEMD_LIBS) or base.endswith("_systemd"):
            out.append(m.group(1))
    return out


def needed_libs(path):
    out = subprocess.run(["readelf", "-d", "--wide", str(path)],
                         capture_output=True, text=True)
    if out.returncode != 0:
        # A file with ELF magic that readelf refuses is a binary this scan did
        # not read, which is not the same as a binary with nothing wrong.
        raise RuntimeError(f"readelf could not read {path}: {out.stderr.strip()}")
    return parse_needed(out.stdout)


def scan_configs(root):
    """Modules named by configuration, which no ELF header mentions."""
    hits = []
    for rel in ("etc/pam.d",):
        d = root / rel
        if not d.is_dir():
            continue
        for f in sorted(d.iterdir()):
            if not f.is_file():
                continue
            for line in f.read_text(errors="replace").splitlines():
                line = line.split("#")[0]
                for lib in SYSTEMD_LIBS:
                    if re.search(rf"\b{lib}\b", line):
                        hits.append((f, f"{lib} (named by PAM config: {line.strip()})"))
    nss = root / "etc/nsswitch.conf"
    if nss.is_file():
        for line in nss.read_text(errors="replace").splitlines():
            if "systemd" in line:
                hits.append((nss, f"libnss_systemd (named by nsswitch.conf: "
                                  f"{line.strip()})"))
    return hits


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

    fails, binaries = [], 0
    for path in sorted(root.rglob("*")):
        if not path.is_file() or not is_elf(path):
            continue
        binaries += 1
        try:
            hits = needed_libs(path)
        except (FileNotFoundError, RuntimeError) as e:
            print(f"FAIL {e}", file=sys.stderr)
            return 1
        for lib in hits:
            udev = "udev" in path.name or "/udev" in str(path.relative_to(root))
            fails.append(f"{path.relative_to(root)} resolves {lib} at runtime"
                         + (" — the pinned edition's `-D link-udev-shared=false` "
                            "assumption broke; that option is the fix, not an "
                            "exception" if udev else ""))

    for path, why in scan_configs(root):
        fails.append(f"{path.relative_to(root)}: {why}")

    for f in fails:
        print(f"FAIL {f}", file=sys.stderr)
    print(f"systemd-scan: root={root} binaries={binaries} findings={len(fails)}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
