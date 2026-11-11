# The Rust toolchain ships in the installed image

Status: accepted 2026-11-10, premises corrected same day against `gpu-stack` D8

The image needs a Rust compiler to build first-party utilities and `uv`. We
decided the compiler stays in the installed system rather than being removed
after the build: removal is not a capability any change owns, the cost it saves
is unmeasured, and every maintenance cycle that rebuilds a first-party utility
would need the compiler back.

## Considered Options

- **Build-only, then remove.** Rejected. The removal contract is "delete exactly
  the paths recorded in the artifact's installed-file list"
  (`maintenance-and-updates` rollback), and the mechanism that captures such a
  list is adopted for installer-driven binary artifacts (`gpu-stack` 2.8, an
  `install -D` wrapper recording each path). Extending it to a source-built
  package is claimed nowhere, and no change specifies a removal step.
- **Ship it.** Accepted.
- **Ship it as a feature.** Not claimed. The machine is a compute node whose
  users may compile things; no part of this decision depends on that.

## Consequences

- The cost is a number, not an adjective: the installed bytes the toolchain adds
  are measured on a build and recorded as evidence with the `rust` entry owned
  by `userspace-and-ml-stack` 5.1, against a threshold fixed before the
  measurement.
- If removal is ever wanted, its prerequisite is an installed-file list for a
  source-built package — captured for installer-driven binaries and for nothing
  else today. That is the cost that makes this reversible only with work.
