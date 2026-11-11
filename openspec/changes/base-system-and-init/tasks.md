# Tasks: base-system-and-init

Scope note: nothing in this change requires the **target** machine. Several tasks require a **build host** and are marked `(build)` per the classes in `openspec/project.md`; 5.2 and 5.4 additionally require a completed build, which is why they were previously mislabelled as answerable from a build tree. Group 0 names the host or blocks them.

## 1. Resolve the open questions first

- [ ] 1.1 Read OpenRC's own build definition for its required and optional dependencies, and record the answer to OQ2 from that file rather than from memory
- [ ] 1.2 ~~Decide the PID 1 supervisor~~ **Closed 2026-11-08, corrected same day:** the pinned edition makes **`openrc-init` PID 1** — `chapter08/openrc.html` symlinks `/sbin/init → openrc-init` and `/sbin/shutdown → openrc-shutdown` and writes `/sbin/poweroff` itself; there is no sysvinit package among its 85. An earlier answer here claiming sysvinit-with-runlevel-invoked-OpenRC was an inference from other packagings and is superseded
- [ ] 1.3 Replace "follow BLFS" with the list of BLFS packages the image actually uses (OQ3), one citation per package — the list itself is derived into `base.lock` by `userspace-and-ml-stack` group 3; this task records that it exists and that no second list is kept
- [ ] 1.4 Note in the design that the count of systemd-linked packages (OQ4) is unknown until the scan in group 5 runs
- [ ] 1.5 Record the OpenRC edition pin: the URL (`~zeckma/openrc/lfs/`), the edition version, the revision or snapshot date taken, and the book version it patches. Record the same for the BLFS OpenRC edition — **r13.0-194, dated 2026-03-23, an SVN rendering that targets the LFS *development* book** — and state the consequence of that mismatch next to the base pin rather than in a change that cannot see it: BLFS instructions here may assume a base newer than the 13.0-openrc pinned above, and every BLFS-derived entry is checked against the pinned base before it is installed
- [ ] 1.5a Record which of the two books wins when they disagree on a base package version, and who re-verifies the BLFS entries when the base pin moves — without this, the mismatch is discovered package by package at build time
- [ ] 1.6 Record that LFS 13.x publishes no SysVinit edition (`13.0/`, `13.1/` → 404) and that the SysVinit line stops at 12.4, so the choice of base edition is a version decision and not only an init decision
- [ ] 1.7 Reconcile the three kernel numbers in one place and state which the build uses: the base book pins **Linux 6.18.10**, `honest-scope-and-config-generation` audited its symbols against **v6.12**, and mainline is ahead of both. Record what the audit has to be redone against when the pin moves (moved here from `build-orchestration`, where it was an orphan task)

## 2. Fix documentation that does not resolve

- [ ] 2.1 Point Phase 1 at the OpenRC edition (`~zeckma/openrc/lfs/`), stating the edition version and the pinned revision from 1.5 — the main view index does not list this book, so the citation has to carry the path
- [ ] 2.2 Replace the "BLFS OpenRC Edition" instruction with the per-package citations from 1.3, and state that the BLFS OpenRC material is a snapshot with no release
- [ ] 2.3 Replace the GLFS citation with the NVIDIA driver installation guide, the CUDA Installation Guide for Linux, and the CUDA Compatibility guide
- [ ] 2.4 Document the init delta against its upstream: where the OpenRC edition covers the substitution, cite it; where it does not, say that no book covers it and name what breaks if the gap is unmaintained
- [ ] 2.5 Remove every reference to `lfs/view/stable-openrc/` and `blfs/view/stable-openrc/` from `README.md`, `docs/`, and `scripts/` — those paths 404 — and replace them with the paths that resolve
- [ ] 2.6 State the "No systemd" claim as what it is — no systemd as init or service manager, no `logind`, no `vconsole` — and name the exception the base book itself creates: `chapter08/udev.html` builds udev from `systemd-259.1`. A flat "no systemd" is unfalsifiable and the link scan is what makes the precise version checkable
- [ ] 2.7 Record that the pinned edition has **no PAM** (the string is absent from its package list; OpenRC is built `-D pam=false`), so anything in this repository that assumes PAM — a `pam_limits` line, a `pam.d` file, `pam_systemd` in a scan — is describing a system this project does not build. `memlock` is set with `rc_ulimit` in the service

