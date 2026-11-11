# build-orchestration

## Purpose

Defines how a BareCortex host is built from source: the phase sequence derived from the pinned base book, the observable criteria that let a phase be called finished, the state that lets a failed build resume instead of restart, the point at which sources are fetched and verified, and the checks that run before the machine is told to boot. It does not define package management, dependency resolution, or the contents of the packages, which stay with the book and with `base-system-and-init`.

## ADDED Requirements

### Requirement: A phase is recorded as passed only when its exit criteria were evaluated

Every build phase SHALL declare exit criteria in a form a machine can evaluate, and SHALL record the phase as passed only after those criteria were evaluated. Criteria SHALL include the pinned version of each package the phase installs, and SHALL include the test-suite result where the pinned book states an expectation for that package.

#### Scenario: A package misbuilds in an early phase

- **WHEN** a package's test suite reports results differing from the expectation recorded for it
- **THEN** the phase SHALL fail and name the package and the differing result
- **AND** the diagnosis SHALL NOT require suspecting every phase after it

#### Scenario: A phase is marked done without evaluation

- **WHEN** a phase completes without its exit criteria being evaluated
- **THEN** the phase SHALL be recorded as failed, not passed
- **AND** skipping criteria to save time SHALL be a visible incomplete phase, not a success

### Requirement: A failed build resumes from the recorded state

The build SHALL maintain a state record naming the last phase whose exit criteria passed, SHALL write it only after they pass, and SHALL resume a new run at the phase following it. The build SHALL take a checkpoint of the build tree at each phase boundary and SHALL retain a bounded, recorded number of checkpoints.

#### Scenario: A failure late in the system pass

- **WHEN** a phase fails after several phases have passed
- **THEN** a resumed run SHALL start at the failed phase using the checkpoint taken before it
- **AND** the phases already passed SHALL NOT be rebuilt

#### Scenario: Recovery from a bad package

- **WHEN** an operator needs the build tree as it stood before the last completed phase
- **THEN** the retained checkpoints SHALL make that restore possible without rebuilding from the host
- **AND** the retention limit SHALL be a recorded number

### Requirement: Sources are fetched and verified before any phase runs

A fetch step SHALL read `base.lock`, download what the cache lacks, verify every checksum, and fail naming any file that does not match. Build phases SHALL run with network access denied by a named mechanism — a network namespace, or the equivalent for the build host — and not by the absence of a cable; the mechanism and its failure mode SHALL be recorded with the phase. A source referenced by a build step but absent from `base.lock` SHALL fail the build before any download is attempted.

#### Scenario: A phase reaches for the network

- **WHEN** a build step attempts a network operation while its phase is running under the denial mechanism
- **THEN** the operation SHALL fail in a way that is recorded against that phase
- **AND** the recorded failure SHALL name the phase, so the missing source becomes a `base.lock` entry rather than an exception

#### Scenario: A checksum does not match

- **WHEN** a fetched source's checksum differs from the value recorded in `base.lock`
- **THEN** the build SHALL fail naming the file, the expected value and the `base.lock` entry
- **AND** no phase SHALL have started

#### Scenario: A step needs something `base.lock` does not name

- **WHEN** a build step references a source with no `base.lock` entry
- **THEN** the build SHALL fail before fetching it
- **AND** the fix SHALL be a `base.lock` entry, not an exception in the step

### Requirement: The boot-critical set is verified before the bootloader is written

The build SHALL verify, before installing the bootloader, that the kernel image, the root filesystem driver set, `/etc/fstab`, `/dev`, and the init program form a set that can mount root and start. Because the system has no initial ramdisk, every driver required to mount the root filesystem SHALL be built into the kernel, and a modular driver in the declared boot-critical set SHALL fail the build and name the symbol.

The boot-critical set SHALL be declared as a **named set supplied by the target kernel fragment**, not enumerated in this change: the concrete storage controller is a property of a machine this project does not have, and naming it here would be an invented fact. The check SHALL be proven against a synthetic fragment whose symbols are known to the check.

#### Scenario: The target machine is unknown

- **WHEN** no target fragment exists yet
- **THEN** the check SHALL be exercised against a synthetic fragment and its result recorded as proven against the synthetic input
- **AND** the requirement SHALL NOT be marked satisfied for a real machine, and no storage symbol SHALL be asserted here

#### Scenario: A root driver is modular

- **WHEN** a symbol in the declared boot-critical set is `=m` and no initial ramdisk is produced
- **THEN** the build SHALL fail and name the symbol and the filesystem or device it gates
- **AND** the failure SHALL occur before the bootloader is installed

#### Scenario: fstab names a root that is not there

- **WHEN** the built tree's `/etc/fstab` names a root device or UUID that does not appear in the partition table recorded by the partitioning phase, and no partitioning phase has recorded one
- **THEN** the build SHALL fail rather than write a bootloader entry expected to fail at boot

