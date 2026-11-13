#!/usr/bin/env python3
"""Bytes per package in an unpacked Gentoo stage3, from its own vdb record.

/var/db/pkg/<pkg>/CONTENTS lists `obj <path> <md5> <mtime>` and `sym <path> ->
<target>` — it carries no size, so this stats the paths it names. A file listed
but absent is reported rather than silently dropped: that is how a package
record and an image part ways.
"""
import pathlib
import sys

R = pathlib.Path(sys.argv[1]) / "root"
PKGS = ["sys-apps/openrc", "sys-apps/baselayout", "sys-libs/glibc", "sys-apps/portage",
        "dev-lang/python", "sys-devel/gcc", "net-misc/dhcpcd", "sys-apps/systemd-utils",
        "sys-apps/shadow", "sys-libs/pam"]

for p in PKGS:
    # `gcc-*` also matches gcc-config, and Path.glob yields in filesystem order,
    # so require the part after the name to start with a version digit.
    vdb = R / "var/db/pkg"
    # `gcc-*` also matches gcc-config, and Path.glob yields in filesystem order,
    # so require what follows the name to start with a version digit.
    c = [x for x in sorted(vdb.glob(p + "-*/CONTENTS"))
         if x.parent.relative_to(vdb).as_posix()[len(p) + 1:][:1].isdigit()]
    if not c:
        print(f"  {p:<24} absent")
        continue
    objs, syms, total, missing = [], 0, 0, []
    for line in c[0].read_text(errors="replace").splitlines():
        parts = line.split(" ", 1)
        if len(parts) != 2:
            continue
        kind, rest = parts[0], parts[1].rsplit(" ", 2)[0]
        if kind.startswith("obj"):
            objs.append(rest)
        elif kind == "sym":
            syms += 1
    for o in objs:
        q = R / o.lstrip("/")
        if q.is_file():
            total += q.stat().st_size
        else:
            missing.append(o)
    note = f"  [{len(missing)} listed but absent: {', '.join(missing[:3])}]" if missing else ""
    print(f"  {p:<24} {len(objs):5d} files + {syms:4d} symlinks  {total:12,d} bytes{note}")
