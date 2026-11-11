# Design: build-orchestration

## Context

`scripts/build/prepare_build.sh` is 62 lines and stops at LFS chapter 2: verify host tools, `mkdir $LFS/sources`, `useradd lfs`, write a `chroot` alias, print "continue with LFS chapter 3". Nothing else in the repository builds anything. The three existing changes specify honesty of claims (`honest-scope-and-config-generation`), what is inherited and how it is pinned (`base-system-and-init`), and what the GPU stack is (`gpu-stack`). None of them sequences a build, and a grep across all three for `chroot`, `cross-toolchain`, `partition`, `fstab`, `grub`, `initrd`, `checkpoint` returns zero hits.

The pinned base book (`~zeckma/openrc/lfs/stable/`, Version 13.0-openrc) is 11 chapters. Read from its own table of contents: chapter 4 prepares the chroot, chapter 5 builds binutils-pass1, gcc-pass1, linux-headers and glibc, chapter 6 builds 17 packages inside the chroot including gcc-pass2 (18 table-of-contents entries, one of which is the chapter introduction), chapter 7 handles kernfs/util-linux/createfiles and entering the final system, chapter 8 builds **84 package pages** of final system software including OpenRC itself, chapter 10 is kernel, `fstab` and GRUB, chapter 11 is the reboot. Its package list pins Linux 6.18.10, glibc 2.43, GCC 15.2.0, binutils 2.46.0, OpenRC 0.63, GRUB 2.14, OpenSSL 3.6.1. (Two counts appear in this change and both are correct: the book's package list has **85 packages**, and chapter 8 has **84 package pages** — a handful of packages are built as passes in chapters 5–7 rather than in chapter 8.)

Two things found in that table of contents are worth recording before anyone designs around them:

- **§8.2 "Package Management" is guidance that recommends nothing.** It states outright that the section "will not talk about nor recommend any particular package manager", lists techniques — including one titled "It is All in My Head!" — and lists upgrade issues. It also states the kernel/glibc upgrade rules: a kernel upgrade needs no rebuild of anything else, a glibc upgrade needs extra steps.
- **§8.55 "Packaging-26.0" is not package management.** It is the Python `packaging` library (PEP 440 version handling), built with `pip3 wheel` and installed `--no-index --find-links dist`. A grep by name finds it first. The base book already builds Python 3.14.3 and installs Python packages from locally built wheels with the network off, which is the pattern the ML stack will extend rather than replace.

## Goals / Non-Goals

**Goals**

- A build that has phases, knows which phase it finished, and resumes there instead of starting over.
- Exit criteria that are observable, so a package that misbuilt is named at the package that misbuilt.
- Sources fetched and verified once, before any phase, with no network during a phase.
- The boot-critical set verified before the bootloader is written.
- A first boot that is checked against a list and recorded.

**Non-Goals**

- A package manager. The book's §8.2 is a whole section explaining why that is a subject of its own with no single answer; this change is not it.
- A dependency resolver. The book's order is a reviewed dependency graph, reviewed once per pin bump.
- A new base, a new init, or a rebuild of the book's instructions. The book is upstream; the orchestrator drives it.
- Anything requiring the target machine. The build runs on a host and a scratch disk.

## Decisions

### D1 — The orchestrator sequences, records, and verifies. It resolves nothing

Three verbs, and the boundary matters. It reads the phase table, runs the book's step, checks the exit criteria, writes the record. It does not compute an install order, does not decide what depends on what, and does not grow a query language.

The alternative — a real package manager — is what the book declines to specify in §8.2, and the reason it gives is that every technique has a drawback worth arguing about. Building one here would be a second project whose first user is us. What this change does own is the *record*: which phase completed, which package produced which files (the `base.lock` mechanism in `base-system-and-init` D3), and where the run can be resumed from.

### D2 — A phase is a chapter boundary with exit criteria that can be observed

Phases are named after the pinned edition's boundaries — host-prep, partitions-and-filesystems, cross-tools, temp-tools, chroot-entry, toolchain-pass, config-files, system-pass, openrc-and-services, kernel, boot-config, first-boot — and each carries entry requirements plus exit criteria of a form a machine can evaluate: a file exists, `ldd` resolves, `pkg --version` prints the pinned version, a test suite reports the counts the book states for that package.

The test counts are the cheap half of this and the part usually skipped. The book prints expected numbers for packages that have suites; recording them costs a table and moves a misbuild's diagnosis from "the system does not boot, chapter 6 onward is suspect" to "binutils pass 2 reported 3 unexpected failures where the book says 0". A phase whose exit criteria were not evaluated SHALL NOT be recorded as passed — a phase that reports success without having done the work is the failure mode already catalogued twice in this project, in `merge_config.sh` and in the OpenRC scripts.

### D3 — State is written after the criteria pass; checkpoints make a phase undoable

The state record names the last phase whose exit criteria passed, plus the artifacts that phase produced. It is written *after* they pass, never before, so a crash mid-phase leaves the record pointing at the last good phase and a resumed run repeats only the failed one.

At each boundary the orchestrator takes a checkpoint: a tar of `$LFS` plus the state record, with a bounded retention list so the checkpoint set cannot fill the build disk. This is the mechanism the external 14-cycle plan had and this repository does not, and it is the difference between "rebuild the last package" and "rebuild the system". The medium (tar, or a filesystem snapshot where the host filesystem supports it) is OQ2; the obligation to have one is not optional.

### D4 — No initrd is a decision, and it has one consequence that must be checked

There is no initrd and none is planned; the base book does not build one. That is a legitimate choice for this machine, and it is not free: with no early userspace, **every driver needed to mount the root filesystem must be built into the kernel**, not modular. A storage controller built `=m` with no initrd is an unbootable system whose kernel config looks fine.

So the obligation is recorded here and the symbols live in `honest-scope-and-config-generation`'s target fragment (which already gained the storage-controller requirement), and the check has a shape even though its inputs need hardware: for each symbol in the declared boot-critical set, `=m` fails the build and names the symbol. **The set is a named set supplied by the target fragment, not a list written here** — the storage controller is a property of a machine this project does not have, and this project does not invent facts about machines it does not have. The check is therefore proven against a synthetic fragment whose symbols the check knows, and that proof is what gets recorded; the real fragment arrives with `target-hardware-bring-up`.

Before GRUB is written, the boot-critical set is verified as a set: kernel image present; the modules tree either installed and non-empty, or absent with an explicit recorded decision naming why; `/etc/fstab` naming a root that appears in the partition table recorded by the partitioning phase; `/dev` populated; init executable. The cheapest moment to discover a missing root driver is before it is called installed.

### D5 — Sources are fetched once, verified, and cached; no phase touches the network

A fetch step reads `base.lock`, downloads what is missing into a cache, verifies every checksum, and fails naming the file. Phases then run with network access **denied by a mechanism** — a network namespace via `unshare -n` is the candidate, and the alternative is whatever the build host supports — because "the network was off" is a story about a cable while "the phase could not reach the network" is a property of the phase. What the denial costs is a task, not an assumption.

This is the air-gap default the external plan had, and it is also what makes a resumed run deterministic: a phase cannot silently pick up a different tarball than the one the record was written against. A source named by a build step but absent from `base.lock` fails before anything is downloaded, which is the same rule `gpu-stack` D1 applies to binaries.

The trust chain stops at the checksum and says so: checksums come from the pinned book over HTTPS, and no signing-key verification is performed. That is a stated exposure, not an oversight — the threat this defends against is a corrupted cache or a mismatched pin, not a compromised upstream, and defending against the latter is a separate decision with a key-distribution cost nobody has agreed to pay yet.

### D6 — The reproducibility claim is bounded, and the bound is stated

What this change can deliver is: same `base.lock` and same host toolchain, same package set, same configuration, same recorded phase results. It does **not** deliver bit-for-bit reproducible artifacts, and the documentation SHALL NOT imply it. Bit-for-bit needs at minimum a pinned host toolchain hash, pinned build paths, `SOURCE_DATE_EPOCH` discipline, and a rebuild-and-compare harness — none of which is cheap, and none of which is claimed here. The honest sentence is "rebuilding from this manifest produces the same system" and the sentence that is not said is "produces byte-identical output".

### D7 — The orchestrator is shell

It runs before any toolchain exists, on a host this project does not choose, and it must survive being read by someone standing in a chroot at 2am. A Rust orchestrator would have to be compiled by the toolchain it orchestrates, or shipped as a prebuilt binary — which is the language policy applied backwards, since there is nothing to port and a bootstrap problem to acquire instead. Shell, `set -eu`, and no cleverness. Where a step needs real data structures it reads a table; the table is data, the driver stays small.

### D8 — The package list is derived from the pinned book, not typed into this repository

The 85-package list, its versions, and its order come from the pinned edition at a recorded revision, extracted into a data file with the revision recorded in the file. A hand-maintained copy of the book's package list is a second book, and it drifts on the day the book updates. A pin bump then produces a reviewable diff of the derived file, which is the review that matters.

Naming caution, learned from the book's own table of contents: §8.55 "Packaging-26.0" is a Python library and must not be mistaken for package management, and §8.2 is the package-management discussion that recommends nothing.

**Ownership, settled.** `base-system-and-init` defines the format of `base.lock`; this change derives its contents from the pinned book. One file, one truth. The earlier state — this change extracting a package list while that change populated a manifest from the same book, each declaring a dependency on the other — was a cycle at change level and a second book at file level, and it is the reason the split is by artifact (format vs derivation) rather than by change.

### D10 — Destructive phases are a separate class, and they are the only irreversible thing here

Partitioning, formatting, and writing a bootloader are the only steps in this project that cannot be undone by a checkpoint: a checkpoint restores the build tree, not the disk that was reformatted beside it. So they get their own requirement — declared preconditions, an explicit list of every device and filesystem to be touched, refusal when the request touches anything outside it, and a dry run that prints the whole set of operations without performing any.

The existing `prepare_build.sh` is the warning. It is absorbed by task 8.1, and it writes `/etc/sudoers.d/lfs_build` granting `lfs ALL=(root) NOPASSWD: SETENV, NOEXEC_ALL` — passwordless root on whatever machine it runs on, which is the LFS book's own convention and is acceptable exactly under one condition: **the build host is disposable**. That condition is now a precondition of this change rather than an assumption, and the sudoers line is recorded as scoped to it. Absorbing the script without saying so would have imported a host-trust decision by copy-paste.

### D11 — Evidence and exclusivity, the two things a build needs before it needs features

Two small obligations that are cheap now and unrecoverable later. **Evidence:** every package leaves its command output, installed version, and evaluated criteria behind, because D2's promise — a misbuild named at the package rather than suspected from chapter 6 onward — is unfalsifiable without the output, and cannot be reconstructed for phases already built. **Exclusivity:** the build root is locked, and a second invocation fails naming the holder, because two concurrent runs produce a corrupted tree and a state record that honestly says "passed".

### D9 — Boundaries with the other changes

`base-system-and-init` defines the format of `base.lock` and owns the pin; this change derives its contents and adds sequencing and state — the split is by artifact, because each change claiming to own the package list is how a second book gets written. `honest-scope-and-config-generation` owns the kernel fragment and the boot parameters; this change adds the built-in-root obligation and runs the check against a synthetic fragment, since the real one needs a machine. `gpu-stack` owns what the GPU stack is; its installation steps become phases here. `userspace-and-ml-stack` is a phase sequence over this spine, and `maintenance-and-updates` consumes its output: the checkpoints this change produces are the rollback mechanism that change documents, and `base.lock` derived here is the ledger that change adds fields to. Anything that needs the target machine belongs to `target-hardware-bring-up`, including the first-boot checklist's *result* — the checklist itself is written here, and its outcome is recorded there.

## Preconditions

Four tasks in this change are not writing: 4.3, 4.5, 5.3, and 6.5 are satisfied by **running** a build, which needs a build host, tens of gigabytes, and hours. That is not the target machine — the scope note in `tasks.md` holds — but it is a machine, and the project has not declared one. Group 0 settles that before group 3, instead of letting it surface on the third day of implementation as a surprise.

## Risks / Trade-offs

- **The book drifts under the pin.** The OpenRC edition lives in a personal directory with no release cadence. Mitigated by D8's derived file: drift becomes a diff, and a pin bump is a decision rather than an accident.
- **Exit criteria get skipped to save time.** Test suites are slow and the temptation is to mark phases passed. Mitigated by the record being written only after evaluation, so a skipped suite is visible as a phase that cannot complete.
- **The orchestrator becomes a distro by accretion.** Every convenience added to it is a step toward the package manager this change declines to build. The three verbs in D1 are the test: if a new feature does not sequence, record, or verify, it is not a feature of this change.
- **Checkpoints eat the build disk.** Bounded retention, and the retention count is a recorded number rather than "keep everything".
- **Chroot environment leaks.** The book's chroot invocation is an explicit clean environment; a driver that exports extra variables produces a system that builds differently than the book's. The environment given to the chroot is recorded and minimal.

## Open Questions

- **OQ1 — the exact temp-tool sequence in this edition.** Chapter 5's pages are binutils-pass1, gcc-pass1, glibc, linux-headers; gcc-pass2 and binutils-pass2 are chapter 6 pages. Confirm the order and the pass structure from the edition's own pages rather than from memory of older LFS releases, and record it as the phase table's first rows.
- **OQ2 — checkpoint medium.** `tar` works everywhere; a filesystem-level snapshot is faster and needs the host filesystem to support it. Decide from the build host, record which was used, and keep the tar path working as the fallback.
- **OQ3 — default test-suite policy.** The book has a chapter on running test suites and states expectations per package. Running everything is slow; running nothing loses D2's main benefit. Candidate default: run suites for the toolchain and glibc, and for anything a later phase's exit criteria depend on.
- **OQ4 — where the phase table lives.** Data file in this repository versus generated from the book at build time. D8 decides the package list; the phase table is smaller and more opinionated, and may want to stay hand-written with the package list generated into it.
