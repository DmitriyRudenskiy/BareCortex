# Gentoo probe: what a complete OpenRC system contains, and whether 32-bit code is any of it

Measures `stage3-amd64-nomultilib-openrc` — its size, its ELF census by class and
machine, its dependency graph, and what each package owns.
`docs/GENTOO-AS-BASE.md` is the write-up,
`openspec/changes/amd64-only-userspace/` the decision record; this is the
machinery and the evidence.

## Run it

    ./probe.sh        # x86_64 Linux, unprivileged, ~2 min, 260 MB downloaded, ~1 GB unpacked

No root, no chroot, no container. The stage3 is unpacked into
`$HOME/.cache/bc-gentoo/root` with `/dev`, `/proc` and `/sys` excluded — device
nodes cannot be created unprivileged, and the init system populates them at boot,
so excluding them does not change what is measured. Then:

- every file with an ELF header is classified from bytes 4 and 18–19
  (`EI_CLASS`, `e_machine`) — this is the 32-bit census, and it needs no toolchain;
- every loadable ELF file is resolved by the stage3's own
  `ld-linux-x86-64.so.2 --inhibit-cache --library-path <every dir holding a .so>
  --list`, which is what `ldd` is on glibc;
- `owns.py` reads `/var/db/pkg/<pkg>/CONTENTS` and stats the paths it names,
  because that record stores an md5 and an mtime per file and no size.

**The library path is the whole trick.** A binary's absolute `RUNPATH`
(`/usr/lib64/man-db`, `/usr/lib64/systemd`, `/usr/lib/gcc/x86_64-pc-linux-gnu/16`)
resolves against the host root when a foreign loader runs, so a short
`--library-path` produced 127 "cannot open shared object" failures that were the
probe's bug. If you change this script, the number to watch is `failures:` — it
must stay 0 for reasons that are not the probe.

## Evidence, and what each file settles

| file | settles |
|---|---|
| `evidence/probe.log` | published vs computed sha256, sizes, path counts, the census line, the loader's counts, per-package bytes, the biggest directories |
| `evidence/elf-census.json` | every ELF file with its class, machine, type and size — the proof that 0 of 1 487 are 32-bit, including `crtprec32.o`, which is not |
| `evidence/ldd.json` | all 1 461 loadable files, their sonames, where each resolved, exit status, stderr |
| `evidence/vdb.txt` | the 292 packages the stage3 records, with versions |

Re-measured 2026-11-13 against the `20261108T170107Z` autobuild, whose sha256 the
probe prints against the published one. Gentoo dates its autobuilds, so the next
one is a different file under a different name; if its checksum differs, every
byte figure in `docs/GENTOO-AS-BASE.md` is stale. Rerun rather than edit.
