## Why

The build is 64-bit only and says so nowhere. `phases.toml` verifies the toolchain
by the name `x86_64-lfs-linux-gnu-gcc` and the loader by the path
`ld-linux-x86-64.so.2`, but no document, comment, or check states that the image
carries no 32-bit code — so the day someone adds a multilib recipe or drops an
i386 blob from the NVIDIA driver, nothing fails, and the first anyone learns is a
loader error at boot.

Gentoo's `stage3-amd64-nomultilib-openrc` is the existence proof that the rule
costs nothing: a complete, bootable OpenRC system in which **0 of 1 487 ELF files
are 32-bit**. Measuring it with the same loader-graph method as
`alpine-as-base` also prices what a distro's OpenRC base drags in — a 337 MB
compiler, PAM in 65 binaries where the pinned LFS edition builds OpenRC with
`-D pam=false`, and a package manager — which is the other half of why we build
the base instead.

## What Changes

- New `scripts/build/check-amd64-only.py`: walks an image root, reads `EI_CLASS`
  and `e_machine` from every ELF header, and fails on any ELFCLASS32 file, any
  non-x86-64 machine, or any `lib32`/`libx32` directory. Same shape and CLI as
  `scripts/build/check-no-systemd.py`, including refusing to scan nothing.
- New `test_check_amd64_only.py`, in the style of `test_check_no_systemd.py`:
  the classification is proven on crafted headers, so the rule is testable
  without a built image.
- New `artifacts/handoff-vm/gentoo-probe/` — `probe.sh`, `owns.py`, and the raw
  output of the run it produced (published checksum, ELF census, loader graph over
  all 1 461 loadable files, per-package byte totals from the stage3's own vdb).
- New `docs/GENTOO-AS-BASE.md` — the measurement, side by side with
  `docs/ALPINE-AS-BASE.md`, and what it says about the pinned base.
- The pinned base does not change. `base.lock`, `phases.toml`, and every build
  script are untouched; the stage3 is measured, not adopted.

## Capabilities

### New Capabilities

- `base-image`: already introduced by the in-flight `alpine-as-base` change
  (`openspec/specs/` is empty, so both changes carry it as a delta). This change
  adds two requirements to it: the image carries no 32-bit code, and an
  architecture claim is read from a binary header rather than from a label.

### Modified Capabilities

None — no existing spec's requirements change.

## Impact

`scripts/build/` gains one check and one test; `docs/` and `artifacts/` gain the
record. The check runs on the build host against the assembled image root, the
same place `check-no-systemd.py` runs (`docs/INIT-LAYER.md`, `docs/PHILOSOPHY.md`),
and `README.md` gains the claim its command backs. No runtime component, no
kernel or driver change: the NVIDIA driver's own 32-bit compat blobs are the
first real test of this rule, and `gpu-stack` owns that package.
