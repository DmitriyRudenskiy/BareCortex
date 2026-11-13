#!/bin/bash
# What is inside a Gentoo amd64 "nomultilib" OpenRC stage3, measured the same way
# as the Alpine minirootfs: published checksum, own file census, own loader asked
# for the graph, and an explicit answer to "is there any 32-bit code in here".
# Unprivileged. See docs/GENTOO-AS-BASE.md.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
B=https://distfiles.gentoo.org/releases/amd64/autobuilds/20261108T170107Z
F=stage3-amd64-nomultilib-openrc-20261108T170107Z.tar.xz
W=$HOME/.cache/bc-gentoo; rm -rf $W; mkdir -p $W/{root,out}; cd $W
curl -sSfL -o $F $B/$F &
curl -sSfL -o $F.sha256 $B/$F.sha256
wait
# The published checksum lives in a PGP clearsigned block; take the hash line.
# The published line is "<hex>  <filename>" inside a PGP clearsigned block.
pub=$(grep -a "  $F\$" $F.sha256 | head -1 | awk '{print $1}')
got=$(sha256sum $F | cut -d' ' -f1)
echo "sha256 published: $pub"
echo "sha256 computed : $got"
[ "$pub" = "$got" ] || echo "CHECKSUM MISMATCH"
echo "tarball: $(du -b $F | cut -f1) bytes"
# Device nodes cannot be created unprivileged; /dev is populated by the init
# system at boot, so excluding it does not change what we are measuring.
tar xf $F -C root --exclude=./dev --exclude=./proc --exclude=./sys
echo "root: $(find root -mindepth 1 | wc -l) paths, $(find root -type f | wc -l) files, $(du -sb root | cut -f1) bytes"
echo "== package database (Gentoo's vdb: /var/db/pkg/<cat>/<pkgver>)"
if [ -d root/var/db/pkg ]; then
  find root/var/db/pkg -mindepth 2 -maxdepth 2 -type d | wc -l | sed 's/^/  categories+packages: /'
  find root/var/db/pkg -mindepth 2 -maxdepth 2 -type d | sed 's|root/var/db/pkg/||' | sort > out/vdb.txt
  head -0 out/vdb.txt
  for p in sys-apps/openrc sys-apps/baselayout sys-apps/portage sys-libs/glibc sys-process/procps sys-apps/util-linux dev-lang/python sys-devel/gcc sys-fs/eudev net-misc/dhcpcd; do
    v=$(grep -m1 "^$p-" out/vdb.txt || echo "$p ABSENT")
    printf "  %-28s %s\n" "$p" "${v#*/}"
  done
  grep -c '^' out/vdb.txt | sed 's/^/  total packages: /'
else
  echo "  no /var/db/pkg"
fi
echo "== 32-bit census: ELF class and machine of every file with an ELF header"
python3 - "$W" <<'PY'
import json, os, struct, subprocess, sys, pathlib, collections
W = pathlib.Path(sys.argv[1]); R = W / "root"; OUT = W / "out"
L = None
for cand in ["lib64/ld-linux-x86-64.so.2", "lib/ld-linux-x86-64.so.2"]:
    if (R / cand).exists(): L = R / cand; break
# Every directory holding a shared object, because a binary's absolute RUNPATH
# (e.g. /usr/lib64/man-db, /usr/lib/gcc/x86_64-pc-linux-gnu/16) resolves against
# the host root when we run a foreign loader, not against the unpacked image.
libdirs = sorted({str(q) for dp, _, fs in os.walk(R) for n in fs if ".so" in n
                  for q in [pathlib.Path(dp)] if n.endswith((".so",)) or ".so." in n})
lp = ":".join([f"{R}/lib64", f"{R}/usr/lib64"] + [d for d in libdirs if d not in (f"{R}/lib64", f"{R}/usr/lib64")])
mach = {3: "EM_386", 8: "EM_M68K", 40: "EM_ARM", 62: "EM_X86_64", 183: "EM_X86_64"}
census = collections.Counter(); elfs = []
for dp, _, fs in os.walk(R):
    for n in fs:
        p = pathlib.Path(dp) / n
        if p.is_symlink() or not p.is_file(): continue
        try:
            h = p.open("rb").read(20)
        except OSError: continue
        if h[:4] != b"\x7fELF": continue
        klass = {1: "32-bit", 2: "64-bit"}.get(h[4], "?")
        e = struct.unpack_from("<H", h, 18)[0]
        t = {2: "exec", 3: "dyn", 1: "rel"}.get(struct.unpack_from("<H", h, 16)[0], "?")
        census[(klass, mach.get(e, f"mach{e}"), t)] += 1
        elfs.append({"path": str(p.relative_to(R)), "class": klass, "machine": mach.get(e, e),
                     "elf": t, "size": p.stat().st_size})
