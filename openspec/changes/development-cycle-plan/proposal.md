# Proposal: development-cycle-plan

## Why

The work exists in three shapes that do not know about each other: eleven phases in
`phases.toml` (chapters 8–10 running now), ten open changes with no stated order
between them, and a research workstream in `nvidia-musl/` whose result decides
which base the other two are building. The operator's re-plan names five cycles —
LFS, build scripts from the build root, busybox+musl from Alpine, NVIDIA on musl,
nomultilib from Gentoo — and putting them in that order is what exposes that they
are not five cycles: one is already finished, one is a property every cycle's
output has to pass rather than a cycle, and one decides whether another is worth
finishing. Saying that before work starts is cheap. Discovering it with a musl
base half-built and the GPU stack needing a glibc jail is not.

There is also a live reason to re-plan now rather than at the next milestone: the
build cannot currently be resumed at all. `scripts/build/build.py` drives the guest
over `ssh -J user@192.168.0.86 -p 2222 bct@127.0.0.1`, the guest's sshd closes the
connection, and neither the key nor `~/.config/barecortex/vm.env` exists on the
laptop any more. A build whose continuation depends on one laptop's keychain is
not a build pipeline, and that is cycle 2's argument, stated as an observation and
not as a hypothesis.

## What Changes

- One file, `docs/DEVELOPMENT-CYCLES.md`, becomes the order of work: for each
  cycle, what it produces, the command that says it is finished, its host class
  from `openspec/project.md`, what it is not allowed to decide, and which existing
  change owns the work. Nothing about a cycle lives only in conversation.
- **Cycle 5 is not a cycle.** `amd64-only-userspace` landed the rule and
  `scripts/build/check-amd64-only.py`; nomultilib becomes an invariant every
  cycle's output passes, with one open task (run it against the assembled image).
- **Cycle 4 gates cycle 3.** A musl base is not a candidate until the NVIDIA
  ladder reaches `nvidia-smi` and CUDA on musl, or the glibc-jail fallback is
  chosen deliberately. Until then cycle 3 builds a second root to measure, not a
  base to ship, and the base stays LFS 13.0-openrc with glibc.
- **Cycle 4's paper work is unscheduled, and it needs no GPU.** Phases 1.2–1.4 of
  `nvidia-musl/TASK.md` and the `elf-audit` of the `.run` run on `(build)`; they
  are the cheapest thing available that de-risks the whole musl track, and they
  are currently blocked on an operator answer (which GPU, therefore 580 or 620)
  rather than on hardware.
- **Cycle 2 gets a requirement: the build is driven from the build root, not from
  a laptop.** Handed to `build-orchestration`, which owns the build root and
  already requires resumable state; this change carries the task, not a duplicate
  requirement.
- The ten open changes are mapped onto the cycles, so that a claim handed to a
  cycle lands on a change that exists and carries a task for it.
- No base change, no `base.lock` change, no code change. This change produces a
  plan with commands attached and the two documents that hold it.

## Capabilities

### New Capabilities
- `development-cycles`: what counts as a cycle in this project, what a cycle's
  exit criterion has to be (a command, on a named host class), which cycles may
  change the base and on what evidence, and what may not be started while its
  gate is closed.

### Modified Capabilities
<!-- None. `build-orchestration` is where cycle 2's requirement will be written,
     and it is still an open change, not an archived spec; `base-image` is not
     touched — the base stays LFS 13.0-openrc. -->

## Impact

- New: `docs/DEVELOPMENT-CYCLES.md`. Edited: `README.md` (Documentation list) and
  `docs/PHILOSOPHY.md` only if the cycle list needs to be findable from the
  philosophy table, which it does not yet.
- Tasks added to owning changes rather than new ones: `build-orchestration`
  (build-root-driven build), `gpu-stack` (the musl line's effect on the GPU stack,
  beside the 1.5 already there), `alpine-as-base` (its measurement becomes
  cycle 3's input, and its "record, do not adopt" stance is what cycle 4 can
  overturn).
- `nvidia-musl/` keeps its own ledger and stop rule; this change does not
  re-sequence inside it, it only states what its two outcomes mean for the rest.
- Nothing here invalidates the chapters 8–10 build in flight. Cycle 1 finishes on
  its current path; the plan changes what happens after it, not what is running.
