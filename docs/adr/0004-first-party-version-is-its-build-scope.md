# A first-party entry's version is its build scope, not its file path

Status: accepted 2026-11-10

The version of a first-party `base.lock` entry is the last commit touching the
**build scope** that produced the artifact — the crate for a compiled binary,
the file for a copied one. We decided this against the obvious rule (last commit
touching the installed file's path) because that rule goes silent exactly when
it matters: a commit to a shared module changes the bytes of all four `bmc-*`
binaries while touching none of their source files.

## Considered Options

- **Last commit touching the installed file's path.** Rejected: it reports no
  change while installed content changes, which breaks the invariant the entry
  exists to provide — that the lock diff means something installed changed.
- **Repository HEAD as everyone's version.** Rejected: a documentation commit
  then changes the version of `/usr/bin/bmc-fan`, and the diff stops being
  readable.
- **Per-artifact build scope.** Accepted. Compiled artifacts take the crate
  scope, which moves all four binaries together because building the crate
  really does reproduce all four. Copied files take the file.

## Consequences

- Version means "produced by a build at commit X", not "this file's bytes
  changed at commit X". A rebuild at a new commit moves provenance even when the
  bytes are identical, which is the honest reading: what changed is what built
  it.
- The scope is a property of the artifact and belongs in the class's field map,
  not in the prose. Without it, "per-path commit" gets implemented literally and
  starts lying under the first shared module.
