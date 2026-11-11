# Tasks: build-orchestration

Scope note: nothing here needs the **target** machine. The only task touching that host writes a checklist and records that running it belongs to `target-hardware-bring-up`.

Precondition note: four tasks are satisfied by **running** a build, not by writing about one — 4.3, 4.5, 5.3, and 6.5. Those need a build host, tens of gigabytes, and hours. The project has not declared one, so group 0 settles that before group 3, and every run task is marked **(run)**. A run task with no host is blocked, not done.

## 0. Preconditions, before any of this is written

- [x] 0.1 **Named 2026-11-11: a KVM guest, disposable by construction — `scripts/build/vm-host.sh`.** Debian 13.7, kernel 6.12.115-cloud-amd64, x86_64, 8 vCPU, 8 GB, guest root grown from its 24 G image to 64 G virtual with **59 G free** — `vm-host.sh grow 64G` does the qcow2, the partition table and the filesystem in one command, passwordless sudo, no GPU. Its whole disk is a qcow2 inside the substrate's `$HOME`, so the uninstall is `rm -rf` and a reset is one `cp`. Reached with `ssh -J user@192.168.0.86 -p 2222 bct@127.0.0.1`. **The substrate is recorded too, because it is not disposable and says so:** Pop!_OS 24.04, 16 cores, 46 GB with 28 resident and load average 8, a 512 GB NVMe whose 468 G root partition read **94 % — 29 G free** when first measured and **82 % — 79 G free** after the operator freed space the same day, no passwordless sudo, five RTX 4060 Ti held by another user's inference processes. Two consequences are structural, not incidental: the guest's disk ceiling is the substrate's free space, which is a knob and not a wall (`grow`, and the qcow2 allocates on demand — so 64 G virtual against 79 G free leaves the host 15 G of headroom and nothing more), and it cannot see a GPU, because passthrough needs root — so `(gpu)` work stays on the substrate and `(build)` work happens in the guest
- [ ] 0.2 Record the disk and time budget for one full build plus the checkpoint retention of 4.4, from the book's own per-package disk figures where it states them, and state what happens when the disk fills mid-phase. **Measured so far:** the chapter 2.4 toolchain inside the guest — `apt-get install build-essential bison m4 gawk bc texinfo libtool autoconf automake pkg-config curl vim` — **1 m 58 s** on 8 vCPU, yielding gcc 14.2.0 and GNU Make 4.4.1. Disk, priced as far as it can be priced today. **From the book:** LFS 13.0-openrc `chapter02/creatingpartition.html` states "a minimal system requires a partition of around 10 gigabytes (GB)" and "a 30 GB partition is a reasonable size to provide for growth" — the only disk figure the base book gives, fetched 2026-11-11 from `~zeckma/openrc/lfs/13.0/`. **Measured:** 2.1 G in the guest before a single LFS package compiles (distro toolchain, rustup at 0.60 G, the repo, a built target directory); the four binaries are ~370 KB each. **A formula, not a number:** the four checkpoints of 4.4 are four tars of `$LFS`, and that term dominates the total — it becomes measurable the moment the first checkpoint is taken and not before. **Not estimable at all:** the BLFS and ML slices, for two independent reasons — `base.lock` does not yet enumerate packages, so there is no list to price, and the CUDA/cuDNN/PyTorch artifacts are unpinned (OQ1), so their sizes cannot even be fetched. So the answer is: 59 G free covers the base build at twice the book's own growth allowance, and the number that decides the rest is blocked on `base.lock` and OQ1, not on the disk. Time remains unmeasured beyond host-prep, and "the disk fills mid-phase" is still unanswerable — though the ceiling is now a knob (`grow`) rather than a wall
- [x] 0.3 **Recorded: satisfied inside the guest, refused on the substrate.** The guest is disposable, so `prepare_build.sh`'s `lfs ALL=(root) NOPASSWD: SETENV, NOEXEC_ALL` is installed **there and nowhere else**; on `192.168.0.86` itself it stays refused (D10). Group 8 is consequently unblocked against the virtual disk — partitioning a qcow2 destroys nothing the physical host needs — and remains blocked on the substrate
- [ ] 0.4 Mark every task in this file `write` or `run`, and confirm the list of (run) tasks matches what 0.1 makes possible

