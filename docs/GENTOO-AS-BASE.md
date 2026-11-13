# Gentoo's nomultilib OpenRC stage3, and 64-bit only

The two questions this answers: what does a *complete* OpenRC system from a
distro actually contain, once Alpine's 8 MB minirootfs is no longer the only
alternative anyone has measured — and does running the image 64-bit only, with no
32-bit compatibility code at all, cost anything?

Every number here was measured on 2026-11-13 by
`artifacts/handoff-vm/gentoo-probe/probe.sh` on the x86_64 host documented in
`docs/HANDOFF-VM.md`, unprivileged, beside an LFS build in flight. Raw output is
in `artifacts/handoff-vm/gentoo-probe/evidence/`. The graph is not read out of
package metadata: it is what the stage3's own `ld-linux-x86-64.so.2` answered when
asked to resolve every loadable file in it.

## The input

`stage3-amd64-nomultilib-openrc-20261108T170107Z.tar.xz`

| | |
|---|---|
| sha256 | `433f0f43db2f3db9515339bca0f2b340650ad87d85ab9d7ec34155b14bcc0228` (published == computed) |
| tarball | 260 157 720 B |
| unpacked | 1 038 605 377 B, 61 480 paths, 49 626 files |
| packages recorded | 292, in `/var/db/pkg` |
| OpenRC | 0.63.3, glibc 2.43-r4, gcc 16.2.1_p20260926, portage 3.0.83-r1, python 3.14.10_p1 |

`/var/db/pkg/<cat>/<pkg>/CONTENTS` is Gentoo's ownership record: it names every
path a package installed, which is the thing `docs/ALPINE-AS-BASE.md` found Alpine
keeping in `/lib/apk/db/installed` and an LFS build has no equivalent of. The
bytes below are that record's file list, stat'ed.

## The 32-bit census, which is the reason for the visit

Every file with an ELF header, classified by `EI_CLASS` and `e_machine`:

```
64-bit EM_X86_64 dyn: 1 461     64-bit EM_X86_64 rel: 26
32-bit: none                    lib32/, usr/lib32/, libx32/, usr/libx32/: none
```

**Zero.** A complete, bootable OpenRC system — init, getty, login, shadow, udev,
dhcpcd, a C compiler, a package manager, 292 packages — contains no 32-bit object
anywhere. Dropping 32-bit support is not a compromise that has to be paid for
later; it is what a 64-bit OpenRC system already looks like when nobody adds the
old libraries.

One file makes the second half of the point.
`usr/lib/gcc/x86_64-pc-linux-gnu/16/crtprec32.o` is in that census as
`64-bit EM_X86_64`: it selects a vector width, it is not 32-bit code. Names are
not evidence, and `nomultilib` in a filename is not evidence either — the census
is, which is why `openspec/changes/amd64-only-userspace/` writes it as a rule.

## The graph, as the loader answered it

