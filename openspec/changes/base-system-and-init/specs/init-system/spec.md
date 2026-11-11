# init-system

## Purpose

Defines the init system of a BareCortex host: what runs as PID 1, what replaces each init artifact the base book would have installed, and how the absence of systemd is established by a command rather than by a claim.

## ADDED Requirements

### Requirement: The init supervisor is named

The specification SHALL name the process that becomes PID 1 and the runlevel set in use. Choosing OpenRC SHALL NOT be recorded as choosing no init.

#### Scenario: A reader determines what PID 1 is

- **WHEN** an operator runs the documented command on a running host
- **THEN** the output SHALL identify the PID 1 program by name
- **AND** it SHALL match the supervisor named in the specification

#### Scenario: The supervisor is left unspecified

- **WHEN** the init specification names OpenRC without naming a supervisor
- **THEN** the specification SHALL be treated as incomplete
- **AND** SHALL NOT be used to justify a build step

### Requirement: The init delta is documented against its upstream

For each init artifact an operator would expect from the systemd edition of the base book — the systemd package, `systemctl`, `journalctl`, `systemd-networkd`, `logind` — and for each artifact the pinned OpenRC edition installs instead — `openrc-init` as `/sbin/init`, `/etc/runlevels`, `/etc/conf.d`, `/etc/init.d`, `rc-status` — the specification SHALL state whether it ships, and if not, what replaces it and which upstream covers that substitution. The pinned edition installs no SysVinit package and no `/etc/inittab`; PID 1 is `openrc-init`.

#### Scenario: A reader arrives from the systemd edition

- **WHEN** an operator follows documentation written against the systemd edition and reaches `systemctl` or a unit file
- **THEN** the specification SHALL name the OpenRC equivalent the pinned edition installs
- **AND** SHALL name the upstream that documents it, or state that none does

#### Scenario: A component has no upstream

- **WHEN** a component exists only because BareCortex wrote it
- **THEN** the specification SHALL say so explicitly
- **AND** SHALL name what breaks when it is not maintained

### Requirement: The absence of systemd is verified by the build

The build SHALL scan the finished image for runtime resolution of systemd-provided shared libraries and SHALL fail when any is found, naming the offending binary. The scan SHALL cover at least `libsystemd`, `libelogind`, `pam_systemd`, and `libnss_systemd`. Documentation SHALL state the claim as "no systemd as init or service manager, no `logind`", and SHALL NOT state that no systemd source is compiled, because the pinned edition builds udev from the systemd tarball.

#### Scenario: The base book's own udev

- **WHEN** the base system is built from the pinned OpenRC edition
- **THEN** udev SHALL come from the systemd source tree as the edition builds it, with `logind` and `vconsole` disabled
- **AND** the scan SHALL still pass, because that build links its support code statically
- **AND** a scan failure on a udev binary SHALL be treated as a broken static-link assumption, not as an expected exception

#### Scenario: A package links systemd anyway

- **WHEN** a package is built with systemd integration still enabled
- **THEN** the scan SHALL fail the build and name the binary and the library
- **AND** the fix SHALL be the package's configure option, not an exception list

#### Scenario: The check is removed or weakened

- **WHEN** a change removes the scan or adds a package to an exclusion list
- **THEN** the change SHALL state why the exclusion cannot be a build option
- **AND** the "No systemd" claim SHALL NOT remain in documentation without the command that verifies it

### Requirement: Every shipped service is inventoried with its failure mode

Each OpenRC service shipped in this repository SHALL have an inventory entry naming its runlevel, what it requires, and how it reports failure. A service SHALL report success only after establishing that it did the work. The inventory SHALL be enforced by a command that fails when a service file has no entry — an inventory maintained by hand is a list, not a check.

#### Scenario: A service file has no inventory entry

- **WHEN** a service exists under the OpenRC service directory
- **AND** no inventory entry names it
- **THEN** the check SHALL exit non-zero naming the file, and the build SHALL NOT pass

#### Scenario: The inventory itself is unenforced

- **WHEN** the inventory exists only as prose and nothing compares it against the service directory
- **THEN** the requirement SHALL be recorded as unmet, since a list nobody checks drifts on the first added service

#### Scenario: A service succeeds without acting

- **WHEN** a service's start path finds nothing to act on
- **THEN** it SHALL report failure, not success
- **AND** the reason SHALL be visible in its output