## 1. Close the open questions from the pinned edition

- [ ] 1.1 Read `chapter05/` and `chapter06/` of the pinned 13.0-openrc edition and record the exact temp-tool and toolchain pass order (OQ1) — from the pages, not from memory of older LFS releases
- [ ] 1.2 Record the chapter-to-phase mapping from the edition's own table of contents, including the chapter 7 pages that enter the chroot and create files
- [ ] 1.3 Decide the checkpoint medium (OQ2) against the build host's filesystem, and record that the tar path remains the fallback
- [ ] 1.4 Decide the default test-suite policy (OQ3) and record the packages whose suites are never skipped
- [ ] 1.5 Decide where the phase table lives (OQ4) and record why the package list and the phase table may differ in that choice
- [ ] 1.6 Read `chapter08/pkgmgt.html` and record its techniques and drawbacks as the citation for owning `base.lock` rather than adopting a package manager — and record that `chapter08/packaging.html` is the Python PEP 440 library, not package management

## 2. Derive the package list rather than retype it

- [ ] 2.1 Extract the pinned edition's package list (name, version, URL, chapter and page) into `base.lock`, recording the edition and the revision it was derived from. **This task owns the contents; `base-system-and-init 3.1` owns the format** — one file, one truth, and neither change maintains a second list
- [ ] 2.2 Record checksums for every source in that file, from the book's own checksums where published, and state where each checksum came from
- [ ] 2.3 Make a pin bump produce a reviewable diff of the derived file, and record one worked example of that diff
- [ ] 2.4 Record the trust boundary of 2.2 in one paragraph: checksums verified, signing keys not, and what class of failure that does and does not defend against
- [ ] 2.5 Implement a check that rejects a change to `base.lock` whose added or moved line carries no book revision and page, and demonstrate it rejecting a citation-free line — without this, the citation rule in the spec is a sentence

## 3. Phase table and exit criteria

- [ ] 3.1 Write the phase table: name, book pages, entry requirements, exit criteria, checkpoint policy
- [ ] 3.2 For every package, record the version check that proves the pinned version was installed
- [ ] 3.3 For every package whose test suite the book describes, record the book's stated expectation as data
- [ ] 3.4 Implement the rule that a phase records passed only after its criteria were evaluated, and make the skip path produce a failed phase
- [ ] 3.5 Record the exact environment handed to the chroot, and verify it contains nothing the book's invocation does not
- [ ] 3.6 Retain per-package build evidence — command output, installed version, evaluated criteria and their results — named by the phase record, and make a phase unable to record passed while its evidence is missing. D2's "the misbuild is named at the package" is unfalsifiable without this, and it cannot be reconstructed for phases already built

## 4. State record and checkpoints

- [ ] 4.1 Define the state record: last passed phase, artifacts, timestamp, `base.lock` identity
- [ ] 4.2 Write the record only after exit criteria pass, and verify a crash mid-phase leaves the previous record intact
- [ ] 4.3 **(run)** Implement resume-at-next-phase and prove it with a deliberately failed phase
- [ ] 4.4 Implement checkpoints with a bounded retention count, and verify the retention limit actually deletes
- [ ] 4.5 **(run)** Verify a checkpoint restores a build tree to the state before the last passed phase
- [ ] 4.6 Lock the build root for the duration of a run: a second invocation fails naming the holding process, and a lock whose owner is gone is reported as stale with the action that clears it

## 5. Fetch, verify, and deny the network

