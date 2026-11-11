# Tasks: maintenance-and-updates

Scope note: markers mean the classes in `openspec/project.md` — `(build)` is a toolchain VM, `(gpu)` needs an NVIDIA GPU present, `(target)` needs the target machine and is executed by `target-hardware-bring-up`. An earlier version of this file marked driver and temperature work `(build)`, which reads as schedulable on a VM and is not. Blocked tasks are recorded as blocked. Cadence numbers are policy, never measurement.

## 0. Inputs this change does not have

- [ ] 0.1 Record the partition layout and free space in `/boot` from the target, or mark every task that depends on disk layout `(target)` and blocked (OQ1)
- [ ] 0.2 Read what the base layer's logging package (sysklogd, pinned by `base-system-and-init` D1) actually does about rotation, from its installed manual page, and record the answer or the absence of one (OQ2). Note the candidate that already exists: `logrotate` is a package in the pinned BLFS edition, so the fallback is book-sourced rather than hand-written
- [ ] 0.3 Record whether a `build-orchestration` checkpoint can be used after a kernel bump, or whether post-bump rollback is a rebuild (OQ3)
- [ ] 0.4 Record the driver branch and its support horizon once the GPU is named (OQ4); until then the driver update path is written as a shape with a named unknown
- [ ] 0.5 Confirm `build-orchestration 2.1` produces `base.lock` and `4.4` produces checkpoints with bounded retention; the ledger and rollback groups depend on both
- [ ] 0.6 Name the GPU-equipped host that groups 3, 4 and 6 need, or mark 3.2, 3.5, 4.4, 6.2, 6.3, 6.6 and 7.6 blocked. Seven tasks in this change were written as though a build host could run them; the audit found it could not, and a mislabelled task is worse than a blocked one because it looks schedulable

## 1. The ledger: what to watch and when it was last watched

- [ ] 1.1 Add per-entry fields to `base.lock`: advisory source, entry class (book package / vendor binary / wheel), last-checked date, last-checked revision
- [ ] 1.2 Populate the advisory source **by entry class**, not entry by entry — one pass per class is the only way this task ends: (a) book packages, one citation per book page, derived rather than typed; (b) vendor binaries, the vendor's release-notes or driver-archive page; (c) wheels, the index they were fetched from. Record the date each class was last checked, which is what makes the whole class stale at once
- [ ] 1.3 List the entries with no advisory source and record each as accepted risk with a reason, so the gap is visible rather than implied
- [ ] 1.4 Implement the staleness report: one command lists every entry whose last check predates the current pinned revision, and demonstrate it going non-empty when the revision changes
- [ ] 1.5 Record the review cadence as policy with its default, who may change it, and the condition that makes the default wrong — labelled policy, not measurement
- [ ] 1.6 Record what a route does not provide: an advisory source is a place to look, and a package can be vulnerable with nothing published at any of them

## 2. The update path

- [ ] 2.1 Write the update procedure as a lock diff plus the phase run plus a checkpoint, and show one worked example end to end on a `(build)` host
- [ ] 2.2 Confirm the citation check from `build-orchestration 2.5` fires on an update diff, not only on an addition, and demonstrate it
- [ ] 2.3 Search every document for an upgrade command or an "update the system" instruction and correct each one to the lock-and-phase route
- [ ] 2.4 Record the update classes and what each touches: book package, kernel, driver binary, wheel — different blast radius, different verification, different rollback
- [ ] 2.5 Record the trust limit of an update in one paragraph: who signs the artifact, whether that signature is verified, and what a compromised mirror would and would not be caught by

## 3. Kernel and out-of-tree module move together

- [ ] 3.1 Record the kernel release a driver module was built for, at install time, in a location the service reads
- [ ] 3.2 `(gpu)` Make the GPU stack's start refuse on a mismatch between the running release and the recorded one, naming both, and verify the refusal path produces the message rather than a generic driver error. Observing a mismatch needs a driver and a kernel, so a build VM cannot answer this
- [ ] 3.3 Write the module rebuild as an orchestrator invocation against the new kernel's headers, and record why DKMS is not adopted (D3) so the rejection is a decision
- [ ] 3.4 Keep the previous module and the previous boot entry available until the new kernel has booted successfully once, and state what "successfully" means for this system
- [ ] 3.5 `(gpu)` Perform one kernel bump end to end: bump, build, boot, mismatch observed, module rebuilt, GPU stack started — recorded as command output at each step. Not `(build)`: the last two steps need a GPU
- [ ] 3.6 Record the failure this does not cover: a kernel that boots but whose modules install failed halfway, and what the operator sees

## 4. Rollback

- [ ] 4.1 Write the rollback procedure for each mechanism the system actually has: previous boot entry, checkpoint restore, and removal via the installed-file list captured by `gpu-stack 2.4`/`2.8`
- [ ] 4.2 State explicitly which rollback mechanisms are **not** available (snapshot, volume manager, copy-on-write) and why: no layout has been recorded as providing them
- [ ] 4.3 `(build)` Rehearse the kernel rollback once: boot previous entry, confirm the running release, confirm the GPU stack state afterwards, keep the record
- [ ] 4.4 `(gpu)` Rehearse a binary-artifact rollback once using the captured file list, and record any file the list did not cover — an incomplete list is the failure this rehearsal exists to find
- [ ] 4.5 Label every unpractised procedure as untested in the documentation, and remove any claim that rollback is available before 4.3 and 4.4 have records
- [ ] 4.6 `(target)` Repeat 4.3 against the real layout once the target exists, and record the differences from the build-host rehearsal

