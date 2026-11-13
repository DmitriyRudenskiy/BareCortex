# Alpine probe: what "take the three from Alpine" costs

Measures the size and the dependency graph of taking OpenRC, busybox and musl
from Alpine v3.24 instead of building them in LFS. `docs/ALPINE-AS-BASE.md` is
the write-up; this is the machinery and the evidence.

## Run it

    ./probe.sh          # on an x86_64 Linux box, unprivileged, ~40 s, ~12 MB of downloads

No root, no chroot, no container: the tarball is unpacked into
`$HOME/.cache/bc-alpine/root`, the apks are untarred over it, and the graph is
produced by musl's own loader (`--library-path <root>/lib:<root>/usr/lib --list`),
which is what `ldd` is on musl and resolves exactly the way the compiled binaries
will. Output lands in `out/`.

To re-resolve the closure against a different Alpine release or seed set:

    ./resolve-apk-closure.py APKINDEX out openrc busybox musl

It reads the index's `D:` (depend) and `p:` (provides, including the `so:`
virtuals) and reports the closure, its declared sizes, and every dependency the
index resolves ambiguously.

## Evidence, and what each file settles

| file | settles |
|---|---|
| `evidence/apk-installed.minirootfs` | the 16 packages the tarball ships, with versions and owners |
| `evidence/closure.json` | the 7-package closure, its edges, and the three ambiguous virtuals |
| `evidence/ldd.json` | 84 executables, their ELF type, every soname and where the loader resolved it |
| `evidence/manifest-<pkg>.txt` | which files each apk owns — how the graph nodes get attributed to packages |

Re-measured 2026-11-13 against `v3.24/main`. If Alpine moves, the sha256 of the
minirootfs in `probe.sh` output changes and every byte figure here is stale —
rerun rather than edit.
