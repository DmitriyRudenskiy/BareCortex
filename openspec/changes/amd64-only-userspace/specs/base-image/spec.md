## Purpose

Governs what the built image is made of at the level of instruction set and
architecture: what may be assumed about it, how a claim about it is verified, and
what a measurement of an alternative base has to include before it can be quoted.

## ADDED Requirements

### Requirement: The image carries no 32-bit code

Every ELF file in the finished image SHALL be an ELFCLASS64 object for the
x86_64 machine, and the image SHALL NOT contain a `lib32` or `libx32` directory.
A committed check SHALL fail when either condition is broken, naming the file or
directory, and SHALL have no exclusion list.

#### Scenario: A built image is checked

- **WHEN** the check is run with the root of an assembled image
- **THEN** it SHALL read the ELF header of every regular file under that root and
  exit non-zero, naming each path, if any file is `ELFCLASS32`, names a machine
  other than x86_64, or sits under a `lib32` or `libx32` directory
- **AND** on exit it SHALL print how many ELF files it read, so that a silent
  scan of nothing cannot be reported as a pass

#### Scenario: The check is pointed at the source tree

- **WHEN** the check is run without an image root
- **THEN** it SHALL fail and say why, because passing over repository sources
  would be a claim about binaries the command never read

#### Scenario: A vendor package brings a 32-bit compatibility library

- **WHEN** a package added late in the build — a GPU driver's compatibility
  blobs are the expected first case — installs a 32-bit object
- **THEN** the check SHALL fail and name that package's file, and the remedy SHALL
  be the package's own install or configuration, never an exception recorded in
  the check

### Requirement: An architecture claim is read from a binary, not from a label

A statement that a base, seed, or package is 64-bit only SHALL be supported by a
census of ELF headers — class and machine — over the artifact actually fetched,
produced by a committed script, and SHALL name that artifact by checksum. An
upstream variant name, release tag, or architecture label SHALL NOT be recorded
as the reason.

#### Scenario: A document claims an upstream ships no 32-bit code

- **WHEN** documentation states that a base contains no 32-bit objects
- **THEN** the claim SHALL cite the counts of ELF files by class and machine
  printed by a script under `artifacts/`, against an input named by checksum
- **AND** the document SHALL NOT rest the claim on the variant being called
  `nomultilib`, `amd64`, or any other label

#### Scenario: A label and a census disagree

- **WHEN** an artifact's label says one thing and the census of its ELF headers
  says another
- **THEN** the census SHALL be recorded as the fact and the label treated as the
  upstream's marketing, and any figure depending on the label SHALL be withdrawn
  rather than annotated
