# maintenance-and-updates Specification

## Purpose

Keeps a system with no package manager maintainable: a written route per package, updates that go through the build, a rollback that has been performed once, and checks that can fail.

## ADDED Requirements

### Requirement: Every package has a named security route or a recorded risk

Each entry in `base.lock` SHALL name where security advisories for that package are published, and SHALL carry the date and pinned revision of the last check. A package with no route SHALL be listed as accepted risk with a reason. A blank route SHALL NOT be readable as "nothing outstanding".

#### Scenario: A package has no advisory source

- **WHEN** a package's entry has no advisory source
- **THEN** the entry states the accepted risk and its reason, and the report of unwatched packages lists it

#### Scenario: A revision bump makes checks stale

- **WHEN** the pinned book revision changes
- **THEN** one command lists every entry whose last check predates that revision, and an empty list means every entry was checked against the current revision

### Requirement: An update is a change to the lock and a run of the phases

A package update SHALL be performed as a cited change to `base.lock` followed by the `build-orchestration` phase sequence for that package, producing a checkpoint. No script or document SHALL offer an in-place upgrade command, and no installed file SHALL be edited to change a version.

#### Scenario: A version is bumped

- **WHEN** a package version changes
- **THEN** the diff shows the lock change with its citation, the phase run is recorded, and the checkpoint exists before the system is rebooted

#### Scenario: A document offers an upgrade command

- **WHEN** a document names an upgrade command for this system
- **THEN** the command exists in the repository and performs the lock-and-phase route, or the document is corrected

### Requirement: The kernel and the out-of-tree module move together

The kernel release a driver module was built for SHALL be recorded at install time. Before the GPU stack starts, the running kernel release SHALL be compared with the recorded one, and a mismatch SHALL prevent the start with both releases named. Rebuilding the module against the new kernel SHALL be performed by the orchestrator.

#### Scenario: A new kernel is booted with an old module

- **WHEN** the running kernel release differs from the release the module was built for
- **THEN** the GPU stack refuses to start, names both releases, and states the rebuild command; `nvidia-smi` is not the first thing that reports the problem

#### Scenario: A module is rebuilt

- **WHEN** the module is rebuilt for a new kernel
- **THEN** the new built-for release is recorded, and the previous module remains selectable until the new kernel has booted successfully

### Requirement: Rollback names the mechanism the layout provides

Rollback SHALL be documented as the mechanisms the system actually has — a previous boot entry, a checkpoint restore, or removal of the paths recorded in a binary artifact's installed-file list — and SHALL NOT claim snapshot, volume-manager, or copy-on-write rollback unless the target layout is recorded as providing it. The procedure SHALL be rehearsed once and its record kept.

#### Scenario: A kernel update does not boot

- **WHEN** the new kernel fails to boot
- **THEN** the documented procedure selects the previous entry, states what happens to the module state afterwards, and names the command that confirms the system is on the previous kernel

#### Scenario: A rollback has never been performed

- **WHEN** a rollback procedure is documented but no rehearsal record exists
- **THEN** it is labelled untested, and no document describes rollback as available

### Requirement: Backup covers what cannot be rebuilt, and a restore is rehearsed

Backup SHALL cover the operator's input — `/etc` deltas, `/etc/conf.d`, runlevel assignments, the kernel fragment and generated configuration, `base.lock` and wheel hashes, the tuning ledger — and SHALL NOT present a copy of reproducible state as protection. One restore SHALL be performed end to end from the backup, with its record kept.

#### Scenario: A restore is claimed without a record

- **WHEN** a document states that configuration is backed up
- **THEN** a restore record exists naming the date, the revision restored, and the commands used

#### Scenario: Reproducible state is offered as a backup

- **WHEN** a backup set includes state the phase sequence can rebuild
- **THEN** the inclusion is justified in one line or removed

### Requirement: A health check returns a status

Every check SHALL exit non-zero when the condition it names is present, SHALL provide a non-interactive form, and SHALL print one line per finding. A check that reports a problem and exits successfully SHALL be corrected or removed. A query whose failure is swallowed by a default value SHALL name the field it failed to read.

#### Scenario: A threshold is exceeded

- **WHEN** a check's named threshold is exceeded
- **THEN** the process exits non-zero, prints the condition and the value that breached it, and the exit status is the whole contract

#### Scenario: A check is written for a consumer that does not exist

- **WHEN** a requirement or comment assumes a scheduler, monitor, or alerting service will act on the result
- **THEN** that consumer SHALL be named, or the assumption SHALL be removed — this project has neither by decision, and an exit code nobody reads is the inert assertion with extra steps

#### Scenario: A queried field does not exist

- **WHEN** a check queries a field the installed tool does not provide
- **THEN** the output names the field as unavailable and the check's status reflects that the measurement was not made

### Requirement: Destructive maintenance is scoped, explicit, and refuses rather than asks

Any action that terminates processes, resets a device, or rewrites storage SHALL name its targets explicitly, SHALL check ownership before acting on another user's process, SHALL offer a dry run that changes nothing, and SHALL refuse when a precondition is unmet instead of prompting. A prompt SHALL NOT be the only way to proceed.

#### Scenario: A device is held by another user

- **WHEN** an action targets a device held by a process owned by a different user
- **THEN** the action refuses naming the process and owner, and proceeds only with an explicit override that is recorded

#### Scenario: A device reset is requested while in use

- **WHEN** a reset is requested for a device with an attached process
- **THEN** the reset is refused with the attached process named, and the refusal is not silenced by a default

### Requirement: Retention and cadence name their mechanism or their absence

Log retention SHALL name the component that performs it, or record that nothing does, together with the consequence measured on the host. Cadences SHALL be recorded as operator policy with a default and the condition that makes the default wrong, and SHALL NOT be presented as measurements.

#### Scenario: Nothing rotates the logs

- **WHEN** no installed component rotates a log directory
- **THEN** the gap is recorded with the observed growth rate on a real host, and no document claims logs are rotated

#### Scenario: A cadence is stated

- **WHEN** a document states how often a review happens
- **THEN** it is labelled policy, names who may change it, and no number is implied to be measured

### Requirement: Maintenance claims carry a date and a revision

Any statement that something was checked, tested, verified, or updated SHALL name the date and the revision or release it was true for. An undated claim SHALL be treated as unverified.

#### Scenario: A claim has no date

- **WHEN** a document states that a driver or package was verified
- **THEN** the claim names the version and date, or it is removed
