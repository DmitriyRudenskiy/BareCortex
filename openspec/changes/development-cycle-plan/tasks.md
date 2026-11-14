# Tasks: development-cycle-plan

Scope note: everything here is `(build)` except the one task that loads the
driver and runs CUDA on the GPU, 5.5, which is `(gpu)` and gated on a machine
the operator has not named.
Nothing here edits the chapters 8–10 build in flight, and nothing here changes the
base. Group 1 is the artifact; groups 2–6 are the cycles it describes, and each
group carries only the tasks this change adds — the work a cycle inherits from its
owning change stays in that change.

## 0. Hosts and the two answers

- [x] 0.1 Name the host classes for every task below, per `openspec/project.md`:
      `(build)` is the x86_64 Linux host at `192.168.0.86` and the disposable guest
      on it (`docs/HANDOFF-VM.md`); `(gpu)` is an NVIDIA machine that is not the
      target, **not yet identified**; `(target)` appears in no task here. Named
      2026-07-21: every task is `(build)` except the one that loads the driver
      and runs CUDA on the GPU, 5.5, which is `(gpu)`
- [x] 0.2 (build) Record the two operator answers the gate depends on, and what is
      schedulable without them: **(a) answered 2026-11-14 — driver branch 580**,
      so userspace and modules come from `NVIDIA-Linux-x86_64-580.196.07.run`
      (x86_64 sha256 `0aab0518db53cf73c412dc7f32bbc850ca95e839d8923d0de4bff42c1c9169ce`),
      closed `kernel/`, recorded as a ruling in `nvidia-musl/progress.md`; the GPU
      model itself is still unnamed, which `nvidia-musl/TASK.md` still needs for
      access and for the GSP-firmware step. **(b) still open** — whether to stand
      up an Alpine VM on `(build)` for the static work. Neither blocks tasks 1.x,
      5.2 or 5.3

## 1. Write the plan so that it can be contradicted

- [x] 1.1 (build) Write `docs/DEVELOPMENT-CYCLES.md`: four cycles and one invariant, each
      with what it produces, the command that fails when it is not finished, the
      host class, the owning change, and what it may not decide. Its first lines
      say that cycle 1 continues on its current path and that this file changes
      what comes after it, not what is running. Every figure cites the document
      that measured it (`docs/ALPINE-AS-BASE.md`, `docs/GENTOO-AS-BASE.md`,
      `nvidia-musl/report/01-study-chimera.md`)
- [x] 1.2 (build) In the same file, map every open change to a cycle — the twelve
      in `openspec/changes/` at the moment of writing (2026-07-21) plus
      `nvidia-musl/` — so that no claim is handed to a cycle, which is not an
      owner. Derive the list from `openspec list --json` at the moment of writing
      and say which command produced it
- [x] 1.3 (build) Add the file to the Documentation list in `README.md`, one line,
      the way `docs/INIT-LAYER.md` is listed
- [x] 1.4 (build) Write the `cycle-plan` subcommand of `tests/test-all.sh` —
      `sh scripts/build/tests/test-all.sh cycle-plan [--file F] [--explain]` —
      because the build-rootfs name set leaves no room for a top-level
      `scripts/build/check-cycle-plan.py`. Every cycle section must carry an exit
      command in backticks and one of `(build)`/`(gpu)`/`(target)`, and must name
      an owning change that exists under `openspec/changes/` (or a workstream like
      `nvidia-musl/`); fail without `--file`, and fail if a cycle names `(gpu)`
      while its exit command is a `nvidia-smi` or driver call marked `(build)`.
      `--explain` prints the rules, per the `no-systemd` subcommand of the same
      file (design D1, and `openspec/project.md`: a check that cannot fail is a
      defect)
- [x] 1.5 (build) Write `test_check_cycle_plan.py` in the style of
      `test_check_no_systemd.py`:
      a cycle with a command, a host class and a real owning change passes; a cycle
      with prose instead of a command fails; a cycle whose owning change does not
      exist fails; a file with no cycles fails rather than passing vacuously;
      a cycle that runs `nvidia-smi` without `(gpu)` fails. Run:
      `python3 test_check_cycle_plan.py`

## 2. Cycle 1 — LFS to a bootable base

