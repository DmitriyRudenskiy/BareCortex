## ADDED Requirements

### Requirement: A task names the machine class it runs on and is blocked when that class is unnamed

Every task in this change SHALL declare one of the three host classes defined in design D1 — `(build)`, `(gpu)`, `(target)`. A task whose class has not been named by a preceding task SHALL be reported as blocked, and SHALL NOT be closed by describing the output it would produce.

#### Scenario: A machine class has not been named

- **WHEN** a task requires the target and no target has been named
- **THEN** the task SHALL be reported blocked with the reason recorded once for the change

#### Scenario: A task is closed without recorded output

- **WHEN** a task in this change is closed and its evidence is prose about expected behaviour
- **THEN** that SHALL be treated as a defect and the task reopened

### Requirement: A hardware fact is recorded with its machine and date

Every hardware fact written down by this change SHALL carry the command that produced it, its verbatim output, the identity of the machine it came from, and the date. A fact without a machine identity SHALL NOT be used to support a claim in another change.

#### Scenario: A recorded fact lacks a machine identity

- **WHEN** a recorded driver version, device name, or topology line names no machine
- **THEN** it SHALL be rejected as evidence and re-collected

#### Scenario: Two machines disagree

- **WHEN** two recorded observations of the same property differ
- **THEN** both SHALL be kept with their machine identities, and the difference SHALL be recorded as a fact about the machines rather than resolved by discarding one

### Requirement: The target kernel fragment is derived from enumeration

The target fragment SHALL be produced by running the device detector on the target and resolving its unresolved markers by hand. The shipped example fragment SHALL NOT be used as the target fragment, and the generator's rejection of unresolved markers SHALL NOT be relaxed.

#### Scenario: The example fragment is used as the target fragment

- **WHEN** the target fragment is byte-identical to the shipped example
- **THEN** the build SHALL fail, since the example names no device on this machine

#### Scenario: An unresolved marker remains

- **WHEN** the fragment still contains an unresolved marker
- **THEN** generation SHALL exit non-zero and say which device is unmapped

#### Scenario: No storage controller driver is named

- **WHEN** the fragment names a network driver but no storage controller driver
- **THEN** the boot-critical check SHALL fail before GRUB is written, since a kernel that cannot mount root is unrecoverable without physical access

### Requirement: The GPU stack is verified by device state in increasing order of cost

Verification SHALL proceed rung by rung — module loaded, device nodes present, driver reports devices, a process opens a device — and a failure SHALL name the rung that failed. Package presence and dependency satisfaction SHALL NOT be reported as GPU stack verification.

#### Scenario: Packages are installed and no module is loaded

- **WHEN** the file and dependency checks pass and the module is absent
- **THEN** the result SHALL name the module rung, and SHALL NOT report the GPU stack as working

#### Scenario: A device node is missing

- **WHEN** the module is loaded and no device node exists
- **THEN** the result SHALL name the device-node rung and name the configuration flag that controls creation

### Requirement: Topology reporting separates availability from bandwidth

Topology claims SHALL state which command produced them. `nvidia-smi topo -p2p r` SHALL be reported as availability only. No bandwidth, throughput, or speed claim SHALL be attributed to it, and no such claim SHALL appear without a measurement command and a date.

#### Scenario: A P2P result is reported

- **WHEN** the P2P check produces `OK`, `ERR`, or `UNS` for a pair
- **THEN** the record SHALL state availability, name the pair, and name the IOMMU state at the time

#### Scenario: A speed is inferred from topology

- **WHEN** a document or record states a bandwidth on the basis of a link type
- **THEN** it SHALL be corrected, since link type names a path class and not a rate

### Requirement: Results return to the changes that are blocked on them

Each output another change waits on SHALL have a named return task, and the consuming task SHALL be updated with a reference to the recorded observation. An observation recorded and never delivered SHALL be treated as an incomplete hand-off.

#### Scenario: The architecture list is produced

- **WHEN** the GPU architecture list is collected on the target
- **THEN** the consuming task in `userspace-and-ml-stack` SHALL cite this change's record rather than restating the value

#### Scenario: A consumer restates the value

- **WHEN** a consuming change copies a hardware value instead of referencing the record
- **THEN** the copy SHALL be replaced with a reference, so the two cannot drift

### Requirement: Destructive steps require a proven recovery path

Before any step that can leave the target unbootable or unreachable — writing the boot loader, replacing a kernel, unloading or replacing a driver — the change SHALL record that the recovery path was rehearsed and that console or out-of-band access exists.

#### Scenario: No out-of-band access exists

- **WHEN** the target has no console or out-of-band access
- **THEN** the destructive steps SHALL be refused and recorded as blocked, not attempted with care

#### Scenario: Recovery was never rehearsed

- **WHEN** the rollback procedure has not been rehearsed on any host
- **THEN** the first kernel or driver replacement SHALL not proceed

### Requirement: A blocked change reports that it produced nothing

When the machine named in 0.1 does not exist, this change SHALL report itself as blocked and SHALL NOT report partial completion in a form that reads like verification.

#### Scenario: No machine exists

- **WHEN** the precondition task records that no target and no GPU-equipped host exist
- **THEN** every dependent task SHALL be labelled blocked and the change SHALL state that it has produced no hardware evidence

#### Scenario: Status is reported upward

- **WHEN** another change cites this change as the owner of a hardware claim
- **THEN** it SHALL also report whether this change is blocked, so the claim is not read as verified
