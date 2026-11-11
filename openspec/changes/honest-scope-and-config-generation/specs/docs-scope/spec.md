# docs-scope

## Purpose

Keeps what BareCortex claims about itself consistent across documentation and executable scripts, so that a reader can trust a claim as a statement of fact rather than of intent.

## ADDED Requirements

### Requirement: Scope is stated where a capability is claimed

Any document or script that names a capability with a scope boundary SHALL state that boundary at the point of the claim. The headline description SHALL state the supported hardware topology.

#### Scenario: Capability is named without its boundary

- **WHEN** a document names a capability that is only supported under a stated limitation
- **THEN** the limitation SHALL appear in the same section, not only in a separate document

#### Scenario: Headline claim

- **WHEN** the project is described in one line
- **THEN** that line SHALL state the supported topology

### Requirement: Numeric claims carry their measurement

Any numeric figure describing performance, footprint, or timing SHALL be accompanied by the command that measures it and the hardware, kernel, and driver it was measured on. A figure without those SHALL be removed.

#### Scenario: Unsourced figure found

- **WHEN** documentation states a figure with no measurement command or hardware target
- **THEN** the figure SHALL be deleted or replaced by the command that produces it

#### Scenario: Measurement command is itself unverified

- **WHEN** a measurement command is offered for the project's own init system
- **THEN** the command SHALL exist on that init system
- **AND** SHALL NOT reference a competing init system or an output string that does not occur

### Requirement: Scripts and documentation state the same scope

Executable scripts SHALL NOT configure a host in a way that contradicts the documented scope.

#### Scenario: Installer contradicts documentation

- **WHEN** an installer writes configuration that enables or assumes a capability the documentation marks unsupported
- **THEN** either the installer or the documentation SHALL be corrected in the same change

#### Scenario: Scope statement is added to documentation only

- **WHEN** a scope change updates documentation but leaves scripts that emit the previous behaviour
- **THEN** the change SHALL be incomplete

### Requirement: Unsupported capabilities are enumerated with their gap

A capability the project names as future work SHALL be accompanied by the specific components that are absent, at the level of individual configuration symbols and userspace packages.

#### Scenario: Future work is described

- **WHEN** a capability is marked out of scope but named as future work
- **THEN** the document SHALL enumerate the missing kernel symbols and userspace dependencies
- **AND** SHALL state that the enumeration is unverified until built against the pinned kernel