`ld-linux-x86-64.so.2 --inhibit-cache --library-path <every dir holding a .so>
--list <file>`, run over all 1 461 loadable files: **1 461 at exit status 0, zero
`not found`.** The 26 `ET_REL` objects (gcc's `crt*.o`) are counted, not handed to
a loader that cannot load them.

By edge count: `libc.so.6` 1 444, `ld-linux-x86-64.so.2` 736, `libm.so.6` 147,
`libxtables.so.12` 100, `libz.so.1` 93, `libpam.so.0` 65, `libeinfo.so.1` 61,
`libgcc_s.so.1` 58.

The `--library-path` is not decoration. A binary's absolute `RUNPATH`
(`/usr/lib64/man-db`, `/usr/lib64/systemd`, `/usr/lib/gcc/x86_64-pc-linux-gnu/16`)
resolves against the **host** root when you run a foreign loader, so pointing it
at `lib64:usr/lib64` alone reports 127 "cannot open shared object" failures that
belong to the probe and not to the image. The first run of the script did exactly
that and produced a graph with 127 holes in it.

## What a distro's OpenRC brings along

| | Alpine minirootfs 3.24.4 + closure | Gentoo stage3 nomultilib-openrc |
|---|---|---|
| unpacked | 9 859 629 B, 686 paths | 1 038 605 377 B, 61 480 paths — **123×** |
| packages | 16 | 292 |
| ELF objects | 84 | 1 487 |
| libc | musl, 670 312 B | glibc, 52 849 340 B across 1 509 files — **79×** |
| C compiler | absent | `sys-devel/gcc` 337 408 853 B — **32.5% of the image** |
| package manager | apk + TLS libs, 9 859 629 B | portage 27 406 462 B + python 59 471 011 B |
| PAM | none; OpenRC is built with PAM off | **65 binaries resolve `libpam.so.0`**, `sys-libs/pam` 2 668 257 B, `/etc/pam.d` 96 KB |
| udev | eudev | `sys-apps/systemd-utils` 260.1-r1, 18 689 277 B |
| OpenRC itself | 171 paths, 1.43 MB, 41 `/etc/init.d` | 214 files + 100 symlinks, 2 072 578 B, 51 `/etc/init.d`, 19 in the boot runlevel |
| documentation | effectively none | `/usr/share` 354 996 KiB: locale 81 036, sgml 78 028, doc 41 544, man 39 760 (`du -sk`; block counts move a few KiB between runs) |

OpenRC itself is the same size in both, and its core is as clean here as it is on
Alpine: `openrc-init` resolves `librc.so.1` and `libc`, `agetty` resolves `libc`
and nothing else. What differs is everything OpenRC is shipped *next to*.

Three of those rows are decisions this project has already made, and the stage3
contradicts all three:

- **PAM.** The pinned LFS edition builds OpenRC with `-D pam=false` and installs
  no PAM. Here `usr/libexec/rc/bin/openrc-user` — the very binary Alpine splits
  out as a separate no-PAM package for this reason — resolves `libpam.so.0`, as
  does `usr/bin/supervise-daemon`, and OpenRC ships `usr/lib64/security/pam_openrc.so`,
  which links back into `librc.so.1`. Taking this base means taking PAM or
  relinking 65 binaries.
- **systemd.** `scripts/build/check-no-systemd.py` fails the build when any binary
  resolves a systemd library at runtime, and it reads `DT_NEEDED` rather than
  strings because udev is built from the systemd tarball on purpose. **12 binaries
  in this stage3 resolve `libsystemd-shared-260.so`** — `udevadm`, `systemd-hwdb`,
  `systemd-tmpfiles`, and the `usr/lib/udev/*.id` helpers. The README's
  "no systemd as init, udev still comes from the systemd tarball" is exactly the
  distinction that measurement exists to keep honest, and this is what the
  distro's side of that distinction costs: 18 689 277 B and 12 build failures.
- **A compiler in the base.** 337 MB of gcc 16 in an image whose job is to run one
  training stack. `image-minimisation` is the change that decides what the
  finished image may contain; this is a floor under what a distro seed starts with.

## What this does not say

It is not a proposal to move off LFS, and the stage3 is not a candidate: 1.04 GB
and 292 packages against Alpine's 8.4 MB and 16, with a compiler and a PAM story
already in the way. The base stays LFS 13.0-openrc and `base.lock` does not change.

What it leaves behind is one rule with a command behind it — the image carries no
32-bit code, `scripts/build/check-amd64-only.py` fails when it does — and the
measurement that shows the rule is free. The first real test of it will be the
NVIDIA driver, which `gpu-stack` owns and whose 32-bit behaviour this
measurement did not examine — the check is what will say whether it installs any
32-bit code; if something
we need turns out to exist only as 32-bit code, that reopens the rule in
`openspec/` on purpose rather than by an exception quietly added to a check.
