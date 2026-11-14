# build-file-layout

## Purpose

Keep the project's build tree (its scripts and its documentation) aligned
file-for-file with the canonical build-rootfs layout, so the build is
self-describing and no build file exists without a build-rootfs counterpart.

## ADDED Requirements

### Requirement: Build file set is a subset of the build-rootfs file set

Every file under the build root `scripts/build/` (excluding
`__pycache__`) SHALL have a same-named counterpart at the same relative path
in the build-rootfs reference file set. The reference set SHALL be the file
list of `github.com/gbionescu/build-rootfs` pinned at commit
`c9fa26adeb66e247afbac33510c9479cf4b89f4a`, carried as an embedded list with
its commit identifier so the check runs without network access. The subset is
one-directional: build-rootfs files the project does not use are allowed to
remain unused.

#### Scenario: A foreign file lands in the build tree

- **WHEN** a file is added under `scripts/build/` whose relative path does not exist in the pinned build-rootfs file list
- **THEN** the build's check suite reports the file by name and fails

#### Scenario: The build tree is aligned

- **WHEN** every file under `scripts/build/` has a same-named counterpart in the pinned build-rootfs file list
- **THEN** the check passes without network access

### Requirement: The build README documents the build tree file by file

`scripts/build/README.md` SHALL contain a section for every file in the build
tree (each section naming the file and giving its usage), and SHALL not
document any file that is not present in the build tree. The check suite
SHALL fail when either direction breaks.

#### Scenario: A build file has no documentation

- **WHEN** a file exists under `scripts/build/` but `scripts/build/README.md` has no section naming it
- **THEN** the check suite reports the file and fails

#### Scenario: The README names a missing file

- **WHEN** `scripts/build/README.md` documents a file that is not present under `scripts/build/`
- **THEN** the check suite reports the file and fails

### Requirement: Build behavior is preserved by the relayout

The relayout SHALL be rename, merge, and relocate only. After it, the build
entry `scripts/build/build-rootfs.sh` SHALL still accept `pages`, `run`,
`resume`, `status`, `derive-base-lock`, and `derive-test-expectations`
subcommands with the same observable behavior; `scripts/build/run-qemu.sh`
SHALL still accept its `seed`, `start`, `wait`, `sh`, `stop`, `grow`, `disk`,
`snapshot`, `rollback`, `prune`, `boot-lfs`, and `console` subcommands; and
every trust check that passed before the relayout SHALL pass after it with
the same result on the same inputs.

#### Scenario: A phase runs through the new entry point

- **WHEN** `scripts/build/build-rootfs.sh run <phase>` is invoked for a phase that passed before the relayout
- **THEN** the same book pages run in the same order and the same evidence records are produced

#### Scenario: The trust checks still pass

- **WHEN** `scripts/build/tests/test-all.sh` is run after the relayout
- **THEN** every check that passed before the relayout passes with unchanged results
