## Context

Three things are moving and none of them knows the others' order.

`phases.toml` holds eleven phases and is mid-flight: chapters 8–10 of the pinned
LFS 13.0-openrc book are being run now by a separate session, with receipts in
`build-evidence/` and prose in `docs/BUILD-LOG.md`. Ten changes are open
(`openspec/changes/`), each self-contained, none stating whether it comes before
or after another. `nvidia-musl/` is a research workstream with its own task
(`TASK.md` v2, approved 2026-11-12), its own ledger (`progress.md`), its own stop
rule, and one closed phase of five — and its subject is whether the GPU stack can
run on the libc that cycles 3 and 4 would choose.

The operator's re-plan names five cycles: LFS, build scripts from the build root,
busybox+musl from Alpine, NVIDIA on musl, nomultilib from Gentoo. Ordered, they
stop being five of the same thing. The measurements already in the tree say why:

- Alpine as a base is cheap and it works as an OpenRC system: 8 431 459 B and 16
  packages unpacked, OpenRC's closure 7 packages and 9 859 629 B more, 84 ELF
  files all resolving through the musl loader, 41 `init.d` entries
  (`docs/ALPINE-AS-BASE.md`).
- The 64-bit-only rule is free and already enforced: 1 487 ELF files in Gentoo's
  nomultilib stage3, none 32-bit, and `scripts/build/check-amd64-only.py` now
  fails an image that adds any (`docs/GENTOO-AS-BASE.md`).
- NVIDIA on musl is unproven and is the only one of the three whose answer is not
  in hand: it needs a patched musl ldso (ifunc/IRELATIVE, static-TLS surplus),
  gcompat, a `libnvcompat` shim and a patchelf pass over the vendor binaries, and
  its published status is a Draft PR (`nvidia-musl/report/01-study-chimera.md`).

And one fact about the present, observed rather than reasoned: the build cannot be
resumed from the laptop today. `scripts/build/build.py` reaches the guest through
`ssh -J user@192.168.0.86 -p 2222 bct@127.0.0.1`; the guest closes the connection,
and `~/.ssh/barecortex_vm_ed25519` and `~/.config/barecortex/vm.env` no longer
exist on this machine. That is cycle 2's argument, and it is not hypothetical.

## Goals / Non-Goals

**Goals:**

- One file stating the order of work, with a command and a host class per cycle,
  so the plan is reviewable and a claim of "done" is checkable.
- The dependency between the musl base and the GPU result stated as a gate, with
  both outcomes written down before either happens.
- The work that is runnable today named explicitly, so a gate on hardware does not
  idle paper work that needs neither GPU nor target.
- Every open change mapped to a cycle, so no claim is handed to a cycle, which is
  not an owner.

**Non-Goals:**

- Changing the base. LFS 13.0-openrc stays pinned until the gate opens.
- Re-sequencing inside `nvidia-musl/TASK.md`. Its phases, ladder and stop rule are
  its own; this plan only states what its two outcomes mean downstream.
- New tooling, new phases, or any edit to the build in flight.
- Stopping the `image-minimisation` work being written in this worktree by another
  session. It maps to cycle 1 and is left alone.

## Decisions

### D1 — Four cycles and one invariant, not five cycles

Nomultilib is not a cycle. `amd64-only-userspace` landed the rule, the command,
the test and the prose; what remains is running `check-amd64-only.py` against the
assembled image, which is a task inside cycle 1's exit, not a cycle after cycle 4.
Keeping it as the fifth item would give a finished thing a queue position, and
would let a future cycle treat "64-bit only" as somebody else's phase. It becomes
an invariant every cycle's output passes, next to "no systemd as init".

### D2 — Cycle 4 gates cycle 3, in that order

The order the cycles were listed in implies: build the musl base, then find out
about the GPU. That spends the expensive work before the disproof. The base is the
cheap part and it is already measured; the GPU stack is the unknown, and its
worst case is not "it fails" but "it works only inside a glibc jail", which
changes what a musl base *is* — a musl system with a glibc rootfs bolted on for
the vendor blobs.

So: cycle 3 produces a measured candidate root, and only a `(gpu)` result
promotes it to base. What cycle 3 is worth even if the gate never opens is stated
in D3 — it is cycle 2's proof, because a second root is the cheapest way to show
the pipeline is not LFS-shaped.

### D3 — Cycle 2 means "the build is driven from the build root"

Read literally, "scripts from the build root" is the pipeline running *inside* the
tree it builds, with the host offering a disk and a console. The alternative
reading — scripts that *create* the build root — is already covered by
`build-orchestration` and `phases.toml`'s `chroot-layout`, so it would be a cycle
in name only; the interpretation used here is the one that names a real defect.
If the operator meant something else, this decision is the one line to rewrite.

The requirement handed to `build-orchestration`: from a clean build root holding
the sources and this repository, one command runs a phase to the same evidence,
with no host-side keychain, no `ProxyJump` to a laptop, and no absolute path
outside the root. The current failure mode is the evidence, and the check is that
a fresh root with no `$HOME` state resumes a phase.

### D4 — Cycle 4's unscheduled half runs now, on `(build)`

Phases 1.2–1.4 of `nvidia-musl/TASK.md` (the git-bruh recipe, the Void jail
notes, MuslMan) and the ELF audit of the vendor `.run` need a toolchain and a
download, not a GPU. They are gated on one operator answer — which GPU, therefore
580 or 620 — and on nothing else. This is the cheapest de-risking available to the
whole plan and it does not touch the host cycle 1 is building on.

### D5 — Two GPU lines, one owner for the cross-product

`gpu-stack` describes the GPU stack for the glibc base. `nvidia-musl/` describes
the musl attempt. Nobody owns the sentence "if musl wins, the GPU stack's packages,
service files and `base.lock` entries change like this", so that sentence is a
task in `gpu-stack`, beside the 1.5 already added there, rather than a new change.
If the musl line wins, `gpu-stack` is where the rewrite lands; if it falls back to
a jail, `gpu-stack` is where the jail becomes a documented component.

### D6 — The mapping is the artifact; no new change is minted for a gated cycle

`alpine-as-base` exists, holds the measurement, and decided "record, do not
adopt". That decision is exactly what the gate can overturn, so cycle 3 is
`alpine-as-base` plus a gate — not a new change whose first task is "wait".
Minting `musl-base` now would create a second place the same decision lives.

## Risks / Trade-offs

**The plan goes stale within weeks.** Two sessions are editing this tree and ten
changes are open. Mitigation: the cycle file names owning changes rather than
dates, and one task re-derives the mapping from `openspec list` instead of keeping
it by hand.

**A second base rots into a second package list.** The moment someone writes
`apk.lock`, `base.lock` becomes a lie. Mitigated by a requirement, not by
discipline: candidate-base pins go into `base.lock`, and the check is that no
second list exists.

**Cycle 1's in-flight work reads as deprecated.** The cycle file says in its first
lines that cycle 1 continues on its current path and that the plan changes what
comes after it.

**The gate stays shut because a question went unanswered.** Two operator answers
are named in the cycle file (which GPU and its access; whether to stand up an
Alpine VM for the static work). The file also names what stays decidable without
them, so a shut gate is visibly about those two answers and not about the cycle.

**Planning documents attract prose.** Every line in the cycle file that reads like
a machine-checked claim has to name its command, which is the same rule
`openspec/project.md` already applies to specs, applied to the roadmap.