- [x] 2.1 (build) Record cycle 1's exit as it already is defined, not as a new
      one: `sh scripts/build/build-rootfs.sh status` reporting `first-boot`
      passed, whose own exit criteria are in `phases.toml`. The chapters 8–10 run
      in flight belongs to that session and to `base-system-and-init`; this change
      adds no task to it
- [ ] 2.2 Run the three invariants against the assembled image root once it exists
      `(build)` — `sh scripts/build/tests/test-all.sh no-systemd --root <image>`,
      `sh scripts/build/tests/test-all.sh services --inventory <image>`,
      `sh scripts/build/tests/test-arch.sh --root <image>` — and record each
      command's counts in the cycle file. This closes `amd64-only-userspace` 2.4
      and is the first real run of all three on one root
- [x] 2.3 Record in the cycle file that `elf=0` or `binaries=0` is a failed
      invocation and not a pass, with the command that produced the non-zero count
      — recorded in the invariant section, and it is a rule for the human record:
      neither script guards the zero case today (`test-arch.sh` and the
      `no-systemd` scan both exit 0 on an empty census), so a record carries the
      command and its non-zero count. The Alpine run of 6.2 is the exemplar:
      `sh scripts/build/tests/test-arch.sh --root <candidate root>` → `elf=84 findings=0`

## 3. Cycle 2 — the build driven from the build root

- [x] 3.1 (build) Hand the requirement to `build-orchestration` as a task in
      `openspec/changes/build-orchestration/tasks.md`, with the observed evidence:
      the pipeline is driven over `ssh -J user@192.168.0.86 -p 2222 bct@127.0.0.1`,
      the guest currently closes the connection, and neither
      `~/.ssh/barecortex_vm_ed25519` nor `~/.config/barecortex/vm.env` exists on
      the laptop — so the build cannot be resumed. The requirement: from a clean
      build root holding the sources and this repository, one command runs a phase
      to the same evidence, with no host-side keychain and no absolute path outside
      the root (design D3)
- [x] 3.2 (build) In that same task, name the check that proves it: a fresh root with no
      `$HOME` state resumes a phase and writes the same `record.json` shape. A
      requirement with no such command is a sentence, and `build-orchestration`
      already says so about itself
- [ ] 3.3 (build) Do not implement cycle 2 from this change. It owns no code, and
      a second change editing `scripts/build/build-rootfs.sh` while chapters 8–10
      are running is a conflict, not a plan

## 4. Cycle 3 — busybox and musl as a measured candidate

- [x] 4.1 (build) Re-run `artifacts/handoff-vm/alpine-probe/probe.sh` if the Alpine index
      moved since the pinned minirootfs sha256, and let it rewrite the numbers
      rather than editing `docs/ALPINE-AS-BASE.md` by hand (`alpine-as-base` 3.3)
      — the condition was checked 2026-11-14 and is not met: the published sha256
      of `alpine-minirootfs-3.24.4-x86_64.tar.gz` is byte-for-byte the pinned value
      (`cdaa38cb…923012`, `alpine-as-base` 0.2), so the index has not moved and no
      re-run is owed; the document's numbers stand. The same check closed
      `alpine-as-base` 3.3, which states the condition
- [x] 4.2 (build) Decide where a candidate root's package pins live, and write the decision
      into `base.lock` as entries with source, version and checksum — no
      `apk.lock`, no second list, per `openspec/project.md` and this change's spec
      — decided and written: 8 `binary-distribution` entries (minirootfs + the
      7-package OpenRC closure), each with url, version, md5 computed from the
      fetched artifact (honest here because the artifact was fetched, unlike the
      book's packages), sha256 published and verified equal, licence from each
      apk's `.PKGINFO`, doc `docs/ALPINE-AS-BASE.md`, and `installed_files` the
      probe's own manifest format (minirootfs file list added to the probe
      evidence). `test-all.sh base-lock` green: packages=97,
      binary-distribution=8, missing=0. Surviving a re-derive is
      `build-orchestration` 2.6 — the handoff this task writes, because
      `derive-base-lock` rewrites the whole file and the section header in
      `base.lock` says so
- [ ] 4.3 Boot the candidate root under QEMU on `(build)` with OpenRC as PID 1 and
      run all three invariants against it, recording the ELF count so a scan of
      nothing cannot be read as a pass. This is cycle 2's proof as much as cycle
      3's: a second root is the cheapest way to show the pipeline is not LFS-shaped
