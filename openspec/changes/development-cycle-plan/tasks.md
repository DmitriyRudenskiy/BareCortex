# Tasks: development-cycle-plan

Scope note: everything here is `(build)` except the two tasks that load a driver
or run CUDA, which are `(gpu)` and gated on a machine the operator has not named.
Nothing here edits the chapters 8–10 build in flight, and nothing here changes the
base. Group 1 is the artifact; groups 2–6 are the cycles it describes, and each
group carries only the tasks this change adds — the work a cycle inherits from its
owning change stays in that change.

## 0. Hosts and the two answers

- [ ] 0.1 Name the host classes for every task below, per `openspec/project.md`:
      `(build)` is the x86_64 Linux host at `192.168.0.86` and the disposable guest
      on it (`docs/HANDOFF-VM.md`); `(gpu)` is an NVIDIA machine that is not the
      target, **not yet identified**; `(target)` appears in no task here
- [x] 0.2 Record the two operator answers the gate depends on, and what is
      schedulable without them: **(a) answered 2026-11-14 — driver branch 580**,
      so userspace and modules come from `NVIDIA-Linux-x86_64-580.196.07.run`
      (x86_64 sha256 `0aab0518db53cf73c412dc7f32bbc850ca95e839d8923d0de4bff42c1c9169ce`),
      closed `kernel/`, recorded as a ruling in `nvidia-musl/progress.md`; the GPU
      model itself is still unnamed, which `nvidia-musl/TASK.md` still needs for
      access and for the GSP-firmware step. **(b) still open** — whether to stand
      up an Alpine VM on `(build)` for the static work. Neither blocks tasks 1.x,
      5.2 or 5.3

## 1. Write the plan so that it can be contradicted

- [ ] 1.1 Write `docs/DEVELOPMENT-CYCLES.md`: four cycles and one invariant, each
      with what it produces, the command that fails when it is not finished, the
      host class, the owning change, and what it may not decide. Its first lines
      say that cycle 1 continues on its current path and that this file changes
      what comes after it, not what is running. Every figure cites the document
      that measured it (`docs/ALPINE-AS-BASE.md`, `docs/GENTOO-AS-BASE.md`,
      `nvidia-musl/report/01-study-chimera.md`)
- [ ] 1.2 In the same file, map every open change to a cycle — the ten in
      `openspec/changes/` plus `nvidia-musl/` — so that no claim is handed to a
      cycle, which is not an owner. Derive the list from `openspec list --json` at
      the moment of writing and say which command produced it
- [ ] 1.3 Add the file to the Documentation list in `README.md`, one line, the way
      `docs/INIT-LAYER.md` is listed
- [ ] 1.4 Write `scripts/build/check-cycle-plan.py --file docs/DEVELOPMENT-CYCLES.md`:
      every cycle section must carry an exit command in backticks and one of
      `(build)`/`(gpu)`/`(target)`, and must name an owning change that exists
      under `openspec/changes/`; fail without `--file`, and fail if a cycle names
      `(gpu)` while its exit command is a `nvidia-smi` or driver call marked
      `(build)`. `--explain` prints the rules, per `scripts/build/check-no-systemd.py`
      (design D1, and `openspec/project.md`: a check that cannot fail is a defect)
- [ ] 1.5 Write `test_check_cycle_plan.py` in the style of `test_check_no_systemd.py`:
      a cycle with a command, a host class and a real owning change passes; a cycle
      with prose instead of a command fails; a cycle whose owning change does not
      exist fails; a file with no cycles fails rather than passing vacuously.
      Run: `python3 test_check_cycle_plan.py`

## 2. Cycle 1 — LFS to a bootable base

- [ ] 2.1 Record cycle 1's exit as it already is defined, not as a new one:
      `python3 scripts/build/build.py status` reporting `first-boot` passed, whose
      own exit criteria are in `phases.toml:178`. The chapters 8–10 run in flight
      belongs to that session and to `base-system-and-init`; this change adds no
      task to it
- [ ] 2.2 Run the three invariants against the assembled image root once it exists
      `(build)` — `check-no-systemd.py`, `check-services.py`, `check-amd64-only.py`
      — and record each command's counts in the cycle file. This closes
      `amd64-only-userspace` 2.4 and is the first real run of all three on one root
- [ ] 2.3 Record in the cycle file that `elf=0` or `binaries=0` is a failed
      invocation and not a pass, with the command that produced the non-zero count

## 3. Cycle 2 — the build driven from the build root

