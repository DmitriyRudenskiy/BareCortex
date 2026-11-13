## Context

The build is x86_64 and always has been, but the fact lives only in incidental
strings: `phases.toml:80` verifies `x86_64-lfs-linux-gnu-gcc`, `phases.toml:100`
runs `/mnt/lfs/usr/lib/ld-linux-x86-64.so.2`. `README.md`, `docs/PHILOSOPHY.md`,
`docs/INIT-LAYER.md`, `openspec/project.md` and `base.lock` contain no occurrence
of `x86_64`, `amd64`, `64-bit` or `multilib` — verified by grep, and the same grep
finds no `-m32`, `--enable-multi-arch`, or `lib32` anywhere in the build scripts.
So the rule is currently true by construction, unstated, and unenforced.

`scripts/build/check-no-systemd.py` is the precedent this design copies: a claim
in the README, a command behind it that reads the finished image, and a refusal to
run when pointed at nothing. It reads `DT_NEEDED` because the claim is about
resolution; the claim here is about the header itself.

The measurement side is `artifacts/handoff-vm/gentoo-probe/`, run 2026-11-13 on
the Linux host documented in `docs/HANDOFF-VM.md`, against
`stage3-amd64-nomultilib-openrc-20261108T170107Z.tar.xz` (published sha256
`433f0f43db2f3db9515339bca0f2b340650ad87d85ab9d7ec34155b14bcc0228`, equal to the
computed one). It ran unprivileged, beside a build in flight. See
`docs/GENTOO-AS-BASE.md` for the figures and `docs/ALPINE-AS-BASE.md` for the
same method applied to Alpine.

## Goals / Non-Goals

**Goals:**

- One command that fails when the image gains 32-bit code, runnable on the build
  host against an image root, with no exclusion list to rot.
- The 64-bit-only decision written down once, with the measurement that shows it
  costs nothing.
- The stage3 measured well enough that nobody re-measures it to argue the same
  point.

**Non-Goals:**

- Adopting the stage3, or changing `base.lock`, `phases.toml`, or any build script.
- Deciding what the NVIDIA driver installs. `gpu-stack` owns that package; this
  change gives it a check to pass or a rule to reopen deliberately.
- Kernel-space architecture (`CONFIG_64BIT`, `CONFIG_IA32_EMULATION`) — the check
  reads user-visible ELF files; the kernel config is `target-hardware-bring-up`'s
  and is not touched here.

## Decisions

**D1 — Parse the first 20 bytes of the ELF header; do not shell out to `readelf`.**
`EI_CLASS` is byte 4 and `e_machine` is bytes 18–19, both fixed by the ELF
specification, so the whole question is one 20-byte read per file.
`check-no-systemd.py` uses `readelf -d` because it asks a different question —
which libraries a binary names — and that question has no fixed offset. Reading
the header also keeps the check's test free of binutils: `test_check_amd64_only.py`
can craft headers and assert the classification, which is the part worth proving.
Rejected: `readelf -h` per file (1 487 subprocesses on the stage3, and a test that
needs binutils installed to prove a two-field parse).

**D2 — A separate check, not a flag on `check-no-systemd.py`.**
Same walk, different claim, different owning capability (`base-image` versus
`init-system`). One exit code has to mean one thing: `--amd64-only` on the systemd
scan would make a pass ambiguous unless the reader also knew which flags were
passed, and the two claims fail for unrelated reasons. The shared part is six
lines of `is_elf`, which is cheaper to duplicate than to abstract across two
files with different owners.

**D3 — The probe asks the stage3's own loader, and puts every directory holding a
shared object on the library path.**
`ld-linux-x86-64.so.2 --inhibit-cache --library-path … --list` is glibc's `ldd`;
the index and the package database are read only for ownership. The library path
has to include `usr/lib64/man-db`, `usr/lib64/systemd`,
`usr/lib/gcc/x86_64-pc-linux-gnu/16` and the rest, because a binary's absolute
`RUNPATH` resolves against the **host** root when a foreign loader is run — with
only `lib64:usr/lib64` the probe reported 127 "cannot open shared object"
failures that were the probe's bug, not the image's. With the full path: 1 461
loadable files, 1 461 at rc 0, 0 unresolved. Rejected: `chroot` + `ldd` (needs
root, and `/mnt/lfs` may be mid-build; the probe has to run beside it).

**D4 — Relocatable objects are censused but not handed to the loader, and names
are not evidence.**
26 of the 1 487 ELF files are `ET_REL` (gcc's `crt*.o`); asking a loader to load
one is a category error, so they are excluded from that pass and counted in the
census instead — reporting them as failures would teach the reader to ignore the
failure count. `usr/lib/gcc/x86_64-pc-linux-gnu/16/crtprec32.o` is in that list,
and the census classifies it `64-bit EM_X86_64`: it selects a vector width, it is
not 32-bit code. That file is the spec's second requirement in one path, and it
is why the requirement is about headers.

**D5 — Record, do not adopt.**
The stage3 is 1 038 605 377 B unpacked across 292 recorded packages, of which
`sys-devel/gcc` alone is 337 408 853 B, against Alpine's 8 431 459 B and 16
packages. The pinned base stays LFS 13.0-openrc. What the measurement buys is the
rule (a complete OpenRC system needs no 32-bit code) and a price list for the
alternative, including the one item this project has already decided against: 65
binaries resolve `libpam`, among them `usr/libexec/rc/bin/openrc-user` — the
binary Alpine ships as a separate no-PAM package for exactly that reason — and
`usr/bin/supervise-daemon`, while the pinned edition builds OpenRC with
`-D pam=false`.

## Risks / Trade-offs

- **The check passes because it scanned the wrong tree** → it refuses to run
  without `--root`, and prints the ELF file count it read, so `binaries=0` on a
  real image root is itself the alarm. Same stance as `check-no-systemd.py`.
- **A vendor package legitimately needs an i386 compatibility library** (the
  NVIDIA driver ships 32-bit Vulkan/CUDA-client blobs) → the check names the file
  and the fix is the package's install policy. If a needed capability turns out
  to exist only as 32-bit code, that reopens this change's requirement in
  `openspec/`, deliberately, rather than adding an exception to the check.
- **A file with ELF magic that cannot be read is ambiguous** → treated as a
  failure, not a skip: an unreadable file is a file the scan did not clear. This
  makes the check strict on a root with pseudo-filesystems mounted, which is why
  it is pointed at an image root and not at a live `/`.
- **The census counts gcc's `.o` files and inflates the "image" figure** → the
  census reports class/machine/type separately, so `1 487 ELF` and
  `1 461 loadable` are both quotable and neither is the other.
