# image-minimisation

## Purpose

Decides which packages the built image actually ships, on evidence read from the
image itself rather than from what the base book happens to build, and keeps that
decision reviewable: one verdict per package, the command that earned it, and a
dependency graph that answers what a removal breaks before the removal runs.

## ADDED Requirements

### Requirement: Every package carries a verdict

Every entry in `base.lock` SHALL carry exactly one verdict in `image-trim.toml`:
`ships`, `build-only`, or `absent`. A package the lock names and the verdict file
does not SHALL fail the build, naming the package. A verdict naming a package the
lock does not have SHALL fail the build, naming the row. The verdict set is
required to be total, because a file listing only the packages someone objected
to is a list of grievances and not a decision about the image.

#### Scenario: A pin bump adds a package

- **WHEN** `base.lock` is regenerated from the pinned edition and names a package `image-trim.toml` has no row for
- **THEN** the check SHALL fail and name that package
- **AND** it SHALL NOT pass by iterating only the rows that exist

#### Scenario: A row outlives its package

- **WHEN** a row names a package absent from `base.lock`
- **THEN** the check SHALL fail naming the row and the package
- **AND** the removal SHALL NOT be treated as still decided

#### Scenario: The lock is regenerated

- **WHEN** `derive-base-lock.py` rewrites `base.lock`
- **THEN** `image-trim.toml` SHALL be unchanged, and SHALL NOT be stored inside `base.lock`
- **AND** the derivation SHALL NOT be the thing that decides whether a package ships

### Requirement: A verdict names the evidence that earned it

Each verdict SHALL record the evidence behind it: the package's recorded file
list, the scan run whose output supports it, and — for `build-only` — the phase
that is its last consumer. A verdict whose support is a sentence SHALL NOT be
accepted, and no verdict SHALL cite a quantity that was never measured on this
build.

#### Scenario: A package is called absent

- **WHEN** a verdict is `absent`
- **THEN** the row SHALL name the build steps checked and found not to consume it, and the scan run that found no image file requiring it
- **AND** a row resting on the package's description alone SHALL fail the check

#### Scenario: A package is called build-only

- **WHEN** a verdict is `build-only`
- **THEN** the row SHALL name the phase after which the package is removed
- **AND** that phase SHALL be later than every phase whose recorded commands invoke the package

#### Scenario: A saving is claimed

- **WHEN** a verdict, task, or document states bytes, milliseconds, or a percentage saved
- **THEN** the figure SHALL name the command and the build run that produced it
- **AND** an unmeasured figure SHALL be removed rather than reworded

### Requirement: File ownership is recorded when the package is installed

The build SHALL record, per package, the files that package's installation put
into the image, captured around that package's own install step and kept as
evidence. Every removal decision, every dependency edge, and every post-trim
check SHALL be computed from those records. A package with no recorded file list
SHALL NOT be removable, because nothing can then be proved about what its removal
takes with it.

#### Scenario: A package is installed without a file list

- **WHEN** the trim step reaches a `build-only` or `absent` package whose file list is missing or empty
- **THEN** the trim SHALL fail and name the package
- **AND** SHALL NOT delete files matched by name or by guess

#### Scenario: Two packages record the same file

- **WHEN** the same path appears in the file list of more than one package
- **THEN** the file SHALL be recorded as disputed and its edges attributed to no single package
- **AND** a verdict that depends on removing it SHALL fail until the dispute is resolved

#### Scenario: The recorded list is smaller than the book's own contents

- **WHEN** a package's page in the pinned edition lists an installed program or library that the captured file list does not contain
- **THEN** the check SHALL report the package as under-tracked and name the missing path
- **AND** the package SHALL NOT be removable while it is under-tracked

#### Scenario: One page installs two packages

- **WHEN** a page in the pinned edition installs more than one declared package — `chapter08/tcl.html` installs Tcl and Tcl Documentation — and its Contents sections do not separate the two products
- **THEN** the captured list SHALL be recorded against the page and both packages SHALL share it
- **AND** a verdict removing one and keeping the other SHALL fail as inseparable, naming the page

### Requirement: Dependency edges are read from the image and the scan never executes what it scans

The dependency edges of the image SHALL be derived from the image's own files:
every file in the image is classified, and for each one that requires shared
libraries the required library names are recorded and attributed to the package
whose file list contains them. The scan SHALL NOT execute a file it is examining,
and the tool that resolves what the loader would actually load SHALL run inside
the image against the image's own loader, never against the build host's.

#### Scenario: A scanned file is setuid, or is not an ELF object

- **WHEN** the scan reaches a file it cannot interpret, including one with the set-user-ID bit set
- **THEN** it SHALL read the file's own recorded requirements and SHALL NOT run it
- **AND** it SHALL record the file as unscanned rather than as having no dependencies

#### Scenario: A required library belongs to no package

