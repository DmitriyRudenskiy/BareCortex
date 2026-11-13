# development-cycles

## Purpose

Defines what a development cycle is in this project: what a cycle must produce,
what its exit criterion has to look like, which cycles are allowed to change the
base and on what evidence, and what stays schedulable while a cycle is gated on
hardware or on an answer. It exists so that the order of work is a reviewable
artifact rather than something reconstructed from open changes.

## ADDED Requirements

### Requirement: The order of work is one file

`docs/DEVELOPMENT-CYCLES.md` SHALL be the single place that states the order of
work. Each cycle it lists SHALL carry: what the cycle produces, the command that
fails when the cycle is not finished, the host class that command runs on, the
change that owns the work, and what the cycle is not permitted to decide. Work
that fits no listed cycle SHALL either gain a cycle in that file or not happen.

#### Scenario: A cycle is claimed finished

- **WHEN** someone says a cycle is done
- **THEN** the file names a command for it and records the last run of that
  command, so the claim can be checked by running it rather than by trusting the
  sentence

#### Scenario: A cycle has no owner

- **WHEN** a cycle is listed whose work no open change carries a task for
- **THEN** the cycle is not schedulable, and the file names the receiving change
  that has to be minted or extended — a cycle is not an owner

### Requirement: An exit criterion is a command on a named host class

Every cycle's exit criterion SHALL be a command, and SHALL name one of the three
host classes defined in `openspec/project.md` — `(build)`, `(gpu)`, `(target)`. A
criterion that needs a machine other than the one it names is a defect in the
plan, not a pass waiting for hardware.

#### Scenario: A GPU call is marked as build work

- **WHEN** a cycle's exit criterion is written as running on `(build)` and it
  calls `nvidia-smi` or loads a driver
- **THEN** the criterion is rejected and rewritten against `(gpu)`, and the cycle
  becomes gated rather than finished-on-paper

#### Scenario: A criterion cannot fail

- **WHEN** a criterion is a sentence with no command, or a command that exits 0
  whatever the image contains
- **THEN** the cycle is recorded as not yet a cycle, with the missing command
  named as its first task

### Requirement: The base changes only on a measured GPU result

The base SHALL stay LFS 13.0-openrc with glibc, as pinned in `openspec/project.md`,
until the musl line produces one of two recorded results on a `(gpu)` host: the
proprietary NVIDIA userspace running on musl with `nvidia-smi` exiting 0 and a CUDA
program completing, or an explicit decision to ship the glibc-jail fallback. While
neither exists, a root built from another distribution's packages SHALL be
described as a measured candidate and never as the base.

#### Scenario: A second root exists and is called the base

- **WHEN** an Alpine-derived root reaches a login prompt with OpenRC as PID 1 and
  nobody has run the NVIDIA ladder on it
- **THEN** `docs/DEVELOPMENT-CYCLES.md` still names the base as LFS 13.0-openrc,
  and names the gate that a musl base has to pass — a booting root is evidence
  about busybox and musl, not about the GPU stack

#### Scenario: The ladder succeeds

- **WHEN** the NVIDIA userspace runs on musl on a `(gpu)` host and the result is
  recorded with its commands and outputs
- **THEN** changing the base is an `openspec` change of its own, with the measured
  result as its input; the switch does not happen as a side effect of a commit
  message

#### Scenario: The jail is chosen instead

- **WHEN** the ladder stops under its own stop rule and the glibc-jail fallback is
  adopted
- **THEN** the plan records that the shipping image contains a glibc rootfs
  alongside the musl base, `base.lock` names what that rootfs contains, and the
  claim "musl system" is rewritten wherever it appears

### Requirement: Every candidate base passes the same invariants

Whatever root a cycle produces SHALL pass the same checks, run against that root:
`scripts/build/check-no-systemd.py`, `scripts/build/check-amd64-only.py`, and
`scripts/build/check-services.py` where the root has services. The invariants
belong to the project, not to a distribution, and a check that read zero ELF
files is a scan that did not happen.

#### Scenario: A musl root passes with nothing scanned

- **WHEN** a check reports `binaries=0` or `elf=0` against a root that has
  binaries in it
- **THEN** the run is a failure of the check's invocation, not a pass, and the
  cycle stays open until the count is non-zero and the findings are zero

#### Scenario: A vendor blob breaks an invariant

- **WHEN** a package needed by a candidate base introduces a 32-bit object or a
  `libsystemd` resolution
- **THEN** the package is not installed, or the invariant is reopened in
  `openspec/` with the measurement attached; no exception list is added to a check

### Requirement: A gated cycle does not idle unblocked work

For every gated cycle the plan SHALL record the work runnable today on `(build)`,
separately from the work waiting on hardware or on an operator decision. A gate
SHALL block only the tasks that actually depend on it.

#### Scenario: The GPU answer has not arrived

- **WHEN** the driver branch and the GPU host are still unknown
- **THEN** the source studies and the ELF audit of the vendor archive stay
  schedulable on `(build)`, and the plan says so next to the gate rather than
  leaving the whole cycle looking blocked

### Requirement: A second candidate base does not get a second package list

Packages belonging to a candidate base SHALL be recorded in `base.lock`, the one
package list `openspec/project.md` names, with the same provenance every entry
carries: source, version, checksum. A per-distribution lock, manifest, or
installed-file list under another name SHALL NOT be started.

#### Scenario: An apk closure is pinned

- **WHEN** cycle 3 needs a reproducible set of Alpine packages
- **THEN** the pins go into `base.lock` with their checksums, and any tool that
  reads them is a reader of `base.lock`, not an author of a parallel list
