# Project: BareCortex

Bare-metal AI compute host built from source: LFS-derived base, OpenRC, single node, multiple GPUs. The repository is a **blueprint plus specifications**, not a working build — 25 tracked files, 476 lines of shell, and a build that stops at LFS chapter 2.

## Non-negotiables

- **No unsourced numbers.** No performance figure, latency, throughput, or percentage appears in any artifact unless it was measured on this machine or cited to a named upstream page. This is the project's founding correction and it outranks convenience.
- **An assertion with no command behind it is not a requirement.** Anything that reads like a machine-checked claim must name the command that fails when it is false. A check that cannot fail is a defect, not a pass.
- **Hardware-dependent work is separate.** Claims that need the target host live in `target-hardware-bring-up`, never inside a change that can be finished on paper.
- **Language policy is a selection order:** maintained port → upstream as-is → do not adopt. Rewrites require usage evidence, parity evidence, and an executed rollback.
- **Delegating a claim to another change is not closing it.** The receiving change must exist and must carry a task for it. `target-hardware-bring-up` is the receiver for hardware claims; a reference to a change that has not been minted is a claim with no owner.

## Pinned inputs

| Input | Value | Note |
|---|---|---|
| Base book | LFS **13.0-openrc**, `linuxfromscratch.org/~zeckma/openrc/lfs/stable/` | Published outside the main view index; no release cadence, so it is pinned by revision |
| Userspace book | BLFS **(OpenRC Edition) r13.0-194**, dated 2026-03-23, `linuxfromscratch.org/~zeckma/openrc/blfs/svn/` | SVN rendering, **targets the LFS development book**, not the 13.0 stable base pinned above — the mismatch is a `userspace-and-ml-stack` input, not a silent assumption |
| Kernel from the base book | Linux **6.18.10** | The book's pin |
| Kernel config audit pin | `v6.12` | What `honest-scope-and-config-generation` verified its symbols against; a known gap, not an error |
| Toolchain from the base book | glibc 2.43, GCC 15.2.0, binutils 2.46.0 | |
| Init | OpenRC **0.63**, PID 1 is `openrc-init` | No sysvinit package, no `/etc/inittab`, **no PAM** |
| udev | built from `systemd-259.1` with `logind`/`vconsole` off | So "no systemd" means no systemd as init or service manager — never "no systemd source compiled" |
| Package truth | `base.lock` at the repository root, **derived** from the pinned book by `build-orchestration` | One file. Other changes add entries to it; nobody maintains a second package list |

## Conventions

- **Change ids** are kebab-case noun phrases naming the subject (`gpu-stack`, `build-orchestration`), not the verb. This is a deliberate deviation from the `add-*/update-*` convention used by external plans; renaming the in-flight changes buys nothing.
- **Archiving:** `openspec archive` writes a delta into `openspec/specs/` as current truth. While a change's tasks are unexecuted, archive with `--skip-specs`. `openspec/specs/` is empty until something is actually built.
- **Specs carry the delta, never a copy of the book.** Duplicating LFS drifts on the day LFS updates and contains no decision of ours.
- **Destructive steps are named as such.** Anything that partitions, formats, or writes a bootloader declares its preconditions and refuses unsafe input.
- **Three host classes, not two.** A task that needs a machine carries the marker; a task whose class has not been named in its change's group 0 is blocked, not schedulable. Definitions live in `target-hardware-bring-up` design D1.

  | Marker | Machine | Proves |
  |---|---|---|
  | `(build)` | Any machine with a toolchain and disk; a VM qualifies | Compilation, phase runs, `ldd` against built libraries, file and synthetic-fragment checks |
  | `(gpu)` | A machine with an NVIDIA GPU that is not the target | Driver install and refusal paths, device nodes, `ldconfig` state, health-check thresholds, binary-artifact rollback, process contention |
  | `(target)` | The machine this project is for | Topology, P2P, collectives, thermals, the real partition layout — everything a claim is finally about |

  Marking a `nvidia-smi` call `(build)` is a defect: it reads as schedulable on a VM. The build runs on a disposable host; the target appears only in `target-hardware-bring-up`. A sudoers entry granting passwordless root to a build user is acceptable on a disposable host and is recorded as scoped to one.
- **One name for the package list: `base.lock`.** "Manifest" survives in older prose as a synonym for it. Nothing else is a manifest, and no second package list is started by any name.
