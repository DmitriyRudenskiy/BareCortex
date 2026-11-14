# Tasks: the build README is a copy of the build scripts

## 1. Documentation blocks into the scripts

- [x] 1.1 `scripts/build/build-rootfs.sh`: add one `# >>> docs` / `# <<< docs` region carrying the README's intro ("The build tree" text) and the "What the build refuses to do" list
- [x] 1.2 `scripts/build/run-qemu.sh`: add the docs region with the current README `## run-qemu.sh` section content
- [x] 1.3 `scripts/build/tests/test-all.sh`: add the docs region with the current README `## tests/test-all.sh` section content (usage + description, minus the check list — that is derived); rewrite the 13 trust-check functions' docstrings as the one-line README items
- [x] 1.4 `scripts/build/tests/test-arch.sh`: add the docs region with the current README `## tests/test-arch.sh` section content

## 2. Derivation

- [x] 2.1 `derive_readme()` in `build-rootfs.sh`: assemble the README from the five docs regions (marker extraction, one `#`/`# ` stripped per line), the phase table read from `phases.toml` (phase, chapter, page count, checkpoint, and the phase's `entry` criteria replacing the hand-summarized "what passing means" column), and the trust-check list read from `TRUST_CHECKS` + the functions' `__doc__` (importing `tests/test-all.sh` lazily inside the function, per design D3); output carries the generated-file header naming the command
- [x] 2.2 Wire the `derive-readme` subcommand into the CLI (argparse choices, dispatch, usage line in the header and docs region)

## 3. Check and verification

- [x] 3.1 Regenerate `scripts/build/README.md` with the subcommand and diff against the previous README: every section's information must be present (the "what passing means" column now reads the phase's real criteria from `phases.toml` instead of the summary — that is the intended change, everything else is a regression)
- [x] 3.2 `tests/test-all.sh`: add `check_readme_is_the_scripts` (byte-for-byte against `derive_readme()`'s output, mismatch names `build-rootfs.sh derive-readme` as the fix) and delete `check_readme_documents_every_file`
- [x] 3.3 Run the full offline suite: `tests/test-all.sh` green (file-set, new check, all trust checks), and the four root checker tests pass
- [x] 3.4 Negative test: hand-edit `README.md` → the suite fails naming the fix; run `derive-readme` → the suite is green again