- [x] 4.4 (build) While the gate is shut, describe the result in the cycle file as
      a measured candidate and not as the base — the spec scenario this task
      exists to satisfy

## 5. Cycle 4 — the GPU stack on musl, the gate itself

- [x] 5.1 Get answer 0.2(a) and record it, because the driver branch is an input
      to every task in this group after 5.3 — **580**, 2026-11-14
- [x] 5.2 Phases 1.2–1.4 of `nvidia-musl/TASK.md` `(build)`: the git-bruh recipe,
      the Void glibc-jail notes, MuslMan — one report each under `nvidia-musl/report/`.
      No GPU, no Alpine VM, no dependency on cycle 1 or 3 (design D4)
- [x] 5.3 Fetch the pinned `.run` (580.196.07, checksum above), `--extract-only`
      outside git, and run `nvidia-musl/tools/elf-audit.py` over it →
      `report/02-elf-audit.md`: the maximum `GLIBC_2.NN` the vendor binaries demand,
      which is the number that picks the jail's rootfs and the one number the whole
      musl plan currently lacks `(build)`
- [x] 5.4 (build) Only after group 5.2–5.3, start Phase 2 (REA) as `TASK.md`
      orders it. The order is a hard condition of that task, not a preference —
      started in order: Phase 1 closed with its checkpoint report, then 2.0 and
      2.2-lite ran (the headless CUDA set loads on stock musl); the (gpu) half of
      Phase 2 is 5.5
- [ ] 5.5 The ladder on `(gpu)`: driver loaded, `nvidia-smi` exiting 0, a CUDA
      program completing, on musl. Two consecutive unresolved ladder steps invoke
      the stop rule and the glibc-jail fallback, as `TASK.md` states it
- [ ] 5.6 (build) Record whichever outcome arrives, with its commands, in the cycle file —
      and then act on it: success mints an `openspec` change to move the base, with
      the measurement as its input; the jail rewrites every "musl system" claim and
      puts the jail's contents in `base.lock`
- [x] 5.7 (build) Add the cross-product task to `gpu-stack`: what its packages, service
      files and `base.lock` entries become under each outcome (design D5). It sits
      beside the 1.5 already there about 32-bit code

## 6. The invariant that was cycle 5

- [x] 6.1 (build) State it once, in the cycle file, as
      `sh scripts/build/tests/test-arch.sh --root <image>` plus the two facts
      already measured: Gentoo's nomultilib
      stage3 has 1 487 ELF files and none is 32-bit; the check agrees with the
      probe's census at 1 487
- [x] 6.2 Measure, do not assume, that an Alpine x86_64 root passes it — the
      Alpine probe recorded that its 84 ELF files resolve, not that they are all
      `ELFCLASS64`, so the invariant has to be run against that root too `(build)`
      — measured 2026-11-14, it passes: the candidate root rebuilt from the pinned
      minirootfs (sha256 `cdaa38cb…923012`, published and computed equal) plus the
      7-package OpenRC closure scans `elf=84 findings=0`, all 84 `ELFCLASS64`/
      `EM_X86_64`, census agreeing with the probe's 84; path count matches exactly
      (515 + 171 = 686). Two honest notes in the cycle file: the rebuild ran on
      the laptop because `(build)` is unreachable (the scan reads twenty bytes per
      file, no host dependency), and its byte totals sit 59 B under the
      `docs/ALPINE-AS-BASE.md` figures — measurement-method difference, not drift
      (the sha256 is byte-for-byte the pinned value; see 4.1)

## 7. What would make this change worth archiving

- [x] 7.1 (build) `docs/DEVELOPMENT-CYCLES.md` exists, passes
      `sh scripts/build/tests/test-all.sh cycle-plan`, and names the gate's
      current state with the date and the command that would close it
- [ ] 7.2 (build) The gate has an outcome recorded — musl works, musl falls back to a jail,
      or the operator has decided not to pursue musl — and the base is stated
      consistently in `openspec/project.md`, `README.md` and this file
- [x] 7.3 (build) No cycle in the file is owned by nothing, and no open change is
      missing from the mapping