(OUT / "elf-census.json").write_text(json.dumps({"census": {f"{k[0]} {k[1]} {k[2]}": v for k, v in census.items()},
                                                 "files": elfs}, indent=1))
print("  " + ", ".join(f"{k}: {v}" for k, v in census.most_common()))
print(f"  total ELF: {len(elfs)}, bytes: {sum(e['size'] for e in elfs):,}")
for d in ["lib32", "usr/lib32", "libx32", "usr/libx32", "lib/32", "usr/lib32"]:
    if (R / d).exists(): print(f"  MULTIBIT DIR PRESENT: {d}")
print("  multilib dirs: none found" if not any((R / d).exists() for d in ["lib32", "usr/lib32", "libx32", "usr/libx32"]) else "")
if not L:
    print("  NO glibc LOADER FOUND"); sys.exit()
rows, bad = [], []
for e in elfs:
    if e["elf"] == "rel": continue  # ET_REL (.o): not loadable, nothing to resolve
    if e["elf"] == "dyn" and e["class"] != "64-bit": continue
    p = R / e["path"]
    env = dict(os.environ); env.pop("LD_LIBRARY_PATH", None)
    try:
        r = subprocess.run([str(L), "--inhibit-cache", "--library-path", lp, "--list", str(p)],
                           capture_output=True, text=True, env=env, timeout=25)
    except Exception as ex:
        bad.append((e["path"], str(ex))); continue
    need, unres = [], []
    for line in (r.stdout or "").splitlines():
        q = line.split()
        if len(q) >= 4 and q[1] == "=>" and q[2] == "not": unres.append(q[0])
        elif len(q) >= 3 and q[1] == "=>": need.append({"soname": q[0], "resolved": q[2]})
        elif len(q) == 1: need.append({"soname": q[0], "resolved": q[0]})
    row = {"path": e["path"], "elf": e["elf"], "rc": r.returncode, "needed": need,
           "unresolved": unres, "stderr": (r.stderr or "").strip()[:200]}
    rows.append(row)
    if r.returncode != 0 or unres: bad.append((e["path"], (r.stderr or "").strip()[:120] or ",".join(unres)))
(OUT / "ldd.json").write_text(json.dumps(rows, indent=1))
print(f"  loader={L.relative_to(R)} checked={len(rows)} rc0={sum(1 for r in rows if r['rc']==0)} unresolved={sum(1 for r in rows if r['unresolved'])}")
edges = collections.Counter()
for r in rows:
    for n in r["needed"]: edges[n["resolved"]] += 1
print("  top libraries by edges: " + ", ".join(f"{k}={v}" for k, v in edges.most_common(8)))
for p, why in bad[:12]: print(f"    FAIL {p}: {why}")
print(f"  failures: {len(bad)} (of {len(elfs)} ELF; {sum(1 for e in elfs if e['elf']=='rel')} relocatable objects excluded)")
pam = [r["path"] for r in rows if any("libpam" in n["resolved"] for n in r["needed"])]
print(f"  PAM-linked binaries: {len(pam)}")
PY
echo "== what each package owns, from its vdb CONTENTS"
python3 "$HERE/owns.py" "$W"
echo "== where the bytes are"
du -sk root/usr/share/* 2>/dev/null | sort -rn | head -5 | sed "s|^|  |"
for d in usr/lib64/python3.14 usr/lib/python3.14 usr/libexec/gcc usr/lib/gcc var/db/pkg etc/pam.d; do
  [ -e "root/$d" ] && printf "  %-24s %s KB\n" "$d" "$(du -sk root/$d | cut -f1)"
done
echo "== udev / systemd sources"
grep -Ei "udev|systemd" out/vdb.txt | sed "s|^|  |"
echo "== init scripts and services"
for d in etc/init.d etc/runlevels/boot etc/conf.d; do
  printf "  %-18s %s entries\n" "$d" "$(ls root/$d 2>/dev/null | wc -l)"
done
echo "== the biggest things in the image"
du -sk root/* 2>/dev/null | sort -rn | head -6
du -sk root/usr/* 2>/dev/null | sort -rn | head -6
ls out/
