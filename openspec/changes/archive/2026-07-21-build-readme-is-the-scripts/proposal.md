## Why

The build-rootfs reference keeps its documentation as a copy of the build
scripts themselves: the README is one section per script, and nothing is
maintained beside the code. Our `scripts/build/README.md` is hand-written prose
next to the scripts — it already carries the phase table (data that lives in
`phases.toml`) and the check list (data that lives in the check functions' own
docstrings), so any edit to a script's CLI or to a phase leaves the README
silently stale. The decision: the build documentation is a copy of the build
scripts, the way `base.lock` and `test-expectations.toml` are copies of their
sources — derived, and checked for drift.

## What Changes

- The documentation content of `scripts/build/README.md` moves into the
  scripts' own documentation blocks (the headers of `build-rootfs.sh`,
  `run-qemu.sh`, `tests/test-all.sh`, `tests/test-arch.sh`), the way the
  reference documents each script in one place.
- New subcommand `build-rootfs.sh derive-readme`: regenerates `README.md`
  from the scripts' documentation blocks, the phase table read from
  `phases.toml`, and the check list read from the check functions' docstrings.
  The README becomes a generated file ("generated, do not edit"), like
  `base.lock` and `test-expectations.toml`.
- `tests/test-all.sh` gains `check_readme_is_the_scripts`: the README must
  match the generated output byte-for-byte, and a mismatch names
  `derive-readme` as the fix. It replaces `check_readme_documents_every_file`
  (section-name coverage is a strict weakening of byte-for-byte equality).

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `build-file-layout`: the README requirement changes from "documents the
  build tree file by file" to "is the build scripts' own documentation —
  generated from them and checked byte-for-byte".

## Impact

- `scripts/build/README.md` — becomes a generated file; content moves into the
  scripts' headers.
- `scripts/build/build-rootfs.sh` — new `derive-readme` subcommand; the
  driver's documentation block gains the README's prose (intro, refusal list).
- `scripts/build/run-qemu.sh`, `scripts/build/tests/test-all.sh`,
  `scripts/build/tests/test-arch.sh` — documentation blocks gain the README
  sections that currently describe them.
- No change to build behavior: the driver, the phases, the checks' logic are
  untouched; only where their documentation lives changes.
