# Design: maintenance-and-updates

## Context

`build-orchestration` D1 declined a package manager because the pinned book declines it. That is a coherent position — the book's own chapter lists the techniques it prefers and calls the alternative "It is All in My Head!" — but the external plans that reach for `pam_limits`, DKMS and a wheel index all quietly assume a distribution's update machinery. This change is where the assumption is either paid for or dropped in the open.

Two facts shape everything below. First, the system is single-node and headless, so maintenance is one operator over SSH with no second machine to fail over to: a bad kernel or a bad driver is a physical-adjacent problem, and the rollback path is not optional. Second, the GPU driver is out-of-tree, so the highest-frequency breakage is not a CVE, it is a kernel bump.

## Decisions

### D1 — The ledger is the update mechanism, and it is an artifact

No package manager means the truth is a file plus a person. `base.lock` gains, per entry: where advisories are published, the class of the entry (book package, vendor binary, wheel), and the date and revision of the last check. The ledger is reviewed as data — a package whose last check predates the current revision is visible in one command — and the review is a task, not a hope.

What this does **not** claim: coverage. A route is a place to look, not a guarantee that a vulnerability was announced there. Packages with no route are listed as accepted risk with the reason, because an empty column read as "nothing to do" is worse than an honest gap.

### D2 — An update is a diff to the lock and a run of the phases

Version bump → `base.lock` diff (citation check from `build-orchestration 2.5` applies) → phase sequence → checkpoint → boot. No script edits an installed file to "update" a package; no document offers an upgrade command that does not exist. This is the same rule `build-orchestration` D8 applies to the initial build, extended in time.

### D3 — Kernel and module move together, and the mismatch is named

The out-of-tree driver module is built against a specific kernel release. Booting a new kernel with the old module installed produces a driver that fails to load, and the operator sees `nvidia-smi` complaining about a missing driver. The maintenance path therefore:

- records the kernel release a module was built for, at install time;
- compares it against the running release before starting the GPU stack, and refuses with the two releases named;
- rebuilds the module against the new kernel's headers through the orchestrator, rather than adopting a second build system that watches kernel installs.

DKMS is not adopted: it is not in the base, it introduces a build system outside the orchestrator, and the orchestrator already owns "build this against these headers". The alternative is recorded so the rejection is a decision rather than an omission.

### D4 — Rollback is the mechanism the layout has, and it is rehearsed

Rollback is: select the previous boot entry; or restore a `build-orchestration` checkpoint; or, for a vendor binary, remove exactly the paths captured in its installed-file list. **No snapshot, LVM, or btrfs-subvolume rollback is claimed**, because no filesystem or layout has been stated to provide one — the layout is a `target-hardware-bring-up` input (OQ1). A rollback procedure that has never been performed is a hypothesis, so the change includes one rehearsal on a build host, with its record kept.

### D5 — Back up what cannot be rebuilt, and rehearse a restore once

Reproducible state is not backed up: anything the phase sequence can rebuild is cheaper to rebuild than to trust a copy of. What is backed up is the operator's input: `/etc` deltas and `/etc/conf.d`, runlevel assignments, the kernel fragment and generated `.config`, `base.lock` and its wheel hashes, and the tuning ledger. The restore is performed once, end to end, and the record of that restore is the evidence the backup exists.

### D6 — A check is a process with an exit code

Every check script exits non-zero on the condition it names, takes a `--quiet` form for use by a service, and prints one line per finding. A check that reports and returns success is deleted or fixed. This is the smallest possible contract: no monitoring framework, no daemon, no metrics pipeline — those are separate changes with their own justification, not a side effect of fixing a script.

### D7 — Destructive maintenance refuses instead of asking

`clean_vram.sh` becomes: list what holds the devices with owners; act only on explicitly named targets; require an override flag to touch another user's process; perform a dry run by default; refuse a device reset while any process is attached, and say why it refused. Interactive prompts are allowed only when a terminal is attached, and never as the only way to proceed.

### D8 — Retention and cadence are stated as policy, with the gap named when the mechanism is missing

Log rotation is either performed by a named component of the installed logging package or it is not performed; the task reads what the pinned logging package actually does rather than assuming `logrotate` exists. Cadences (ledger review, checkpoint retention, driver branch review) are recorded as operator policy with a default, explicitly labelled policy rather than measurement, and each names what makes the default wrong.

## Open questions

- **OQ1:** what is the partition layout, and does `/boot` have room for a second kernel plus its module tree? Unanswerable without the target. Every rollback task that touches disk layout is blocked on it.
- **OQ2:** does the pinned logging package rotate its own output, and with what policy knobs? Read the installed manual page; do not assume a rotation tool is present. One data point is already in hand and changes the shape of the question: **`logrotate` is a package in the pinned BLFS edition** (present in its rendered index), so the fallback is a book-sourced package rather than a hand-written rotator — the open half is whether the base layer's logging package rotates on its own, which decides whether `logrotate` is installed at all.
- **OQ3:** are `build-orchestration` checkpoints usable after a kernel bump, given bounded retention and the fact that a checkpoint captures the build tree rather than the installed root? This decides whether "restore the checkpoint" is a rollback or a rebuild.
- **OQ4:** which driver branch applies to the eventual GPU, and what is its support horizon? Needs the hardware; it decides whether the driver update path is routine or forced.

## Risks

- **The ledger decays.** A file of "last checked" dates that nobody updates is worse than none, because it looks like control. The mitigation is that staleness is computed against the pinned revision, so a revision bump makes every stale entry visible at once.
- **Rollback rehearsal on a build host is not rollback on the target.** The rehearsal proves the procedure's steps are correct, not that the target's layout matches; the target half stays in `target-hardware-bring-up` and is not counted as done here.
- **Refusing instead of prompting can strand an operator at 3am.** The override flag is therefore mandatory in the tooling and documented at the top of the script, not buried in a help string.
