# `base.lock` is an input; the build never writes it

Status: accepted 2026-11-10

A first-party entry cannot carry the checksum of the artifact it describes,
because that artifact does not exist until the phase that reads the entry has
run. We decided the built artifact's checksum lives in the phase's evidence and
the entry carries only what is knowable beforehand — the commit and the source
path — so that the lock stays an input the build is refused by rather than an
output it produces.

## Considered Options

- **Write the artifact checksum back into the lock.** Rejected twice over. A
  field that cannot be filled at the moment the refusal is computed is either
  skipped, which makes the check inconsistent, or filled by the build, and a
  file the build rewrites cannot refuse the build. It is also unsatisfiable for
  one entry in particular: the ledger file ships to `/srv/manifest`, so under
  this option its own entry would carry the checksum of the file containing
  that checksum.
- **Checksum in the evidence, joined to the lock.** Accepted. "Does this disk
  match the lock" survives the change: it becomes a join of the lock (commit,
  phase) against the phase evidence (artifact checksum, command output,
  evaluated criteria) that `build-orchestration` D11 already requires be kept,
  against the disk. Two files instead of one, and the evidence carries the
  build context a bare hash does not.

## Consequences

- The per-class field map is what makes the asymmetry legal rather than an
  oversight: `source-built` requires a checksum, `first-party` does not. The
  map has one machine-readable home, and prose names it without restating it.
- `base.lock` is derived and committed in the repository — a version bump is a
  commit naming the book revision and page — so "input" means reviewed input,
  not frozen input.
