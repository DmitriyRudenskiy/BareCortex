# Development cycles — the order of the work

Cycle 1 continues on its current path: chapters 8–10 of the pinned LFS
13.0-openrc edition are being run in a separate session, with receipts in
`build-evidence/` and the running account in `docs/BUILD-LOG.md`. This file
changes what comes after it, not what is running.

Four cycles and one invariant. Each cycle carries what it produces, the
command that fails when it is not finished, the host class that runs it, the
change that owns it, and what it may not decide. Host classes are the three in
`openspec/project.md`: `(build)` any machine with a toolchain and disk,
`(gpu)` an NVIDIA machine that is not the target, `(target)` the machine the
project is for. A cycle whose machine is unnamed is blocked, not schedulable.

The two operator answers the gate depends on: **(a)** answered 2026-11-14 —
driver branch **580**, so userspace and modules come from
`NVIDIA-Linux-x86_64-580.196.07.run` (x86_64 sha256
`0aab0518db53cf73c412dc7f32bbc850ca95e839d8923d0de4bff42c1c9169ce`), closed
`kernel/`, recorded as a ruling in `nvidia-musl/progress.md`; the GPU model
was measured 2026-11-14 over ssh on the host `gpu-stack` 0.2 names — **5×
GeForce RTX 4060 Ti (Ada), driver 580.159.03 loaded** — and Ada is Turing+
and up, so the GSP-firmware step `nvidia-musl/TASK.md` needs applies to it.
**(b)** still open —
whether to stand up an Alpine VM on `(build)` for the static work. Neither
blocks cycle 1, the cycle-2 hand-off, or cycle 4's `(build)` half, which is
done.

## Cycle 1 — LFS to a bootable base

Produces: a bootable OpenRC base image, built page by page from the pinned
edition with per-page receipts, and the whole-build procedure documented in
`docs/`.

Exit: `sh scripts/build/build-rootfs.sh status` reporting `first-boot` passed —
whose exit criteria are the ones `phases.toml` writes for that phase: the disk
boots to a login prompt with the host's disk detached, `uname -r` answers the
pinned kernel, and PID 1 is the init the OpenRC edition installs.

Host: (build)

Owns: `base-system-and-init` — the in-flight chapters 8–10 run belongs to that
session; this plan adds no task to it.

May not decide: which base wins. Cycle 3 measures a candidate; cycle 4's
result is what promotes it.

## Cycle 2 — the build driven from the build root

Produces: a build that runs from a clean build root holding the sources and
this repository — one command per phase, the same evidence, no host-side
keychain, no absolute path outside the root.

Exit: from a fresh root with no `$HOME` state, `sh scripts/build/build-rootfs.sh <phase>` resumes the phase and writes the same `record.json` shape (the command lands in `build-orchestration` with this check named; a requirement with no such command is a sentence).

Host: (build)

Owns: `build-orchestration`

May not decide: the phase table or the book — `phases.toml` and the pinned
edition stay the source of every command.

Why it is a cycle: the pipeline is driven over
`ssh -J user@192.168.0.86 -p 2222 bct@127.0.0.1`, and neither
`~/.ssh/barecortex_vm_ed25519` nor `~/.config/barecortex/vm.env` exists on
the laptop — the build cannot be resumed from here. Corrected 2026-11-14 in
the other direction too: the host answers ssh, the guest VM runs with
`lfs.qcow2` attached, and from the host the guest's sshd is reached at
`127.0.0.1:2222` — the connection dies on a **changed guest host key**, not
on a dead guest. The requirement stands as written — no host-side keychain,
drive from the build root — and a live guest is exactly what makes that
usable once the drive does not depend on one laptop's keychain.

## Cycle 3 — busybox and musl as a measured candidate

Produces: a measured candidate root — Alpine minirootfs plus OpenRC — with its
pins in `base.lock` (no `apk.lock`, no second list), booted under QEMU on
`(build)` with OpenRC as PID 1, and every invariant run against it with a
non-zero ELF count, so a scan of nothing cannot be read as a pass.

Exit: `sh scripts/build/tests/test-arch.sh --root <candidate root>` reports a
non-zero 64-bit-only ELF census for a root that boots to OpenRC, and the
candidate's pins are entries of `base.lock`.

Host: (build)

Owns: `alpine-as-base` plus the gate — the change that already holds the
measurement and the "record, do not adopt" decision the gate can overturn;
no second change for the same decision.

May not decide: promotion to base. Until cycle 4's result, the candidate is a
measured candidate, not the base: 8 431 459 B unpacked, OpenRC's closure 7
packages and 171 paths (9 859 629 B after), 84 ELF objects all resolving
through the musl loader, 41 `/etc/init.d` entries (`docs/ALPINE-AS-BASE.md`).
It is also cycle 2's proof: a second root is the cheapest way to show the
pipeline is not LFS-shaped.

