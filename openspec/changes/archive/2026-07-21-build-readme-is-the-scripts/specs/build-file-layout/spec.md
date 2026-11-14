## MODIFIED Requirements

### Requirement: The build README documents the build tree file by file

`scripts/build/README.md` SHALL contain a section for every file in the build
tree (each section naming the file and giving its usage), and SHALL not
document any file that is not present in the build tree. The README SHALL be
the build scripts' own documentation: the content of each section SHALL come
from the corresponding script's documentation block (or, for data the scripts
already carry as data, from that data — the phase table from `phases.toml`,
the check list from the check functions' docstrings), and the check suite
SHALL fail when either the file-for-section coverage or the content breaks.

#### Scenario: A build file has no documentation

- **WHEN** a file exists under `scripts/build/` but `scripts/build/README.md` has no section naming it
- **THEN** the check suite reports the file and fails

#### Scenario: The README names a missing file

- **WHEN** `scripts/build/README.md` documents a file that is not present under `scripts/build/`
- **THEN** the check suite reports the file and fails

#### Scenario: A script changes and the README is not regenerated

- **WHEN** a script's documentation block or the data the README is derived from changes, and `scripts/build/README.md` still carries the old text
- **THEN** the check suite fails and names the command that regenerates the README

## ADDED Requirements

### Requirement: The README is regenerated from the build scripts

The build driver SHALL provide a `derive-readme` subcommand that rewrites
`scripts/build/README.md` in place from: each build file's own documentation
block, the phase table read from `phases.toml`, and the check list read from
the check functions' docstrings. The regenerated file SHALL carry a generated
header stating the command that produced it, and the subcommand SHALL run
offline (no network, no guest).

#### Scenario: The README is regenerated

- **WHEN** the `derive-readme` subcommand is run from the repository root
- **THEN** `scripts/build/README.md` is rewritten so that the check suite's README check passes, without network access

#### Scenario: The README is a derived file

- **WHEN** a maintainer edits `scripts/build/README.md` by hand and then runs the check suite
- **THEN** the check fails and names the `derive-readme` subcommand as the fix