- **WHEN** a file in the image requires a library that no package's file list provides
- **THEN** the build SHALL fail naming the file and the library
- **AND** this SHALL be treated as a broken image, not as an absence of dependency

#### Scenario: A library is opened by name at run time

- **WHEN** a file loads a library through the dynamic loading interface rather than declaring it as a requirement
- **THEN** the edge SHALL be recorded as unresolved by the scan and named
- **AND** a verdict for the providing package SHALL NOT rest on scan evidence alone

### Requirement: The dependency graph answers what a removal breaks

The build SHALL produce a directed graph over the packages of the image, whose
edges come only from recorded scan output, and SHALL keep it as a repository
artifact. It SHALL support the reverse question — for one package, the set of
packages that require it directly or transitively — and every edge SHALL name the
file and the recorded requirement that produced it. An edge with no scan line
behind it SHALL NOT enter the graph.

#### Scenario: A removal is proposed

- **WHEN** a verdict proposes removing a package
- **THEN** the graph SHALL be queried for the packages that transitively require it
- **AND** a non-empty answer SHALL block the verdict until the listed packages are removed with it or the verdict is withdrawn

#### Scenario: The graph is older than the evidence

- **WHEN** the graph was produced from a scan run older than the newest file list or package build it describes
- **THEN** the check SHALL report it stale and fail
- **AND** a stale graph SHALL NOT be used to accept a verdict

#### Scenario: A hub is proposed for removal

- **WHEN** a package requires nothing and is required by many
- **THEN** the graph SHALL name it as one of the most-required packages
- **AND** its verdict SHALL be `ships` or the removal SHALL name what stops requiring it

### Requirement: The trim is one phase, idempotent, and reversed by checkpoint

Removal SHALL happen in one named phase, after the last phase that consumes
anything the phase removes and before the disk is frozen. It SHALL be computed
from `image-trim.toml` so that a rebuild applies the same trim, it SHALL be safe
to run twice, and undoing it SHALL be the existing checkpoint restore and no
mechanism of its own.

#### Scenario: The phase runs twice

- **WHEN** the trim phase runs against an image already trimmed
- **THEN** it SHALL remove nothing and report zero removals
- **AND** it SHALL NOT fail on files that are already absent

#### Scenario: A skip would remove a second package

- **WHEN** a row's verdict is `absent` and a page that builds that package also builds another declared package
- **THEN** the check SHALL reject the verdict and name the page and the package it would take with it
- **AND** the row SHALL state the mechanism it can actually use, which for a package sharing a page is removal by recorded file list and not a skip

#### Scenario: A package is built by more than one page

- **WHEN** a package's `pages` list names two or more pages, as `Python Documentation` names a chapter 7 page and a chapter 8 page
- **THEN** `absent` SHALL require every page in the set to be skipped, and the check SHALL say so when skipping them would remove a package another verdict keeps

#### Scenario: A later phase reinstalls a trimmed package

- **WHEN** any phase after the trim installs a file belonging to a trimmed package
- **THEN** that phase SHALL fail and name the package and the phase that reinstalled it
- **AND** the image SHALL NOT be frozen with the package silently back in it

#### Scenario: The trim has to be undone

- **WHEN** a trimmed image is found to be wrong
- **THEN** the recorded checkpoint from before the trim phase SHALL restore a working image
- **AND** `image-trim.toml` SHALL still describe the image the build intends to produce

### Requirement: A verdict is accepted by rebuild and boot, and the record states what its removal costs

A verdict SHALL be accepted only when a build from the recorded checkpoint
completes with the trim applied and the image reaches the multi-user runlevel.
Each non-`ships` verdict SHALL state what breaks if the package is later needed
and how it is brought back. A removal that breaks a later build step or the boot
SHALL be reversed by changing the verdict, with the failure recorded beside it.

#### Scenario: A later build step fails after the trim

- **WHEN** a package built after the trim fails for a missing file, header, metadata file, or interpreter belonging to a trimmed package
- **THEN** the verdict SHALL become `ships` and the failure SHALL be recorded in its row
- **AND** the graph SHALL be regenerated so the missing edge is visible next time

#### Scenario: The trimmed image does not reach multi-user

- **WHEN** the image with the trim applied fails to reach the multi-user runlevel
- **THEN** the service and the missing file SHALL be named, the responsible verdict reversed, and the checkpoint restored
- **AND** the boot SHALL NOT be declared working on the strength of the scan

#### Scenario: A removed package is needed at a console

- **WHEN** a verdict removes a package a person might need to repair the system
- **THEN** the row SHALL name the recovery: the phase that builds it and the command that installs it
- **AND** the recovery SHALL NOT require a package manager, which this project does not have

#### Scenario: Shrinking is proposed by another means

- **WHEN** a change proposes stripping binaries, dropping documentation at
  compile time, or removing a package on the accelerator path
- **THEN** this capability SHALL NOT be cited as its authority
- **AND** the book's own stripping and cleaning steps SHALL be cited where those are what is meant