## Cycle 4 — the GPU stack on musl, the gate itself

Produces: a verdict — musl carries the vendor stack, or it does not. Both
outcomes are written down before either happens: success mints an `openspec`
change to move the base, with the measurement as its input; the jail fallback
rewrites every "musl system" claim as a musl base with a glibc rootfs for the
vendor blobs, and the jail's contents go into `base.lock`.

Exit: the ladder on the `(gpu)` machine — the driver loads, `nvidia-smi`
exits 0, and a CUDA program completes, on musl. Two consecutive unresolved
ladder steps invoke the stop rule and the glibc-jail fallback, as
`nvidia-musl/TASK.md` states it.

Host: (build) for the audit half — done, reports 02–05 under
`nvidia-musl/report/`, 580.196.07 pinned; (gpu) for the ladder — the machine
is named: `user@192.168.0.86`, 5× RTX 4060 Ti, driver 580.159.03 loaded,
confirmed reachable 2026-11-14 (`gpu-stack` 0.2 named it). The gate is no
longer a missing machine; it is the ladder's outcome, and the ladder runs on
that host.

Owns: `nvidia-musl`

May not decide: the base alone, and the GPU stack's own shape. The stack's
packages, service files and `base.lock` entries under each outcome are a task
in `gpu-stack` (beside its 1.5 on 32-bit code); the rewrite lands there in
either outcome.

## Invariant — the userspace is 64-bit only

Not a cycle: `amd64-only-userspace` landed the rule, the command, the test and
the prose, and what remains — running it against the assembled image — is a
task inside cycle 1's exit, not a queue position for a finished thing. Every
cycle's output passes it, next to "no systemd as init":
`sh scripts/build/tests/test-arch.sh --root <image root>` fails on any
non-x86-64 ELF, and `elf=0` is a failed invocation, not a pass.

Measured, not assumed: Gentoo's nomultilib stage3 is 1 487 ELF files and none
is 32-bit, and the check agrees with the probe's census at 1 487
(`docs/GENTOO-AS-BASE.md`). The Alpine candidate root passes it too, measured
2026-11-14: `sh scripts/build/tests/test-arch.sh --root <candidate root>`
reports `elf=84 findings=0` for a root rebuilt from the pinned minirootfs
(`alpine-minirootfs-3.24.4-x86_64.tar.gz`, sha256
`cdaa38cb8de3537967ee5b6c9b6cd00cc962da2b018e6eea48696bb274923012`, published
and computed equal) plus the 7-package OpenRC closure — all 84 ELF files are
`ELFCLASS64`/`EM_X86_64`, the census agreeing with the probe's 84
(`artifacts/handoff-vm/alpine-probe/evidence/ldd.json`). The rebuild ran on
the laptop because `(build)` is unreachable (cycle 2); the scan reads twenty
bytes per file and carries no host dependency. Its byte totals sit 59 B under
the `docs/ALPINE-AS-BASE.md` figures (8 431 400 against 8 431 459 before the
closure, 9 859 570 against 9 859 629 after) while the path count matches
exactly (515 + 171 = 686). The delta is not drift: the tarball carries the
pinned sha256 byte for byte, and the delta is the same 59 B before and after
the closure — one measurement-method difference between the probe's GNU
`du -sb` on the VM and an apparent-size sum on the laptop, not a changed
input. The `ALPINE-AS-BASE.md` figures stand, and the invariant does not rest
on byte totals.

A zero census is a failed invocation, not a pass: `elf=0` from
`test-arch.sh` and `binaries=0` from `test-all.sh no-systemd` both exit 0
today — neither script guards against scanning nothing — so a recorded result
carries the command and its non-zero count, and a zero count means the scan
proved nothing. The Alpine record above is the shape of a valid entry:
`elf=84`, not `rc=0` alone.

## The mapping — every open change to a cycle

Derived from `openspec list --json` on 2026-07-21 — twelve changes — plus the
`nvidia-musl/` workstream, so that no claim is handed to a cycle that is not
an owner:

| change | cycle |
|---|---|
| `base-system-and-init` | 1 |
| `image-minimisation` | 1 |
| `honest-scope-and-config-generation` | 1 |
| `userspace-and-ml-stack` | 1 |
| `maintenance-and-updates` | 1 |
| `build-orchestration` | 2 |
| `align-build-with-build-rootfs` | 2 |
| `amd64-only-userspace` | invariant (its 2.4 closes inside cycle 1's exit) |
| `alpine-as-base` | 3 |
| `nvidia-musl/` | 4 |
| `gpu-stack` | 4 (the cross-product task) |
| `target-hardware-bring-up` | none — `(target)` appears in no cycle here |
| `development-cycle-plan` | this file |