- [ ] 5.1 Implement the fetch step over `base.lock`, with checksum verification failing the build and naming the file
- [ ] 5.2 Fail any build step that references a source absent from `base.lock`, before downloading anything
- [ ] 5.3 **(run)** Run a full phase with the network denied and record what it needed, so the claim rests on a run rather than an intention
- [ ] 5.4 Record the cache location and its invalidation rule
- [ ] 5.5 Implement the network **denial mechanism** (`unshare -n` or the host's equivalent) and record its failure mode, so "phases run without network" is a property of the phase and not a story about a cable. 5.3 tests what this task builds; neither replaces the other

## 6. Boot-critical verification

- [ ] 6.1 Define the boot-critical set as a **named set supplied by the target kernel fragment** — root device driver, filesystem driver, any layer between them — without naming a storage symbol this project has no machine to observe. The concrete symbols arrive with `target-hardware-bring-up`
- [ ] 6.2 Implement the check that a boot-critical symbol set `=m` with no initrd fails the build and names the symbol
- [ ] 6.3 Implement the pre-bootloader set check: kernel image; modules tree installed and non-empty, or absent with a recorded decision naming why; `fstab` root present in the partition table recorded by the partitioning phase; `/dev` populated; init executable
- [ ] 6.4 Record the no-initrd decision and its consequence in the install guide, next to the boot parameters, so the two are read together
- [ ] 6.5 **(run)** Verify the check fails on a deliberately modular root driver — a check that cannot fail is the failure mode this project has already catalogued twice
- [ ] 6.6 Build the synthetic fragment the check is proven against, record its symbols and the expected failures, and state that a passing proof here says nothing about a real machine until `target-hardware-bring-up` supplies the real fragment

## 7. First boot

- [ ] 7.1 Write the first-boot checklist: root from the expected device, PID 1 identity, expected services, expected devices, expected effective kernel parameters
- [ ] 7.2 State for each item what it proves and what it does not prove
- [ ] 7.3 Record the checklist as not yet run, and hand its execution to `target-hardware-bring-up`
- [ ] 7.4 Remove or amend any document that presents a first boot as accomplished, if no run record exists

## 8. Destructive phases and the absorbed script

- [ ] 8.1 Write the destructive-phase guard: declared preconditions, the explicit list of devices and filesystems to be modified, refusal when the request touches anything outside the list, and refusal when the build root resolves to the host's own filesystem
- [ ] 8.2 Implement the dry run that prints every partition, format, and bootloader write without performing any of them, and verify the dry run changes nothing
- [ ] 8.3 Fold `scripts/build/prepare_build.sh` into the orchestrator as the host-prep phase rather than keeping it beside the new scripts
- [ ] 8.4 Replace its generic "LFS (OpenRC Edition) book" comment with the pinned edition and revision
- [ ] 8.5 Check its host-tool list against the pinned edition's host requirements chapter and record the diff, including anything the edition requires that the script does not test
- [ ] 8.6 Record that the script's `chroot` alias is not the book's chroot invocation, and make the phase use the book's
- [ ] 8.7 Record the scope of the inherited `lfs ALL=(root) NOPASSWD` sudoers line — disposable build host, removed with the host — per 0.3, or stop writing it
- [ ] 8.8 State in the README and install guide what the build actually is: a phase sequence with resumable state, derived package list, and the phases that are not yet implemented
- [ ] 8.9 Remove every remaining implication that a working build exists today

## 9. Honesty checks on this change

- [ ] 9.1 Confirm no phase, script, or document claims a completed build that has not been run end to end
- [ ] 9.2 Confirm the reproducibility wording matches D6 and that no byte-identical claim exists without a harness
- [ ] 9.3 Confirm the orchestrator gained no dependency resolution, install-order inference, or package-manager query surface
- [ ] 9.4 Confirm no measured number from the target machine appears in this change, and no storage symbol is asserted for a machine nobody has
- [ ] 9.5 Confirm every (run) task's evidence is a recorded run output, not a description of what the run would show
- [ ] 9.6 Archive this change with `--skip-specs` while its tasks are unexecuted — `openspec archive` writes the delta into `openspec/specs/` as current truth, and an unexecuted change has no truth to write (convention recorded in `openspec/project.md`)