## 3. `base.lock`

- [ ] 3.1 Define the `base.lock` format: name, version, source URL, checksum, upstream remote, revision, patch series. **This change owns the format; `build-orchestration 2.1` owns the derived contents** — one file, one truth, and the format has to accommodate entries other changes add
- [ ] 3.2 Consume the file derived by `build-orchestration 2.1` and verify it covers every package in the base set — this task does not retype the book's package list, and a second copy of it is the defect this line exists to prevent
- [ ] 3.3 Fail the build on checksum mismatch, naming the package, with no fallback to an unpinned source *(blocked by `build-orchestration 3.1` — there is no build to fail until the phase table exists)*
- [ ] 3.4 Fail the build on an image package with no `base.lock` entry, naming the file and its owner *(same block; needs `build-orchestration` group 3)*
- [ ] 3.5 Move any existing local modification of an upstream package into a `patches/<pkg>/` series referenced from `base.lock`
- [ ] 3.6 Create `class-map.toml` at the repository root: one row per provenance class, each row naming the fields that class requires and the change owning it. Hand-authored — never emitted with the derived contents of `base.lock`, which is what makes it a separate file. Its rows are written by their owners (`gpu-stack 2.9`, `gpu-stack 2.10`), not here
- [ ] 3.7 Add the provenance class and that class's required fields to the per-entry record, the field list read from the class map rather than written into this task
- [ ] 3.8 Write the class-map completeness check: read the class rows from the class map, emit one machine-readable line per class naming class, completeness, and owner, and fail naming the path when the map is absent, unparseable, or empty — a check iterating zero rows is a pass it did not earn
- [ ] 3.9 Declare the set-list convention in the class map's schema: a field is checked against the class map only when the schema declares it enumerates the classes, and prose is never such a declaration

## 4. Init layer

- [ ] 4.1 Write the artifact substitution table: what the systemd edition would install (`systemctl`, `journalctl`, units, `logind`) against what the pinned OpenRC edition installs (`openrc-init` as `/sbin/init`, `/etc/runlevels`, `/etc/conf.d`, `/etc/init.d`, `rc-status`), and what replaces each
- [ ] 4.2 Document the PID 1 supervisor with the command that identifies it on a running host
- [ ] 4.3 Inventory the three existing services (`nvidia-persistenced`, `ai-training`, `gpu-fancontrol`): runlevel, requirements, failure mode. Behavioural ownership sits elsewhere — `gpu-stack` group 7 for fan control, `userspace-and-ml-stack` group 6 for the training service — and this inventory links rather than duplicates
- [ ] 4.4 Review each service against the rule that success requires having done the work, and fix any that report success on finding nothing
- [ ] 4.5 Implement the check that fails when a file under `openrc/init.d/` has no inventory entry, and run it over the tree — the requirement is not met by a list

## 5. Systemd link scan

- [ ] 5.1 Implement the scan over the image for `libsystemd`, `libelogind`, `pam_systemd`, `libnss_systemd`, failing with the binary and library named
- [ ] 5.2 **(build)** Run it against a completed build and record the actual result — a nonzero count is the expected first outcome, not a regression. This needs a built image, not a source tree
- [ ] 5.3 Fix each hit with that package's own build option; do not add an exclusion list
- [ ] 5.4 **(build)** Wire the scan into the build so a package cannot reach the image unscanned, and demonstrate it on a package built with the option omitted
- [ ] 5.5 Update the README claim to name the command that verifies it

## 6. Language and dependency policy

