# base-system

## MODIFIED Requirements

### Requirement: Every package in the image is declared

The build SHALL read one file — `base.lock`, at the repository root — recording,
for each package: name, version, source URL, checksum, provenance class with the
fields that class requires, and the patch series applied to it. A package present
in the image without an entry SHALL fail the build. **This change defines the
format; `build-orchestration` derives the contents from the pinned book** — and an
entry for bytes that come from no book page is authored by the change that
installs it, there being no page to derive it from. A second package list anywhere
in this repository is the defect this requirement exists to prevent.

Whether a declared package reaches the image is a separate decision and SHALL be
recorded outside `base.lock`, in the hand-authored verdict file
`image-trim.toml`, keyed by the package names `base.lock` declares. The verdict
file is a delta over the one package truth and SHALL NOT restate any field of it:
a name it uses that `base.lock` does not declare SHALL fail the check, so the file
cannot become a second list by accretion.

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

#### Scenario: A package is declared but does not ship

- **WHEN** a `base.lock` entry carries a verdict other than `ships`
- **THEN** the entry SHALL remain in `base.lock`, because the package is still built and its source is still fetched
- **AND** the verdict file SHALL carry the reason and what breaks if the package is later needed
- **AND** the image SHALL contain no file of that package's recorded file list

#### Scenario: The verdict file starts carrying package data

- **WHEN** a row of `image-trim.toml` records a version, URL, checksum, or book page
- **THEN** the check SHALL fail: those fields belong to `base.lock` and nowhere else
- **AND** the row SHALL key on the package name alone
