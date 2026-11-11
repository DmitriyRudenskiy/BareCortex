# Proposal: build-orchestration

## Why

The repository contains 25 tracked files and 476 lines of shell. One of them, `scripts/build/prepare_build.sh`, checks that the host has `gcc`, creates `$LFS/sources`, creates an `lfs` user, writes a `chroot` alias, and prints:

```
Host ready.
Export LFS=/mnt/barecortex and continue with LFS chapter 3.
```

That is the entire build system. Between "host ready" and a bootable system sits the pinned base book — **11 chapters, 85 packages** (`~zeckma/openrc/lfs/stable/chapter03/packages.html`: Linux 6.18.10, glibc 2.43, GCC 15.2.0, binutils 2.46.0, OpenRC 0.63, GRUB 2.14) — and none of it exists here as an artifact. There is no cross-toolchain step, no temp tools, no chroot entry, no final system pass, no kernel, no `/etc/fstab`, no bootloader, no first boot. Searching this project's three existing changes for `chroot`, `cross-toolchain`, `partition`, `fstab`, `grub`, `initrd` or `checkpoint` returns nothing.

So the answer to "is it ready to build a working Linux distribution from source" is no, and not because a step is wrong: the step is missing. The consequence of that gap is worse than an unfinished script. A build with no recorded phase boundaries cannot fail well — every failure, including the ordinary one where a package's test suite reports two unexpected failures at chapter 8, is a failure of the whole run, and the only recovery is to start over. That is the specific reason "build it from the book" does not scale to a machine this size, and it is why the book alone is not the artifact and never was.

## What Changes

- A **phase table** becomes the build's spine: named phases mapped to the pinned edition's chapter boundaries, each with entry requirements and **observable exit criteria** (a file exists, a version command prints the pinned version, a test suite reports the count the book states). The book's order is the dependency graph; this change records it and checks it, and does not invent a resolver.
- A **build state record** naming the last phase whose exit criteria passed, written only after they pass. A failed phase leaves the record pointing at the last good phase, and a resumed run starts there.
- **Checkpoints at phase boundaries** — a tar of `$LFS` plus the state record — with a bounded retention list, so "undo the last package" is an operation and not a reformat.
- A **fetch-and-verify step** that reads the package list from `base.lock`, runs once, before any phase, and fails on a checksum mismatch naming the file. No phase reaches the network, and "no network" is a denial mechanism attached to the phase, not the absence of a cable.
- **Destructive phases are a class of their own.** Partitioning, formatting, and writing a bootloader declare their preconditions, list every device and filesystem they will touch, refuse anything outside that list, and can print the whole operation set without performing it. A checkpoint restores a build tree; it does not restore a disk.
- **Every package leaves evidence** — command output, installed version, evaluated criteria — because "the misbuild is named at the package" is a claim about retained output.
- **One build per build root**, enforced by a lock, because two concurrent runs produce a corrupted tree and a state record that honestly says "passed".
- A **check that rejects a citation-free change** to the derived package list, so the rule that every added line names a book revision and page is enforced rather than encouraged.
- The **no-initrd decision is made explicit** and its obligation recorded: with no early userspace, every driver needed to mount root must be built in, and the boot-critical set (`/dev`, kernel, `fstab`, init, bootloader) is verified before GRUB is written. That set is **declared by the target kernel fragment, not enumerated here** — naming a storage symbol for a machine nobody has observed is the kind of invention this project exists to stop — so the check is proven against a synthetic fragment, and the proof says which fragment it was.
- A **first-boot acceptance checklist that is run and recorded**, replacing "reboot and see".
- `prepare_build.sh` stops claiming to be the build: it becomes phase 1 of the orchestrator, and its comment citing "the LFS (OpenRC Edition) book" names the pinned revision.

## Impact

- New capability: `build-orchestration`. New scripts under `scripts/build/`; `prepare_build.sh` is absorbed rather than kept beside them.
- Task-level dependencies, not change-level — two changes each declaring a dependency on the other is a cycle that hides which task unblocks which:
  - `base-system-and-init 3.1` (defines `base.lock` format) blocks `5.1` here; this change's `2.1` (derives its contents) blocks that change's `3.2`
  - `honest-scope-and-config-generation` supplies the target kernel fragment that names the boot-critical symbols; until it exists, the check here is proven against a synthetic fragment and says so
  - This change blocks `userspace-and-ml-stack`, which has nowhere to run until a system exists, and it is where `gpu-stack`'s build steps will be sequenced
- **Precondition:** four tasks are satisfied by running a build, which needs a build host the project has not declared. Group 0 names it or records that it does not exist; until then those tasks are blocked, not complete.
- Nothing in this change needs the target machine. The build runs on a host and a scratch disk; the target host appears only in the first-boot checklist, whose result is recorded there and not asserted here.
- The 85-package list is **derived from the pinned book into `base.lock`**, not typed by hand. A hand-typed copy of the book's package list is a second book, and it drifts.