- [ ] 6.1 Write the selection order — maintained port, then upstream as-is, then do not adopt — as a short policy document
- [ ] 6.2 State the accelerator-path exclusion (kernel, GPU driver, CUDA runtime, CPython, PyTorch) and the reason: they are C and CUDA by construction
- [ ] 6.3 Record why a rewrite program was rejected: the bootstrap dependency of a Rust compiler on linker, libc, and LLVM, and the fact that a rewrite has no upstream
- [ ] 6.4 State that modified upstreams live as patch series, never as directories in this repository
- [ ] 6.5 Adopt `uv` as the default environment manager in place of `pip`/`venv`, and record it as the mechanism that replaces container isolation — this is the "change the language" goal achieved by adoption rather than by rewriting
- [ ] 6.6 Record the adopt-when-needed list (`uv`, `ruff`, `tokenizers`, `polars`) as ports to reach for before any rewrite is proposed
- [ ] 6.7 Create the coverage ledger — `[keep]` entries with a one-line rationale each, `[rewritten]` entries with the usage evidence behind them — and state that the file, not a conversation, is the policy truth. It ships to `/srv/manifest/rust-coverage.toml` so an operator at a console with no network can read it; **nothing running on the installed system reads it**, the repository copy is authoritative, and an edit made on the target is overwritten by the next build. When it ships it takes its own `first-party` `base.lock` entry naming its build scope
- [ ] 6.8 State the three conditions a replacement must meet before it is written: usage evidence, parity evidence captured from the incumbent on this machine, and a rollback that has been executed once. Name the incumbent as moved, not deleted
- [ ] 6.9 Apply the conditions to the standing proposals from the external plan and record the verdicts: uutils coreutils passes as a maintained port; hand-written syslogd, cron, and an OOM watchdog fail it while sysklogd, cron, and earlyoom are maintained upstreams

## 7. Do not let external wording come back

Outside advice about this project has repeatedly asserted things that contradict decisions already taken here, or that describe a mechanism other than the one named. Some of it is already in the tree.

- [ ] 7.1 `kernel/.config:50` — the comment `(for GPU passthrough if needed)` is the origin of `CONFIG_VFIO`/`CONFIG_VFIO_PCI`. Remove it with the symbols (`honest-scope-and-config-generation` task 2.14); "in case it is needed" is not a requirement
- [ ] 7.2 `kernel/patches/README.md:24` — the row "Large-page allocations → `CONFIG_TRANSPARENT_HUGEPAGE_ALWAYS` + `nccl` env" cites an environment file this project deletes, and holds huge pages up as load-time acceleration with no measurement. Rewrite to what they actually do, or drop the row
- [ ] 7.3 `kernel/patches/README.md:26` — "Lock CPU clocks → `cpupower frequency-set -g performance" is a second mechanism beside the `cpufreq` service (`honest-scope-and-config-generation` task 7.1). One mechanism: the row points at the service
- [ ] 7.4 Record three decisions in `docs/PHILOSOPHY.md` so they stop arriving with the next consultation:
      IOMMU/VFIO do not return "in case a VM is needed", they return on a stated VM requirement; OOM during training is handled by `vm.overcommit_memory` and a cgroup v2 memory limit, not `ulimit`, which does not disable the OOM killer (`INSTALL_GUIDE.md:148` is already correct); `nvidia-persistenced` is described by what it does — keeping driver state across process exits and driver unloads — not by "removing per-epoch initialisation delay", verified against `man nvidia-persistenced` from the installed driver
- [ ] 7.5 `README.md:12` — the `display-Xorg` badge advertises an option on a node whose documented default is no X server at all (`PHILOSOPHY.md:45`). Drop the badge or make it state the default
- [ ] 7.6 Check: `grep -rn "passthrough if needed\|cpupower frequency-set\|display-Xorg" .` matches nothing outside the text of these tasks

## 8. Verification

- [ ] 8.1 Every external URL in `docs/` and `README.md` resolves
- [ ] 8.2 No documentation file cites an edition path that does not resolve; the LFS OpenRC edition is cited by its real path, version, and pinned revision
- [ ] 8.3 `base.lock` accounts for every package in a built image, demonstrated on a build tree **(run task — needs a build host and a completed build, see `build-orchestration` group 0)**
- [ ] 8.4 The link scan fails a deliberately mis-built package, demonstrated on a build tree **(run task — same precondition)**
- [ ] 8.5 `openspec validate --strict` passes for this change
