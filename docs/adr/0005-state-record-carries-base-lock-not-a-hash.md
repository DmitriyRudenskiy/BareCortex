# The state record carries `base.lock`, not a hash of it

Status: accepted 2026-11-10

A `base.lock` change must invalidate the phase that installs the changed entry
and every phase after it, which means the build has to say *which* entry moved.
A recorded hash answers only "did anything change", so localized invalidation is
uncomputable from it and silently degrades to "rebuild everything after
`config-files`". We decided the state record carries the lock content itself,
in the record the checkpoint already captures at every phase boundary, so the
diff is available to the phase that needs it.

## Considered Options

- **Record the identity only.** Rejected: it makes the mismatch check mandatory
  (`build-orchestration` 4.1 already records the lock identity) and its useful
  result uncomputable, which is the same defect one level up — detection nobody
  consumes.
- **Carry the lock content.** Accepted. The state record is already captured at
  every phase boundary by the checkpoint, so a larger record needs no new
  mechanism.

## Consequences

- Invalidation is one formula with no cases: add, change, or remove an entry
  restarts its owning phase, because the lock is an input and the input moved.
- It is only sound if every entry is named by exactly one phase row's
  `installs[]`. An entry owned by no phase invalidates nothing and ships stale,
  so the coverage check fails naming the orphan — the same shape as the
  unaccounted-file refusal, extended to the phase table.
- The phase row is the single home of `{name, position, requires[], installs[]}`:
  ordering, entry requirements, and invalidation are one artifact rather than
  three that can disagree.
