# Align the build with the build-rootfs file layout

## Why

The build grew organically into ten files with no common layout, and the build
documentation (`docs/build/BUILD-STEPS.md`) describes a phase table instead of
the files themselves. `gbionescu/build-rootfs` provides a canonical minimal
build layout — a handful of named entry scripts, `utils/`, `tests/`, and a
README that documents the build file by file. Aligning to it makes the build
tree self-describing and turns the project requirement — *the project must not
carry build files that do not exist in build-rootfs* — into a machine-checked
invariant instead of a convention.

Reference: `github.com/gbionescu/build-rootfs` at
`c9fa26adeb66e247afbac33510c9479cf4b89f4a` (`master`).

## What Changes

- **BREAKING** (paths only, no behavior change): `scripts/build/` is
  restructured so that every file it carries matches a build-rootfs file by
  name and relative path:

  | current file | becomes | build-rootfs counterpart |
  |---|---|---|
  | `build.py` (1372) + `book.py` (217) + `derive-base-lock.py` (363) + `derive-test-expectations.py` (117) | `build-rootfs.sh` — polyglot shell+python entry; `derive-base-lock` and `derive-test-expectations` become subcommands | `build-rootfs.sh` |
  | `vm-host.sh` (271) | `run-qemu.sh` — renamed, CLI unchanged | `run-qemu.sh` |
  | `test_build.py` (324) + `check-base-lock.py` (177) + `check-no-systemd.py` (156) + `check-services.py` (138) | `tests/test-all.sh` — polyglot; runs every trust check | `tests/test-all.sh` |
  | `check-amd64-only.py` (131) | `tests/test-arch.sh` — polyglot | `tests/test-arch.sh` |
  | `docs/build/BUILD-STEPS.md` (70) | `scripts/build/README.md` — rewritten file by file in the build-rootfs README style; `docs/build/` removed | `README.md` |

  Final build tree: `build-rootfs.sh`, `run-qemu.sh`, `README.md`,
  `tests/test-all.sh`, `tests/test-arch.sh` — five files, all build-rootfs
  names.
- New enforcement in `tests/test-all.sh`: the build fails if any file under
  `scripts/build/` has no same-named counterpart in the pinned build-rootfs
  file list, and if any build file is not documented in
  `scripts/build/README.md`.
- Behavior is preserved: all sixteen trust checks, the phase CLI
  (`pages` / `run` / `resume` / `status`), evidence handling, and the
  `run-qemu.sh` subcommands are unchanged — this is rename, merge, and
  relocate only.
- Root-level `test_*.py` (four files) are updated to invoke the new `.sh`
  entries via subprocess; comment references in `phases.toml`,
  `build-overrides.toml`, `base.lock`, `openrc/services.toml`,
  `class-map.toml`, `README.md`, and the one executable path
  (`build.py:479`) are updated to the new names.
- Unused build-rootfs files (`build_kernel.sh`, `build-initrd.sh`, `type-*/`,
  `utils/*`, `kernel/*`, the remaining `tests/test-*.sh`) stay unused: the
  invariant is one-directional — ours ⊆ theirs.

## Capabilities

### New Capabilities
- `build-file-layout`: the build tree's file set is a subset of the pinned
  build-rootfs file set (same names, same relative paths, one-directional),
  and the build README documents the tree file by file.

### Modified Capabilities
<!-- none: phase behavior, evidence format, and CLI are unchanged -->

## Impact

- Code: all ten files in `scripts/build/` (renamed, merged, or moved), the
  four root-level `test_*.py`.
- Docs: `docs/build/BUILD-STEPS.md` moves to `scripts/build/README.md` and is
  rewritten; comment references in six top-level files.
- Unchanged: `phases.toml` data, `base.lock` contents,
  `build-overrides.toml` semantics, `build-evidence/` format, the LFS disk
  and checkpoints, the build guest's `~/barecortex-vm/vm.sh` name (its copy
  source path is the only thing that moves).
