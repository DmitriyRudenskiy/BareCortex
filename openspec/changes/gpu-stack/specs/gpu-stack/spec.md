# gpu-stack

## Purpose

Defines what sits between the kernel and a training run — NVIDIA kernel module, userspace driver, CUDA Toolkit, cuDNN, NCCL, the persistence daemon, and the fan-control service that ships beside them — how each is obtained and accounted for, and how the project's from-scratch claim is worded now that the stack is named. Installation sources are NVIDIA's own documentation. Running the stack on hardware belongs to `target-hardware-bring-up`.

## ADDED Requirements

### Requirement: Every GPU-stack component has a recorded provenance class

Each component of the GPU stack SHALL be recorded with its source URL, its licence, and one provenance class. The classes are the rows of the class map, whose path and record format `base-system-and-init` owns; the rows for `source-built`, `binary-distribution`, and `binary-authenticated-fetch` are this change's, and a class enters the vocabulary by a map row naming the change that owns it rather than by prose. The rule that an unaccounted package fails the build is that change's, and this one adds the GPU-binary case to it rather than restating it.

#### Scenario: A binary component is added to the image

- **WHEN** a component is installed from a published binary rather than built here
- **THEN** the `base.lock` entry SHALL name the artifact, its checksum, its licence, and the document the installation follows
- **AND** the entry SHALL NOT be marked `source-built`

#### Scenario: An installer writes files outside the build's view

- **WHEN** a component is installed by a vendor installer that writes files directly into system directories
- **THEN** the file set it installed SHALL be recorded as part of the `base.lock` entry
- **AND** a later image build SHALL be able to distinguish those files from unaccounted ones

#### Scenario: A component requires an authenticated download

- **WHEN** fetching a component requires a human account or login
- **THEN** the build SHALL accept a locally supplied artifact and verify its checksum
- **AND** the location an operator places that artifact in, and the filename it must have, SHALL be written where the operator will read it
- **AND** a missing artifact SHALL stop the build naming the artifact and its source, and SHALL NOT skip the component
- **AND** the documentation SHALL NOT describe the whole stack as building unattended

#### Scenario: A package's provenance fits no existing class

- **WHEN** a package's provenance matches no row of the class map
- **THEN** the class SHALL enter the vocabulary as a map row naming the change that owns it
- **AND** a document SHALL NOT gain a class name that the map does not carry

#### Scenario: The map and a document disagree about the classes

- **WHEN** a document that lists the classes as a set disagrees with the class map
- **THEN** the build SHALL fail naming the map, the document, and the classes on which they differ
- **AND** a document naming some classes incidentally — an ownership statement, a single class in a rule — SHALL NOT be treated as a list

#### Scenario: A class row carries no required fields

- **WHEN** a class exists in the map with no fields recorded for it
- **THEN** the check SHALL fail naming the class and the change owning the row
- **AND** the map SHALL NOT be reported complete

#### Scenario: A class is added after the completeness check exists

- **WHEN** a class row is added to the map
- **THEN** the completeness check SHALL cover that class without being edited
- **AND** the check SHALL report it red until its required fields are written

### Requirement: The from-scratch claim names its exception

Documentation claiming the system is built from scratch, or that runs with zero abstraction between code and GPU, SHALL state that the GPU driver and CUDA userspace are binary distributions. The statement SHALL be adjacent to the claim rather than in a separate document.

#### Scenario: A claim about building from scratch is written or edited

- **WHEN** a README or documentation line asserts a from-scratch or zero-abstraction property
- **THEN** the same view of the page SHALL carry the GPU-stack exception
- **AND** the exception SHALL name the components it covers

#### Scenario: A component is later built from source

- **WHEN** a component currently classed `binary-distribution` becomes `source-built`
- **THEN** the exception list SHALL shrink to match
- **AND** the claim SHALL NOT keep the exception after it stops applying

### Requirement: Driver and CUDA versions are recorded as a pair

The driver branch, the kernel-module flavour, and the CUDA version SHALL be recorded together, with the supported-architecture or compatibility table row that permits the combination. A version or branch SHALL NOT be recorded without the hardware fact that selects it.

#### Scenario: A driver branch is proposed

- **WHEN** a branch or module flavour is written into the build inputs
- **THEN** the record SHALL name the GPU architecture it was selected for and the table it was read from
- **AND** a branch taken from conversation or recollection SHALL be rejected as an input

#### Scenario: One half of the pair is updated

- **WHEN** the driver branch or the CUDA version changes
- **THEN** the pairing SHALL be re-checked against the compatibility table before the change is accepted
- **AND** the check SHALL be recorded as performed, with the table consulted named

### Requirement: The CUDA runtime owner is declared per environment

The documentation SHALL state, for the default environment, which component provides the CUDA runtime, cuBLAS, cuDNN, and NCCL that a training process loads: the system installation or the framework's own bundled libraries. Where the two differ, the difference SHALL be stated rather than left to load order.

#### Scenario: An environment is created from the documented procedure

- **WHEN** the documented steps are followed to create the default training environment
- **THEN** the libraries a training process loads SHALL be the ones the documentation names
- **AND** the command that shows which libraries are loaded SHALL be recorded

#### Scenario: A source-built library is shadowed

- **WHEN** a library built here is not the one loaded at run time
- **THEN** the build SHALL be corrected or the documentation SHALL stop claiming it is used
- **AND** the situation SHALL NOT be left to load order

### Requirement: Loader paths are registered and services read them

Shared libraries shipped by the GPU stack SHALL be registered through loader configuration owned by the installation. Environment variables SHALL NOT be the mechanism by which a service finds these libraries, and any remaining use SHALL be documented per consumer with its reason.

#### Scenario: A service needs the GPU stack

- **WHEN** an OpenRC service depends on a library from the stack
- **THEN** the service SHALL resolve it through loader configuration or an environment file the service actually reads
- **AND** the setup SHALL NOT depend on a login profile

#### Scenario: A library path is exported instead of registered

- **WHEN** a change adds an `LD_LIBRARY_PATH` entry
- **THEN** the change SHALL name the consumer that cannot use loader configuration and why

#### Scenario: The rule is enforced by a person remembering it

- **WHEN** compliance rests on a one-time audit rather than a command that fails
- **THEN** the mechanism SHALL be recorded as missing, since an audit does not keep a rule true after it is performed

### Requirement: A shipped service that controls cooling states its decision and its failure state

A service that changes fan speed or power limits SHALL document the input it reads, the threshold it applies, the interval, and the state it leaves the hardware in when its input is unavailable. Shipping the file is not specifying the behaviour.

#### Scenario: A cooling service ships without a specification

- **WHEN** a service file exists in the tree and no change states what decides its output
- **THEN** it SHALL be treated as unspecified behaviour and either specified or removed, with the decision recorded

#### Scenario: The temperature read fails

- **WHEN** the service cannot obtain the reading it acts on
- **THEN** it SHALL NOT leave the hardware in a lower cooling state than it found it in
- **AND** it SHALL report failure rather than success

### Requirement: Verification commands state what they prove

Documentation SHALL list the commands that verify the stack, each with the property it establishes and the properties it does not establish. No output of these commands, and no number derived from one, SHALL appear in this change.

#### Scenario: A verification command is documented

- **WHEN** a command is added to the verification list
- **THEN** the entry SHALL name what a passing result establishes and what it leaves unproven
- **AND** the change that runs it SHALL be named

#### Scenario: A hardware-dependent claim is proposed

- **WHEN** a documentation line would assert a measured result from the target machine
- **THEN** the claim SHALL live in `target-hardware-bring-up` and carry the measurement
- **AND** it SHALL NOT be added here
