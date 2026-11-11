# maintenance-and-updates

## Why

There is no package manager, by decision: the pinned book's own package-management page recommends not having one, and `build-orchestration` D1 adopts that. That decision has a consequence the repository has not paid for. With no package manager, **every security update is a human act with a written route**, and today there is no route, no ledger, and no record of who last looked. A system that cannot `upgrade` can still be maintained — but only if the maintenance path is written down, which is this change.

The two scripts that exist are examples of the failure mode this project keeps meeting:

- `scripts/maintenance/check_gpu_health.sh` prints a temperature warning at line 56 and **exits 0 anyway** (`TEMP_THRESHOLD=85`, line 51, is compared and reported, never acted on). Line 33 queries a field whose existence is unverified and masks the failure with `|| echo "N/A"`. It is a check that cannot fail, which is the same class of defect as a detector that matches nothing.
- `scripts/maintenance/clean_vram.sh` asks a human with `read -p` (lines 32, 57), then kills **every** process holding GPU memory regardless of which user owns it (lines 39–53), then offers `nvidia-smi -r` on devices that may still be in use. It cannot be run by a service, and if run by a person it can end someone else's week-long job.

Nothing covers the update path itself: kernel bumps, the out-of-tree driver module that breaks when the kernel moves, config backup, log rotation, or how to get back.

## What Changes

- **A per-package security route in `base.lock`** — where advisories for this package are published, and the date and revision of the last check. Packages with no route are recorded as accepted risk, in the open, rather than silently unwatched.
- **An update is a build.** A version bump is a diff to `base.lock` that goes through the `build-orchestration` phase sequence and produces a checkpoint. There is no in-place upgrade, and no document implies one exists.
- **Kernel and out-of-tree module move together.** The maintenance path detects a module built for a different kernel release and refuses to start the GPU stack with that reason, instead of letting `nvidia-smi` fail with a generic error.
- **Rollback names the mechanism the layout actually provides** — a previous boot entry, a checkpoint restore, and the installed-file list captured for binary artifacts. No snapshot is claimed for a filesystem that has not been stated to provide one.
- **Backup covers what is not reproducible**: `/etc` deltas, `/etc/conf.d`, runlevels, the kernel fragment and `.config`, `base.lock`, wheel hashes. One rehearsed restore, with its record kept.
- **Health checks return a status.** A check exits non-zero on the condition it names, or it is not a check.
- **Destructive maintenance is scoped and non-interactive**: explicit targets, ownership checked, dry run first, refusal instead of a prompt.
- **Log retention is a named mechanism** — the thing that rotates, or the recorded fact that nothing does.

## Impact

- New capability: `maintenance-and-updates`. Affected: `scripts/maintenance/**` (absorbed), `docs/INSTALL_GUIDE.md`, `base.lock` (new per-entry fields), `build-orchestration`'s checkpoint contract.
- Task-level dependencies: `build-orchestration 2.1` (derives `base.lock`) blocks the ledger tasks; `build-orchestration 4.4` (checkpoints with bounded retention) blocks the rollback tasks; `gpu-stack`'s installed-file-list capture blocks binary rollback. Nothing in the other four changes waits on this one, so there is no cycle.
- Blocked on the target machine: free space in `/boot`, the partition layout a restore must respect, the driver branch and its end-of-life policy, and any retention figure that depends on real log volume. Those tasks are marked and stay open.
- Cadence is operator policy. This change records a default and labels it policy; it does not invent an SLA, an RPO, or a number of days.