- [ ] 3.1 Hand the requirement to `build-orchestration` as a task in
      `openspec/changes/build-orchestration/tasks.md`, with the observed evidence:
      the pipeline is driven over `ssh -J user@192.168.0.86 -p 2222 bct@127.0.0.1`,
      the guest currently closes the connection, and neither
      `~/.ssh/barecortex_vm_ed25519` nor `~/.config/barecortex/vm.env` exists on
      the laptop — so the build cannot be resumed. The requirement: from a clean
      build root holding the sources and this repository, one command runs a phase
      to the same evidence, with no host-side keychain and no absolute path outside
      the root (design D3)
- [ ] 3.2 In that same task, name the check that proves it: a fresh root with no
      `$HOME` state resumes a phase and writes the same `record.json` shape. A
      requirement with no such command is a sentence, and `build-orchestration`
      already says so about itself
- [ ] 3.3 Do not implement cycle 2 from this change. It owns no code, and a second
      change editing `scripts/build/build.py` while chapters 8–10 are running is a
      conflict, not a plan

## 4. Cycle 3 — busybox and musl as a measured candidate

- [ ] 4.1 Re-run `artifacts/handoff-vm/alpine-probe/probe.sh` if the Alpine index
      moved since the pinned minirootfs sha256, and let it rewrite the numbers
      rather than editing `docs/ALPINE-AS-BASE.md` by hand (`alpine-as-base` 3.3)
- [ ] 4.2 Decide where a candidate root's package pins live, and write the decision
      into `base.lock` as entries with source, version and checksum — no
      `apk.lock`, no second list, per `openspec/project.md` and this change's spec
- [ ] 4.3 Boot the candidate root under QEMU on `(build)` with OpenRC as PID 1 and
      run all three invariants against it, recording the ELF count so a scan of
      nothing cannot be read as a pass. This is cycle 2's proof as much as cycle
      3's: a second root is the cheapest way to show the pipeline is not LFS-shaped
- [ ] 4.4 While the gate is shut, describe the result in the cycle file as a
      measured candidate and not as the base — the spec scenario this task exists
      to satisfy

## 5. Cycle 4 — the GPU stack on musl, the gate itself

- [x] 5.1 Get answer 0.2(a) and record it, because the driver branch is an input
      to every task in this group after 5.3 — **580**, 2026-11-14
- [ ] 5.2 Phases 1.2–1.4 of `nvidia-musl/TASK.md` `(build)`: the git-bruh recipe,
      the Void glibc-jail notes, MuslMan — one report each under `nvidia-musl/report/`.
      No GPU, no Alpine VM, no dependency on cycle 1 or 3 (design D4)
- [ ] 5.3 Fetch the pinned `.run` (580.196.07, checksum above), `--extract-only`
      outside git, and run `nvidia-musl/tools/elf-audit.py` over it →
      `report/02-elf-audit.md`: the maximum `GLIBC_2.NN` the vendor binaries demand,
      which is the number that picks the jail's rootfs and the one number the whole
      musl plan currently lacks `(build)`
- [ ] 5.4 Only after group 5.2–5.3, start Phase 2 (REA) as `TASK.md` orders it.
      The order is a hard condition of that task, not a preference
- [ ] 5.5 The ladder on `(gpu)`: driver loaded, `nvidia-smi` exiting 0, a CUDA
      program completing, on musl. Two consecutive unresolved ladder steps invoke
      the stop rule and the glibc-jail fallback, as `TASK.md` states it
- [ ] 5.6 Record whichever outcome arrives, with its commands, in the cycle file —
      and then act on it: success mints an `openspec` change to move the base, with
      the measurement as its input; the jail rewrites every "musl system" claim and
      puts the jail's contents in `base.lock`
- [ ] 5.7 Add the cross-product task to `gpu-stack`: what its packages, service
      files and `base.lock` entries become under each outcome (design D5). It sits
      beside the 1.5 already there about 32-bit code

## 6. The invariant that was cycle 5

- [ ] 6.1 State it once, in the cycle file, as `scripts/build/check-amd64-only.py
      --root <image>` plus the two facts already measured: Gentoo's nomultilib
      stage3 has 1 487 ELF files and none is 32-bit; the check agrees with the
      probe's census at 1 487
- [ ] 6.2 Measure, do not assume, that an Alpine x86_64 root passes it — the
      Alpine probe recorded that its 84 ELF files resolve, not that they are all
      `ELFCLASS64`, so the invariant has to be run against that root too `(build)`

## 7. What would make this change worth archiving

- [ ] 7.1 `docs/DEVELOPMENT-CYCLES.md` exists, passes `check-cycle-plan.py`, and
      names the gate's current state with the date and the command that would close it
- [ ] 7.2 The gate has an outcome recorded — musl works, musl falls back to a jail,
      or the operator has decided not to pursue musl — and the base is stated
      consistently in `openspec/project.md`, `README.md` and this file
- [ ] 7.3 No cycle in the file is owned by nothing, and no open change is missing
      from the mapping
