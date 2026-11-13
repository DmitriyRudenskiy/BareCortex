#!/bin/bash
# What does it cost to take OpenRC, busybox and musl from Alpine instead of
# building them? Asks Alpine's own index for the closure, installs it the way
# Alpine does, then lets musl's loader answer for the graph. Unprivileged: no
# root, no chroot, no container. See README.md; write-up in docs/ALPINE-AS-BASE.md.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
V=3.24.4; A=x86_64; R=v3.24
CDN=https://dl-cdn.alpinelinux.org/alpine/$R
W=$HOME/.cache/bc-alpine; rm -rf $W; mkdir -p $W/{root,apks,out}; cd $W

T=alpine-minirootfs-$V-$A.tar.gz
curl -sSfL -o $T.sha256 $CDN/releases/$A/$T.sha256
curl -sSfL -o $T $CDN/releases/$A/$T
echo "sha256 published: $(cut -d' ' -f1 $T.sha256)"
echo "sha256 computed : $(sha256sum $T | cut -d' ' -f1)"
tar xzf $T -C root
cp root/lib/apk/db/installed out/apk-installed.minirootfs
echo "minirootfs: $(find root -mindepth 1 | wc -l) paths, $(find root -type f | wc -l) files, $(du -sb root | cut -f1) bytes, $(grep -c '^P:' out/apk-installed.minirootfs) packages"

curl -sSfL -o idx.tar.gz $CDN/main/$A/APKINDEX.tar.gz && tar xzf idx.tar.gz -C . APKINDEX
python3 "$HERE/resolve-apk-closure.py" APKINDEX out openrc busybox musl | tee out/closure.txt
echo "== fetching the closure (v3.24 names apks <name>-<version>.apk; arch is in the path, not the name)"
awk '/\.apk$/{print}' out/closure.txt | while read -r f; do
  curl -sSfL -o apks/$f $CDN/main/$A/$f || echo "FETCH FAILED $f"
done
shopt -s nullglob
for a in apks/*.apk; do
  n=$(basename $a .apk)
  tar tzf $a 2>/dev/null | grep -v '^\./\?\.git/' | grep -v '/$' > out/manifest-$n.txt
  printf "  %-34s %8s B  %3s files\n" "$n" "$(du -b $a | cut -f1)" "$(wc -l < out/manifest-$n.txt)"
done
for a in apks/*.apk; do tar xzf $a -C root 2>/dev/null; done
rm -rf root/.git
echo "root after install: $(find root -mindepth 1 | wc -l) paths, $(du -sb root | cut -f1) bytes"

echo "== the graph, from musl's own loader (this is ldd on musl)"
python3 - "$W" <<'PY'
import json, os, struct, subprocess, sys, pathlib
W = pathlib.Path(sys.argv[1]); R = W / "root"; L = R / "lib/ld-musl-x86_64.so.1"
lp = f"{R}/lib:{R}/usr/lib"
def etype(p):
    with open(p, "rb") as f: h = f.read(20)
    return {2: "exec", 3: "dyn", 1: "rel"}.get(struct.unpack_from("<H", h, 16)[0], "?")
rows = []
for dp, _, fs in os.walk(R):
    for n in fs:
        p = pathlib.Path(dp) / n
        if p.is_symlink() or not p.is_file(): continue
        try:
            if p.open("rb").read(4) != b"\x7fELF": continue
        except OSError: continue
        env = dict(os.environ); env.pop("LD_LIBRARY_PATH", None)
        r = subprocess.run([str(L), "--library-path", lp, "--list", str(p)],
                           capture_output=True, text=True, env=env, timeout=30)
        need, unres = [], []
        for line in (r.stdout or "").splitlines():
            q = line.split()
            if len(q) >= 4 and q[1] == "=>" and q[2] == "not": unres.append(q[0])
            elif len(q) >= 3 and q[1] == "=>": need.append({"soname": q[0], "resolved": q[2]})
            elif len(q) == 1: need.append({"soname": q[0], "resolved": q[0]})
        rows.append({"path": str(p.relative_to(R)), "elf": etype(p), "size": p.stat().st_size,
                     "rc": r.returncode, "needed": need, "unresolved": unres,
                     "stderr": (r.stderr or "").strip()[:200]})
(W / "out/ldd.json").write_text(json.dumps(rows, indent=1))
print(f"elf={len(rows)} rc0={sum(1 for r in rows if r['rc']==0)} unresolved={sum(1 for r in rows if r['unresolved'])}")
PY
echo "== the parts of openrc that are not ELF (its scripts and data)"
echo "  openrc tree: $(find root -path '*openrc*' -type f | wc -l) files, $(find root -path '*init.d*' -type f | wc -l) init.d entries"
ls out/
