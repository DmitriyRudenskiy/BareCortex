# base-system

## Purpose

Defines what BareCortex inherits from Linux From Scratch, how the difference from LFS is recorded, and how every package in the built image can be traced back to a declared source. The book itself stays upstream; this specification covers only the decisions that are ours.

## ADDED Requirements

### Requirement: The base book is named and pinned

Documentation and build inputs SHALL name the LFS edition and version the system is built from, and SHALL record the revision at which an edition without release cadence was pinned. The edition named SHALL be one that resolves, and SHALL be the OpenRC edition while the init system is OpenRC.

#### Scenario: A documentation link is added or changed

- **WHEN** a documentation file cites an external book, guide, or edition
- **THEN** the URL SHALL resolve at the time of the change
- **AND** the citation SHALL describe what the target actually contains

#### Scenario: The upstream publishes no releases

- **WHEN** an upstream the build depends on has no release cadence — a development snapshot, or a directory outside the project's own index
- **THEN** `base.lock` SHALL record the revision or snapshot date used
- **AND** the documentation SHALL state what the project does when that upstream lags the book it patches
- **AND** SHALL NOT describe the dependency as owned software with no upstream

#### Scenario: The named edition contradicts the init system

- **WHEN** the base book named is the systemd edition of LFS
- **AND** the system is configured to run OpenRC
- **THEN** the combination SHALL be treated as an error, not as a choice

### Requirement: Specifications carry the delta, not the book

Specifications SHALL state what BareCortex excludes from LFS, adds to it, or guarantees on top of it. A specification SHALL NOT restate LFS package lists, versions, or build instructions.

#### Scenario: A specification paragraph duplicates upstream content

- **WHEN** a specification section reproduces content available from the pinned book
- **THEN** the section SHALL be replaced by a reference to the book plus the delta
- **AND** the reason for the delta SHALL be stated in one sentence

#### Scenario: A package is excluded from the image

- **WHEN** the image omits a package the base book builds
- **THEN** the specification SHALL name the package and the reason
- **AND** SHALL state what breaks if a later change adds it back

### Requirement: Every package in the image is declared

The build SHALL read one file — `base.lock`, at the repository root — recording, for each package: name, version, source URL, checksum, provenance class with the fields that class requires, and the patch series applied to it. A package present in the image without an entry SHALL fail the build. **This change defines the format; `build-orchestration` derives the contents from the pinned book** — and an entry for bytes that come from no book page is authored by the change that installs it, there being no page to derive it from. A second package list anywhere in this repository is the defect this requirement exists to prevent.

#### Scenario: A checksum does not match

- **WHEN** a downloaded source archive does not match its recorded checksum
- **THEN** the build SHALL stop and name the package
- **AND** SHALL NOT fall back to an unpinned source

#### Scenario: An undocumented package appears

- **WHEN** a file in the built image belongs to no `base.lock` entry
- **THEN** the build SHALL fail and name the file and its owning package

#### Scenario: An entry comes from no book page

- **WHEN** an installed artifact is built from this repository rather than from the pinned book
- **THEN** its entry SHALL be authored by the change that installs it
- **AND** it SHALL record the artifact's installed path, the build scope that produced it, and whether that scope is a crate or a file

### Requirement: The provenance classes are a class map, not a sentence

The provenance classes SHALL live in one machine-readable file — `class-map.toml`, at the repository root — holding one row per class and recording, per row, the fields that class requires and the change that owns it. The class map is hand-authored and SHALL NOT be regenerated together with the derived contents of `base.lock`. Checks SHALL read the class list from the class map. Prose SHALL point at the class map and SHALL NOT list the classes.

#### Scenario: The class map cannot be read

- **WHEN** the class map is absent, unparseable, or holds no class row
- **THEN** the check SHALL fail naming the path `class-map.toml`
- **AND** it SHALL NOT pass by iterating zero rows

#### Scenario: Class completeness is reported

- **WHEN** the completeness check runs
- **THEN** it SHALL emit one machine-readable line per class row naming the class, whether its required fields are present, and the owner recorded in that row
- **AND** an incomplete class map SHALL be known from those lines and not from prose asserting that it is red

#### Scenario: A field claims to enumerate the classes

- **WHEN** a field is declared, in the class map's schema, to enumerate the classes
- **THEN** its contents SHALL be checked against the class map, and a class it names that the class map lacks SHALL fail
- **AND** prose SHALL NOT count as such a declaration however it reads — prose needing the list points at the class map

#### Scenario: `base.lock` contents are regenerated

- **WHEN** the derived contents of `base.lock` are rewritten from the pinned book
- **THEN** the class map SHALL be unchanged, and it SHALL NOT be stored inside `base.lock`

### Requirement: Deltas are patch series, not vendored forks

Modifications to an upstream package SHALL be recorded as a patch series referenced from `base.lock`, together with the upstream remote and revision. A copy of an upstream project SHALL NOT be committed to this repository.

#### Scenario: A fork seems necessary

- **WHEN** a change to an upstream package cannot be expressed as a patch series
- **THEN** the reason SHALL be recorded in the `base.lock` entry
- **AND** the fork SHALL be tracked as a remote and revision, not as a directory in this repository

### Requirement: Language policy is a selection rule, not a rewrite program

Package selection SHALL follow, in order: a maintained port in the preferred language, then the upstream package as-is, then do not adopt. The policy SHALL NOT require rewriting a package, and SHALL NOT apply to the accelerator path — the kernel, the GPU driver, the CUDA runtime, CPython, and PyTorch.

#### Scenario: A rewrite is proposed

- **WHEN** a change proposes rewriting an existing package rather than adopting a maintained port
- **THEN** the proposal SHALL name the port considered and why it was rejected
- **AND** SHALL name the maintenance cost being accepted, since a rewrite has no upstream

#### Scenario: A package on the accelerator path is considered

- **WHEN** a package is part of the kernel, GPU driver, CUDA runtime, CPython, or PyTorch
- **THEN** the language policy SHALL not apply to it
- **AND** any change to it SHALL be justified by function, not by implementation language
