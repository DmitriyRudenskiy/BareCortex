# kernel-config Specification

## Purpose

Defines how BareCortex expresses kernel configuration and how a complete `.config` is produced from it, so that a configuration file can be trusted as a specification rather than a guess.

## Requirements


### Requirement: Fragment line grammar

Configuration fragments SHALL contain only lines of the form `CONFIG_<SYMBOL>=<value>` or `# CONFIG_<SYMBOL> is not set`. A fragment line SHALL carry no trailing comment, no inline annotation, and no commented-out configuration line. A decision not to set a symbol SHALL be recorded by the symbol's absence from the fragment, never as a commented line.

Rationale: the kernel's merge tool recognises the two forms above by anchored pattern match. Any additional text on the line makes the line inert — it is neither applied nor verified, so it silently asserts something that is not true.

#### Scenario: Trailing comment makes an assertion inert

- **WHEN** a fragment contains `# CONFIG_FOO is not set  # not needed for NVIDIA`
- **THEN** generation SHALL report the line as malformed and fail
- **AND** SHALL NOT proceed as if the symbol had been addressed

#### Scenario: Removal is expressed by absence

- **WHEN** a maintainer decides a symbol should no longer be forced
- **THEN** the change SHALL delete the line from the fragment
- **AND** the reason MAY be recorded in the change proposal, not in the fragment

### Requirement: Generation fails when a symbol does not survive

The generation step SHALL verify that every symbol requested by a fragment is present in the resulting configuration with the requested value, and SHALL exit with a non-zero status when it is not. Verification SHALL NOT be suppressed by any option passed to the merge tool.

#### Scenario: Symbol does not exist in the pinned kernel version

- **WHEN** a fragment requests a symbol the pinned kernel does not define
- **THEN** generation SHALL exit non-zero
- **AND** SHALL name the symbol and the requested value

#### Scenario: Symbol is dropped by an unmet dependency

- **WHEN** a fragment requests a symbol whose dependencies are not satisfied
- **THEN** generation SHALL exit non-zero rather than emit a configuration missing that symbol

#### Scenario: Verification is accidentally disabled

- **WHEN** generation is invoked with a flag that suppresses the merge tool's own verification
- **THEN** the script SHALL fail before writing any configuration

#### Scenario: Requested value is invalid for the symbol's type

- **WHEN** a fragment line names a real symbol with a value that symbol cannot take — a mode string given to a boolean, for example
- **THEN** generation SHALL exit non-zero, naming the symbol, the requested value, and the value the configuration actually holds
- **AND** SHALL NOT rely on the merge tool's report, which compares symbol names and discards values

### Requirement: Target device drivers are enumerated, not assumed

The base configuration SHALL NOT be relied upon to provide drivers for the target machine's devices. A target-derived fragment SHALL supply the network controller driver and the storage controller driver, and generation SHALL treat that fragment as mandatory.

#### Scenario: Target fragment is absent

- **WHEN** generation runs and the target fragment does not exist
- **THEN** generation SHALL exit non-zero
- **AND** SHALL NOT emit a configuration
- **AND** SHALL NOT emit a warning and continue

#### Scenario: Fresh checkout on a new machine

- **WHEN** a user clones the repository and runs generation without having produced a target fragment
- **THEN** generation SHALL exit non-zero and state how to produce the fragment

#### Scenario: Target fragment lists no network driver

- **WHEN** the target fragment contains no network controller driver symbol
- **THEN** generation SHALL exit non-zero
- **AND** SHALL state that the resulting kernel would have no network on the target

#### Scenario: Target fragment lists no storage controller

- **WHEN** the target fragment names no driver for the disk the root filesystem lives on
- **THEN** generation SHALL exit non-zero
- **AND** SHALL state that the resulting kernel would not mount root

### Requirement: Symbols are pinned to a kernel version

Every symbol used in a fragment SHALL exist in the kernel version the project pins, and the pinned version SHALL be recorded in the fragment.

#### Scenario: Kernel version is bumped

- **WHEN** the pinned kernel version changes
- **THEN** generation SHALL be re-run and SHALL fail on any symbol removed or renamed in the new version
