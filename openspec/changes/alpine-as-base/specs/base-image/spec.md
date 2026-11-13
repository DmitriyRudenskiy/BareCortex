# base-image

## Purpose

Governs how an alternative to the pinned base may be discussed and recorded: with
a measurement a committed script reproduces, with the dependency providers Alpine
leaves open named by us rather than resolved by apk, and with the numbers
detectably stale the moment the upstream input moves.

## ADDED Requirements

### Requirement: An alternative base is argued with a reproducible measurement

A claim about the cost or dependency shape of a base the build does not use SHALL
be produced by a script committed alongside it, and the document quoting it SHALL
name the input that script fetched. A cost stated in prose with no script behind
it SHALL NOT appear in the repository.

#### Scenario: A document states what an alternative base would cost

- **WHEN** documentation states a size, a package count, or a dependency graph for
  a base other than the pinned one
- **THEN** the figures SHALL be traceable to a script under `artifacts/` that
  re-derives them
- **AND** the document SHALL name the fetched input by checksum
- **AND** the script SHALL run unprivileged, so that re-measuring does not require
  stopping the build

#### Scenario: The measurement is challenged

- **WHEN** a figure from the measurement is disputed
- **THEN** running `artifacts/handoff-vm/alpine-probe/probe.sh` on an x86_64 Linux
  host SHALL reproduce it, and SHALL print the published and computed checksums of
  the input side by side
- **AND** the loader SHALL resolve every executable it is given: a `not found` in
  its output contradicts any recorded claim that the graph is closed

### Requirement: A virtual dependency's provider is named, not left to the resolver

Where a dependency is satisfied by more than one package, the record SHALL name
the provider chosen, or SHALL name the change that owns the choice and say that
the choice is open. Silence SHALL NOT count as a decision, and a placeholder
package that satisfies a resolver while doing nothing at run time SHALL NOT be
recorded as the provider of a capability.

#### Scenario: The closure of the alternative is recorded

- **WHEN** the closure of a seed set is recorded for an alternative base
- **THEN** each dependency the index resolves ambiguously SHALL appear with
  either a chosen provider and its reason, or an explicit deferral naming the
  change that owns it
- **AND** the recorded set SHALL be the set the index reports, obtained by running
  `artifacts/handoff-vm/alpine-probe/resolve-apk-closure.py` against that index,
  not a list of the dependencies someone expected to be ambiguous

#### Scenario: A provider is chosen because it is the default

- **WHEN** a provider is recorded with no reason other than the resolver picking it
- **THEN** the record SHALL be treated as undecided, because repository order and
  index contents decide it instead of this project

### Requirement: A recorded measurement becomes detectably stale

A figure measured against an upstream that moves SHALL be recorded together with
the checksum of the input it was measured against, and the check that detects the
input moving SHALL be named. Editing the figure to match a changed upstream
without re-running the measurement SHALL be a defect.

#### Scenario: The upstream input changes

- **WHEN** the published checksum of `alpine-minirootfs-3.24.4-x86_64.tar.gz`
  differs from the checksum recorded in `docs/ALPINE-AS-BASE.md`
- **THEN** every byte and package count in that document SHALL be treated as
  stale, and SHALL NOT be quoted as current
- **AND** the remedy SHALL be to re-run the probe and let its output replace the
  figures

#### Scenario: A figure is updated without its evidence

- **WHEN** a figure in the document changes while `artifacts/handoff-vm/alpine-probe/evidence/`
  still holds the output of the older run
- **THEN** the change SHALL be incomplete until the evidence is regenerated from
  the same run the figures came from
