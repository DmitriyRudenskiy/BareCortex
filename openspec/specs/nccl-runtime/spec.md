# nccl-runtime Specification

## Purpose

Defines where NCCL runtime environment is defined on a BareCortex host and how every way of starting training receives it, so that tuning and troubleshooting never depend on which entry point was used.

## Requirements


### Requirement: One definition reaches every execution path

NCCL environment variables SHALL be defined in exactly one location, and every supported way of starting training SHALL receive that definition. Supported entry points include an interactive login shell, the training deployment script, and the service manager unit or init script.

#### Scenario: Training started by the service manager

- **WHEN** training is started through the init script rather than a login shell
- **THEN** the NCCL settings in force SHALL be identical to those an interactive shell receives
- **AND** the init script SHALL NOT define its own NCCL values

#### Scenario: A second definition is introduced

- **WHEN** a script or service sets an NCCL variable that the single source also defines
- **THEN** the change SHALL be rejected until one of the two definitions is removed

### Requirement: Only non-default variables are set

Each NCCL variable the project sets SHALL differ from the library default, and SHALL state why it is set. Variables equal to their default SHALL NOT be set.

#### Scenario: Redundant variable proposed

- **WHEN** a proposed variable already holds that value by default
- **THEN** it SHALL be removed from the configuration

#### Scenario: Troubleshooting values

- **WHEN** verbose diagnostic variables are useful
- **THEN** they SHALL be present but commented, with the condition for enabling them stated

### Requirement: No variable forecloses a future scope

The project SHALL NOT set a variable that would have to be unset in order to enable a capability the roadmap names.

#### Scenario: Transport-disabling variable proposed

- **WHEN** a variable would disable a network transport the project does not currently use but names as future work
- **THEN** the variable SHALL NOT be set
- **AND** the absence of that transport SHALL instead follow from the transport stack not being installed

#### Scenario: Interface pinning proposed

- **WHEN** a variable would pin the communication interface to loopback
- **THEN** it SHALL be rejected, since it removes the fallback path without improving the paths that matter

### Requirement: Scope is stated where the runtime is configured

The NCCL configuration file SHALL state the supported scope in its own header, so that a host configured by this project can be distinguished from one configured by hand.

#### Scenario: Operator inspects a configured host

- **WHEN** an operator reads the NCCL configuration file on a configured host
- **THEN** the file SHALL state the supported scope and link to the guide that explains it