### Requirement: The orchestrator resolves no dependencies

The install order SHALL come from the pinned book, recorded as data with the revision it was derived from. The orchestrator SHALL NOT compute an install order, and adding or reordering a package SHALL require a change naming the book revision and page that justifies the position. **A machine check SHALL reject a change to the derived package list whose added or moved line carries no book revision and page**, and that check SHALL be demonstrated to reject a citation-free line.

#### Scenario: A package is added

- **WHEN** a package is added to a phase
- **THEN** the change SHALL cite the book revision and page establishing its position and version
- **AND** the derived package list SHALL change as a reviewable diff

#### Scenario: A resolver is proposed

- **WHEN** a change adds dependency resolution to the orchestrator
- **THEN** it SHALL be rejected as out of scope, the book's own package-management section having declined to recommend any scheme

### Requirement: A change to the derived package list carries a citation

Every line added to or moved in `base.lock` SHALL name the book revision and page it came from, and a check SHALL reject a change whose diff lines carry no such citation. The rule that the list is derived rather than typed is enforced by that check, not by prose in a design document.

#### Scenario: A version bump cites its source

- **WHEN** a commit changes a version in `base.lock` and names the book revision and page
- **THEN** the check passes and the diff shows the citation beside the changed line

#### Scenario: A package is added without a citation

- **WHEN** a commit adds a package to `base.lock` with no book page named
- **THEN** the check fails naming the line, and the build does not consume the edited list

### Requirement: A destructive phase declares its preconditions and refuses unsafe input

Any phase that partitions, formats, writes a bootloader, or otherwise modifies state it cannot undo SHALL declare its preconditions, SHALL list every device and filesystem it intends to modify, and SHALL refuse to run when the requested operation touches anything outside that list. It SHALL be able to print the full set of operations it would perform without performing them.

#### Scenario: A device already holds something

- **WHEN** a target device carries a partition table or filesystem not listed in the partition plan being applied
- **THEN** the phase SHALL refuse and name what it found
- **AND** it SHALL NOT proceed on a confirmation flag alone

#### Scenario: The build tree is somewhere it should not be

- **WHEN** the build root resolves to the host's own filesystem, or to a mount that is not a dedicated build target
- **THEN** the phase SHALL refuse before creating any directory

#### Scenario: Dry run

- **WHEN** the phase is asked what it would do
- **THEN** it SHALL print every device, partition, filesystem, and bootloader write
- **AND** the dry run SHALL NOT modify state

### Requirement: Each package leaves retrievable build evidence

For every package a phase builds, the build SHALL retain the command output, the version actually installed, the exit criteria evaluated, and their results, in a location named by the phase record. A phase SHALL NOT be recorded as passed for a package whose evidence is absent.

#### Scenario: A test count differs from the book

- **WHEN** a package reports results differing from the recorded expectation
- **THEN** the retained output SHALL make it possible to name the failing test without rebuilding the package

#### Scenario: Evidence is dropped to save disk

- **WHEN** retention policy removes build evidence
- **THEN** it SHALL remove it for phases already recorded as passed and never for the current phase

### Requirement: One build runs at a time against a build root

The build SHALL take an exclusive lock on the build root and a second invocation SHALL fail naming the holding process. A lock left by a killed run SHALL be detectable as stale and removable by an explicit action, not silently.

#### Scenario: Two runs against one tree

- **WHEN** a second build is started against a build root that is locked
- **THEN** it SHALL fail immediately, without writing to the tree or the state record

#### Scenario: A run was killed

- **WHEN** a lock exists whose owning process no longer exists
- **THEN** the build SHALL report it as stale and state the action that clears it

### Requirement: The reproducibility claim is bounded to what is delivered

Documentation SHALL state reproducibility as the same `base.lock` and host toolchain producing the same package set, configuration, and recorded phase results. It SHALL NOT claim byte-identical output unless a rebuild-and-compare harness exists and has been run.

#### Scenario: A stronger claim appears

- **WHEN** documentation or a comment claims bit-for-bit or reproducible builds
- **THEN** it SHALL be removed or the harness and its recorded result SHALL exist in the repository

### Requirement: First boot is checked against a list and the result is recorded

The build SHALL produce a first-boot checklist covering the observable state of a started system — root mounted from the expected device, the init system running as PID 1, the expected services active, expected devices present, expected kernel parameters in effect — and the outcome of running it SHALL be recorded with the build that produced the system. The checklist SHALL NOT be presented as passed before it has been run.

#### Scenario: No machine has run it yet

- **WHEN** the checklist exists but has not been run
- **THEN** the build SHALL report it as not yet run
- **AND** no document SHALL record the system as verified
