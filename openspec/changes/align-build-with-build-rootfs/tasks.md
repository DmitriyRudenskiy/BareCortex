# Tasks: align the build with the build-rootfs file layout

## 1. Target files (old files stay until section 5)

- [x] 1.1 Create `scripts/build/build-rootfs.sh`: polyglot header (`'''exec' python3 '$0' "$@" # '''` + explanatory comment), then the `build.py` payload with `book.py` inlined as an internal section and `derive-base-lock` / `derive-test-expectations` added as subcommands (folding in `derive-base-lock.py` and `derive-test-expectations.py`; the `build.py:479` subprocess call becomes an in-payload function call)
- [x] 1.2 Create `scripts/build/run-qemu.sh`: rename of `vm-host.sh`, body and CLI unchanged, shebang/comment references updated
- [x] 1.3 Create `scripts/build/tests/test-arch.sh`: polyglot header + `check-amd64-only.py` payload
- [x] 1.4 Create `scripts/build/tests/test-all.sh`: polyglot header + `test_build.py` runner with `check-base-lock`, `check-no-systemd`, and `check-services` merged in as checks (one result line each), plus two new checks: (a) file-set inclusion — every file under `scripts/build/` (excluding `__pycache__`) is in the embedded build-rootfs reference list pinned at `c9fa26adeb66e247afbac33510c9479cf4b89f4a` (list + SHA embedded, offline); (b) doc coverage — `README.md`'s `## <relative-path>` headings match the tree in both directions
- [x] 1.5 Write `scripts/build/README.md` from `docs/build/BUILD-STEPS.md`: build-rootfs README style, one `## <relative-path>` section per build file (usage + CLI), keep the phase table, the checks list, and "What the build refuses to do"

## 2. References

- [x] 2.1 Update root `test_*.py` (four files) to invoke the new `.sh` entries via subprocess, preserving their assertions
- [x] 2.2 Update name references: `README.md` (table rows + `python3 scripts/build/build.py --help` usage line), `phases.toml` comments and the `sources` phase entry prose, `build-overrides.toml` comments, `base.lock` derivation comment, `openrc/services.toml` comment, `class-map.toml` comment
- [x] 2.3 Verify how the build guest receives `~/barecortex-vm/vm.sh` (manual copy or seed flow — no copy step exists in `phases.toml` or the overrides) and point its source at `scripts/build/run-qemu.sh`; the guest-side name stays `vm.sh`

## 3. Verification before deletion

- [x] 3.1 Run `scripts/build/tests/test-all.sh` with the old files still present: the file-set check must now FAIL naming the ten old files (proves the check works), all other checks must pass
- [x] 3.2 Run the root `test_*.py` suite: all pass against the new entries
- [x] 3.3 Smoke: `scripts/build/build-rootfs.sh status` and `pages first-boot` print the same output as `build.py` did

## 4. Deletion

- [x] 4.1 Delete `scripts/build/{build.py,book.py,vm-host.sh,test_build.py,check-base-lock.py,check-no-systemd.py,check-services.py,check-amd64-only.py,derive-base-lock.py,derive-test-expectations.py}` and any `__pycache__`
- [x] 4.2 Delete `docs/build/` (BUILD-STEPS.md is now `scripts/build/README.md`); update any `docs/*.md` link that pointed at `docs/build/BUILD-STEPS.md` (BUILD-LOG.md, PREBUILD.md, HANDOFF-VM.md, GENTOO-AS-BASE.md, PHILOSOPHY.md, INIT-LAYER.md) to the new path

## 5. Final check

- [x] 5.1 Run `scripts/build/tests/test-all.sh`: every check passes, file-set check green, doc coverage green
- [ ] 5.2 Run one full `scripts/build/build-rootfs.sh run <phase>` (cheapest already-passed phase) and confirm the evidence record format is unchanged — needs the build host (QEMU is not on this Mac); `status` and `pages` verified against the real records in the meantime
