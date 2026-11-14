# Design: align the build with the build-rootfs file layout

## Context

See proposal.md for motivation. Current state: `scripts/build/` holds ten
files — a Python orchestrator (`build.py`, 1372 lines) with a book-page
parser (`book.py`, 217), four check scripts, two derivation scripts, a test
runner (`test_build.py`), and `vm-host.sh` (271, the build-host VM driver);
the build documentation is `docs/build/BUILD-STEPS.md` (70 lines, a phase
table). The build-rootfs reference (`c9fa26adeb66e247afbac33510c9479cf4b89f4a`)
lays its build out as a handful of named shell entries plus `tests/`, with a
README that documents the build file by file.

Constraints: the build must keep working — all sixteen trust checks, the
phase CLI, evidence records, and the guest's `~/barecortex-vm/vm.sh` calls
inside `build.py` are load-bearing; the relayout must not change what a phase
does.

## Goals / Non-Goals

**Goals:**
- Five-file build tree, every name a build-rootfs name: `build-rootfs.sh`,
  `run-qemu.sh`, `README.md`, `tests/test-all.sh`, `tests/test-arch.sh`.
- A check that fails on any build file without a build-rootfs counterpart and
  on any build file missing from the README.
- Behavior identical before/after (rename, merge, relocate only).

**Non-Goals:**
- The repo as a whole: root-level data files (`phases.toml`, `base.lock`,
  `build-overrides.toml`, `test-expectations.toml`, `class-map.toml`, the
  four root `test_*.py`) and `docs/` outside `docs/build/` stay where they
  are; only their references to moved files are updated.
- Adopting build-rootfs *features* (docker-based `type-*` builds, initrd,
  kernel builds): the invariant is one-directional.
- Changing the guest-side `vm.sh` name or the phase/evidence semantics.

## Decisions

### D1 — Scope: the invariant covers `scripts/build/` plus the build doc

The user requirement ("no project files that don't exist in build-rootfs")
applied to the whole repo would delete the project's data, docs, and plan
files, none of which build-rootfs can correspond to. Scope is therefore the
build tree: `scripts/build/**`, and the build documentation, which moves
into the tree as `scripts/build/README.md` (build-rootfs's own build doc is
its root `README.md`). `docs/build/` is deleted after the move.

### D2 — Python hosted under `.sh` names: polyglot header

build-rootfs has no `.py` file names to claim, so the Python programs move
into shell-named files using a two-line polyglot:

```sh
#!/bin/sh
'''exec' python3 '$0' "$@" # '''
# everything below is Python; the line above execs python3 on this file
```

Shell parses line 2 as `exec python3 $0 "$@"` (the words `''`+`'exec'`,
`python3`, `'$0'`, `"$@"`, then a comment). Python parses it as a
triple-quoted string literal expression. Rejected alternatives: keep the
`.py` files (violates the invariant); rewrite the orchestrator in bash
(1600 lines of logic re-exposed to behavioral risk for zero benefit);
heredoc-fed `python3 -` (consumes stdin and is fragile with a large payload).

Affected files: `build-rootfs.sh`, `tests/test-all.sh`,
`tests/test-arch.sh`.

### D3 — Merge policy

- `book.py` inlines into `build-rootfs.sh`'s payload: `build.py` is its only
  importer, and the parser exists to serve the orchestrator.
- `derive-base-lock.py` / `derive-test-expectations.py` become
  `build-rootfs.sh` subcommands (`derive-base-lock`,
  `derive-test-expectations`): they derive build inputs, so they belong to
  the entry point, not to a test. The internal call at `build.py:479`
  becomes an in-payload function call.
- The three checks without a build-rootfs test name to claim
  (`check-base-lock`, `check-no-systemd`, `check-services`) merge into
  `tests/test-all.sh` alongside the `test_build.py` runner: build-rootfs's
  `tests/test-all.sh` is its run-everything entry, which is exactly this
  role. `check-amd64-only` takes the only fitting name, `tests/test-arch.sh`.
- `vm-host.sh` → `run-qemu.sh` by rename; CLI and body unchanged (it is
  already pure bash).

### D4 — Reference list embedded, pinned, offline

`tests/test-all.sh` carries the build-rootfs file list as a Python constant
with the commit SHA in an adjacent comment. The check is a pure set
comparison, no network. Updating the pin is a deliberate two-line change
(list + SHA) that shows up in review. Fetched-at-test-time rejected: the
trust checks must run offline (the book cache already sets this pattern).

### D5 — Documentation coverage is a set comparison

The README is rewritten with one `## <relative-path>` heading per build file
(`## build-rootfs.sh`, `## run-qemu.sh`, `## tests/test-all.sh`,
`## tests/test-arch.sh`), each section giving usage, in the build-rootfs
README style. The check parses those headings and compares against the tree
in both directions. Free-form prose documentation would make the check
fuzzy; headings keep it exact.

### D6 — Root `test_*.py` become subprocess tests

The four root-level `test_*.py` import the check modules directly; after the
merge there is nothing importable at those paths. They are rewritten to run
the `.sh` entries via `subprocess` and assert on their output; their
assertions are preserved and their locations (repo root) are unchanged —
they are outside the invariant's scope.

### D7 — Guest `vm.sh` keeps its name

`build.py` drives the build guest through `~/barecortex-vm/vm.sh`; that
guest-side file is out of scope and keeps its name. Only its copy source
moves (`vm-host.sh` → `run-qemu.sh`). How the guest currently receives the
script is not a repo step (no copy in `phases.toml` or the overrides), so
implementation must verify the actual origin (manual copy or seed flow) and
update it.

## Risks / Trade-offs

- Polyglot header is exotic → mitigated by a comment on every affected file
  and by `tests/test-all.sh` itself running the file under both interpreters
  (sh syntax check + python compile check) before anything else.
- One 2000+ line `build-rootfs.sh` is harder to navigate than four modules →
  accepted: the file-set invariant is the point; the payload keeps the old
  module structure as internal sections.
- Merging checks into `test-all.sh` couples them to one file → each merged
  check keeps its function and its own exit reporting; `test-all.sh` prints
  one line per check as before.
- A future build-rootfs rename silently breaks the pin → the embedded list
  pins behavior regardless of upstream renames; drift is only an issue when
  the pin is deliberately updated.
- The `sources` phase entry prose and several TOML comments name the old
  files → a grep sweep task catches stragglers; the doc-coverage check
  catches README drift.

## Migration Plan

1. Create the five target files (content moved verbatim, polyglot headers
   added); keep the old files until step 4 so the build is runnable at every
   commit in the sequence.
2. Point the references at the new names (TOML comments, `README.md`,
   root `test_*.py`, the `build.py:479` internal path).
3. Run `tests/test-all.sh` (new) — all sixteen trust checks must pass before
   the old files are deleted; run the root `test_*.py` too.
4. Delete the ten old files and `docs/build/`.
5. Run `tests/test-all.sh` once more (file-set + doc-coverage checks now see
   the final tree) and one `build-rootfs.sh status` smoke run.

Rollback: the change is a single squashable sequence of commits; reverting
restores the old tree wholesale. No data migration, no evidence rewrites.

## Open Questions

None — the mapping is fixed by the proposal's table; anything found during
implementation (e.g. the `vm.sh` copy origin, D7) is a task, not a design
change.
