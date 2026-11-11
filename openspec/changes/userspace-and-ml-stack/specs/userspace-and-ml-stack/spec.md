# userspace-and-ml-stack Specification

## Purpose

Installs the layer between a built base system and a running training job, and makes the environment that layer produces accountable: one package list, one owner per environment concern, one record per run.

## ADDED Requirements

### Requirement: Every installed package names the book it came from

Every package installed by this change SHALL appear in `base.lock` with the edition, revision and page of the book it was installed from, or `binary:` plus the vendor and version for a vendor artifact. No second package list SHALL exist in this repository, in `ai-stack/`, or in a document.

#### Scenario: A BLFS package is added

- **WHEN** a package is added to the layer
- **THEN** its `base.lock` entry names the book edition, revision and page, and the check that rejects an uncited entry passes only when all three are present

#### Scenario: A README lists packages

- **WHEN** a document lists packages this layer installs
- **THEN** the list is generated from `base.lock` or states which entries of `base.lock` it covers, and no document states a version that differs from the lock

### Requirement: The runtime topology is declared per environment

The system SHALL state, in the install guide, whether the training runtime is wheel-authoritative or toolkit-authoritative, and SHALL state for each environment how `libcuda`, the CUDA runtime, cuDNN and NCCL are found. Building a framework from source SHALL be documented as a deviation with its gate, not as the default.

#### Scenario: A reader asks which CUDA a training run uses

- **WHEN** the install guide is read for the runtime topology
- **THEN** it names the default topology, names the deviation and its gate, and states which library set the training process loads

#### Scenario: A source build is attempted without the gate

- **WHEN** a source build of the framework is started without a GPU architecture list and a recorded reason
- **THEN** the build refuses and names the missing input, rather than producing a build for an assumed architecture

### Requirement: Python dependencies are pinned and installed without a network index

Every Python dependency set SHALL be recorded as a lock with a hash per artifact, and every install performed by this repository SHALL resolve from a local curated directory with the network index disabled. A shipped script that names a remote package index SHALL fail the check.

#### Scenario: An artifact is missing from the curated directory

- **WHEN** an install requests a dependency the curated directory does not hold
- **THEN** the install fails naming the artifact and its expected hash, and does not reach the network

#### Scenario: A script installs from an index

- **WHEN** a shipped script contains an install command naming a remote index
- **THEN** the check fails naming the file and line

### Requirement: Each piece of the runtime environment has exactly one owner

Library visibility for the driver SHALL be configured once through the dynamic linker configuration; toolkit libraries SHALL NOT be registered system-wide; service environment SHALL come from `/etc/conf.d/`; `profile.d` SHALL NOT set `LD_LIBRARY_PATH`; launchers and init scripts SHALL NOT set variables that a conf file owns.

#### Scenario: Two places set the same variable

- **WHEN** the same variable is set by a conf file and by a launcher or `profile.d`
- **THEN** the check fails naming both locations, and the launcher reads the conf file instead

#### Scenario: A run needs to know its environment

- **WHEN** a training run is recorded
- **THEN** the record contains the resolved values of the variables that affect the run, not a pointer to the file that may have set them

### Requirement: A training job leaves a record

Starting a job SHALL write a record containing the command, the resolved environment, the selected devices, the start time, and the log path; on completion the record SHALL gain the exit status. A job whose exit status was never observed SHALL be reported as unknown, not as successful.

#### Scenario: A job dies without a status

- **WHEN** a job's process disappears and no exit status was captured
- **THEN** the record reads unknown and the tool that reports running jobs lists it as unresolved

#### Scenario: A job is started twice by mistake

- **WHEN** a start is requested for a project that already has an unresolved record holding the same devices
- **THEN** the start refuses, names the existing record, and offers the override explicitly

### Requirement: Device selection refuses rather than guesses

Requested devices SHALL exist and SHALL be free of other processes before a job starts. A request naming a device held by another process SHALL be refused with the owning process named, and SHALL proceed only on an explicit override that is recorded.

#### Scenario: A requested device is busy

- **WHEN** a job requests a device with memory held by another process
- **THEN** the start refuses naming the device, the process and its owner, and no override is implied by a default

### Requirement: Scripts describe what they do

A script SHALL be named and documented for what it actually performs, and SHALL NOT cite a source that does not contain what it claims. A script that configures an environment SHALL say it configures rather than installs.

#### Scenario: A prerequisite is missing

- **WHEN** `setup_cuda.sh` runs with no toolkit present
- **THEN** it names the change and the task that installs the toolkit, and does not cite a book that does not contain it

#### Scenario: A script writes only environment files

- **WHEN** a script's only effect is writing configuration files
- **THEN** its name, its output and the install guide describe it as configuring, and no document counts it as installing the toolkit

### Requirement: Instructions that the change invalidates are rewritten in the same change

Where a change alters or removes what a script does, every document instructing a reader to run it SHALL be updated by the same change. Shipping a correct system next to an obsolete guide transfers the defect to the operator.

#### Scenario: A guide step names a replaced script

- **WHEN** the install guide instructs a reader to run a script this change has replaced
- **THEN** the step SHALL be rewritten to the new procedure, and the old instruction SHALL NOT remain as an alternative

#### Scenario: A verification step proves less than its heading

- **WHEN** a documented "verification" step runs a command that does not test what the step claims
- **THEN** the step SHALL name what the command actually establishes, or be replaced by one that does

#### Scenario: A documentation line points at a book with no target

- **WHEN** a document names an upstream book without a revision or states a relationship with an arrow and no sentence
- **THEN** it SHALL name the pinned revision, or the line SHALL be removed

### Requirement: Hardware-dependent validation is declared, not performed

Checks that require the target machine — device visibility, device count, a multi-device collective, throughput — SHALL be written as the check plus the record it produces, and SHALL be executed by `target-hardware-bring-up`. No document SHALL report their result before that record exists.

#### Scenario: A validation task has no machine

- **WHEN** a `(run)` validation task is reached with no target host
- **THEN** it is recorded as blocked, and the change does not claim the framework works on the GPUs

#### Scenario: A performance claim appears

- **WHEN** a document claims a speed or efficiency figure for this layer
- **THEN** the claim names the measurement, the host, the date and the revision it was measured against, or it is removed