## 5. Backup and restore

- [ ] 5.1 Define the backup set as the operator's input only, and justify each entry in one line; anything the phase sequence rebuilds is out
- [ ] 5.2 Implement the backup as a reproducible archive with the pinned revision and the `base.lock` identity inside it, so a restore can state what it restored
- [ ] 5.3 `(build)` Perform one restore end to end on a host that has never been built, and keep the record: commands, revision, what did not survive
- [ ] 5.4 Record the operator's data policy as a decision with a placeholder owner — datasets and checkpoints are not this change's to define — and state that an undefined policy means data is not backed up
- [ ] 5.5 State where the backup lives and what it protects against, naming the failure it does not cover (same-disk copy protects against none of them)

## 6. Checks that can fail

- [ ] 6.1 Give every check script an exit-code contract and a non-interactive form, and state the contract in the script header
- [ ] 6.2 `(gpu)` Fix `check_gpu_health.sh` so the temperature condition it reports at lines 50–58 sets a non-zero exit, and verify by running it with a threshold below the current temperature. **Demonstrated 2026-11-11 on `192.168.0.86`:** with the threshold lowered to 20 °C against GPUs idling at 29–31 °C the script prints `WARNING GPU(s) … exceed 20°C!` for all five devices and **exits 0** — a monitor invoking it sees success. The same run shows a second defect: `HOT_GPUS` carries newlines, so the message breaks across five lines and reads `GPU(s) 0 / 1 / 2 / 3 / 4 exceed 20°C!`. Task stays open until the exit code is fixed
- [ ] 6.3 `(gpu)` Verify each `nvidia-smi` field the health check queries against the installed driver, and replace the `|| echo "N/A"` masks (line 33) with an explicit "field unavailable" line that affects the exit status. **Verified 2026-11-11 on driver 580.159.03:** every field the script queries is valid **except `pcie.replay.counter`**, which the driver rejects — `Field "pcie.replay.counter" is not a valid field to query`, **exit 2** — so line 33 has been printing a reassuring `N/A` on real hardware, which is the defect this task names. `ecc.errors.*` answer `[N/A]` with exit 0 on these cards. A query matching no device answers `No devices were found` with **exit 6**, neither zero nor one. `fan.speed` reads **0 at P8 idle** on all five devices
- [ ] 6.4 Add the checks the current script omits and the project has already decided matter: module-versus-kernel mismatch (3.1), persistence daemon state, and device visibility
- [ ] 6.5 Write one line per finding in the output, so a check can be consumed by a script as well as read by a person, and name that consumer: this project has no scheduler and no monitoring framework by decision D6, so the reader is an operator running the check and whatever the operator runs next. Anything that assumes an unattended consumer is inventing a component
- [ ] 6.6 `(gpu)` Demonstrate each new check failing once, on purpose — a check that has never been red is not evidence

## 7. Destructive maintenance

- [ ] 7.1 Rewrite `clean_vram.sh` to list holders with owners and act only on explicitly named targets, with a dry run as the default
- [ ] 7.2 Refuse to signal another user's process without an explicit override, and record the override in the output so the action is attributable
- [ ] 7.3 Refuse a device reset while any process is attached, name the attached process, and make the refusal visible rather than swallowed by `2>/dev/null` (line 61)
- [ ] 7.4 Remove the interactive-only paths: a prompt is permitted with a terminal attached but never as the sole way to proceed
- [ ] 7.5 Document the two-minute version for an operator under pressure: what to run, what it will touch, what it will refuse, and the override flag
- [ ] 7.6 `(gpu)` Verify 7.1–7.4 against a device held by a second user, and record that the refusal actually happened

## 8. Retention, cadence, and honesty

- [ ] 8.1 Record the logging mechanism's rotation behaviour from 0.2, or record that nothing rotates, with the growth rate observed on a real host rather than an assumed figure
- [ ] 8.2 If nothing rotates the logs, adopt the book-sourced `logrotate` (present in the pinned BLFS edition) or record why a different mechanism was chosen, and note the rejected alternative and its reason
- [ ] 8.3 Record checkpoint retention as inherited from `build-orchestration 4.4`, and state what retention does not protect against (a build that passed and was wrong)
- [ ] 8.4 Add a date and revision to every "verified", "tested" and "updated" claim in `docs/` and `README.md`, and remove the claims that have none to add. Lines owned by `honest-scope-and-config-generation` group 8 and `userspace-and-ml-stack` 8a are left to those tasks — check the task list before editing a line, so two changes do not rewrite the same sentence in different directions
- [ ] 8.5 Confirm no document implies a package manager, an upgrade command, or an automatic update exists
- [ ] 8.6 Confirm every `(build)`, `(target)` and `(run)` task's evidence is recorded output, and that blocked tasks are labelled blocked
- [ ] 8.7 Archive with `--skip-specs` while tasks remain unexecuted, per `openspec/project.md`
