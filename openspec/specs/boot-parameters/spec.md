# boot-parameters Specification

## Purpose

Documents the kernel command-line parameters that change GPU behaviour in ways kernel configuration cannot, each tied to the condition under which it applies and to a check an operator can run.

## Requirements


### Requirement: Parameters state their condition

Each documented kernel command-line parameter SHALL state the system state under which it is required, and SHALL state the behaviour when that state is absent. A parameter SHALL NOT be documented as unconditionally required when its effect depends on another setting.

#### Scenario: Conditional parameter is documented

- **WHEN** a parameter's effect depends on whether the IOMMU is enabled
- **THEN** the documentation SHALL name both branches: IOMMU enabled, and IOMMU disabled
- **AND** SHALL give the command that determines which branch the running machine is in
- **AND** SHALL NOT assert what firmware of a given platform family defaults to, absent a measurement of that machine

#### Scenario: Operator determines whether a parameter applies

- **WHEN** an operator follows the documented check on a running system
- **THEN** the check SHALL return an observable result that distinguishes the two branches

### Requirement: Behaviour claims are observable, not asserted

Documentation of GPU peer-to-peer and IOMMU behaviour SHALL direct the operator to a command whose output shows whether the behaviour is active. It SHALL NOT assert a performance outcome.

#### Scenario: Performance figure appears without a measurement

- **WHEN** documentation states a throughput or bandwidth figure
- **AND** no measurement command, hardware description, or kernel and driver version accompanies it
- **THEN** the figure SHALL be removed or replaced with the command that produces it

#### Scenario: Peer-to-peer verification

- **WHEN** an operator follows the documented verification procedure
- **THEN** the procedure SHALL yield a pass or fail per GPU pair from tool output
- **AND** SHALL state what to change when the result is fail

### Requirement: Documented parameters are discoverable

Every parameter the project recommends SHALL be checkable on a running system without rebooting.

#### Scenario: Parameter presence is checked

- **WHEN** an operator needs to confirm the running command line
- **THEN** the documentation SHALL name the file or command that reports it
