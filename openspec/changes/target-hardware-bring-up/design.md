## Context

Every other change in this portfolio stops at a boundary and points here. The boundary was drawn deliberately — `honest-scope-and-config-generation` D9: no target machine is accessible, and a change whose first task is blocked forever is not a plan.

That decision is only sound if this change exists and is honest about its own condition. Two failure modes are specific to it:

**Paper substitution.** A task here reads "run `nvidia-smi topo -p2p r` and record the result". With no machine, the tempting completion is to write what the output would be. That is the project's own catalogued defect — an artifact that looks like a machine-checked assertion and is inert — with the machine removed.

**Orphaned delegation.** Five changes say a claim "belongs to `target-hardware-bring-up`". Until this change exists, those claims have no owner and read like verified facts.

## Goals / Non-Goals

**Goals:**
- Name the machine classes once, so "needs a host" stops meaning three different things.
- Make every hardware claim carry the machine and date it came from.
- Bring the system from bare metal to: boots, reachable, GPUs visible, topology read, one collective completes.
- Return the outputs other changes are blocked on, by task reference.

**Non-Goals:**
- Making the system fast. No tuning, no benchmark, no throughput claim unless measured here, on this machine, with the command recorded.
- Multi-node, RDMA, passthrough.
- Recovering a machine nobody can reach. If the target bricks, the recovery is the `maintenance-and-updates` procedure, and rehearsing it is a task here — not an assumption.

## Decisions

### D1 — Three host classes, defined once here

| Class | What it is | What it can prove |
|---|---|---|
| **build host** | Any machine with a toolchain and disk. A VM qualifies. | Compilation, phase runs, `ldd` against built libraries, file-list checks, synthetic-fragment proofs |
| **GPU-equipped host** | A machine with at least one NVIDIA GPU that is not the target. May be the same box as the build host. | Driver install and refusal paths, device nodes, `ldconfig` state, health-check thresholds, two-process contention, rollback of a binary artifact |
| **target** | The machine this project is for. | Everything a claim is finally about: topology, P2P, collective, thermals, the real partition layout |

The middle class is what the portfolio was missing. Tasks marked `(build)` that call `nvidia-smi` are not build-host tasks; they are GPU-host tasks wearing a label that makes them look schedulable on a VM.

**Other changes use these labels:** `(build)`, `(gpu)`, `(target)`. A task whose class has not been named in its own group 0 is blocked.

### D2 — Evidence is output plus machine plus date

A recorded `Driver Version: 575.57.08` with no machine and no date is a rumor: two hosts with the same GPU model disagree on driver, firmware, and topology. Every recorded hardware fact carries all three, and the recording format is fixed here so the other changes can consume it.

### D3 — The target fragment is authored from enumeration, not from the example

`honest-scope-and-config-generation` ships `target-nic.fragment.example` explicitly marked as one generation rejects, and a detector that prints PCI class `02` devices with vendor:device IDs and exits non-zero while TODOs remain. This change runs that detector on the real machine and resolves the TODOs by hand. Copying the example is the failure the example's warning exists to prevent.

Storage controller first, NIC second — a kernel that boots and cannot be reached is recoverable over a console; a kernel that cannot mount root is not.

### D4 — The GPU stack is verified by device state, not package presence

Files in `/usr/lib` and a satisfied dependency chain prove an install ran. They do not prove a module loaded, a device node exists, or a process can open one. The verification ladder is: module present in `lsmod` → device nodes exist → `nvidia-smi` reports count, names, driver and firmware versions → a process opens a device and gets a result. Each rung failing names the rung, not "GPU stack broken".

### D5 — Topology availability is not bandwidth

`nvidia-smi topo -p2p r` answers `OK`/`ERR`/`UNS` — can these two devices talk directly. `nvidia-smi topo -m` prints link types (`NV#`, `PIX`, `PHB`, `SYS`). Neither measures bandwidth. `honest-scope-and-config-generation` 8.8/8.9 fixed the documentation of this; this change produces the observation, and the interpretation table there is the one used. An `ERR`/`UNS` on an NVLink pair with IOMMU active is the canonical failure and a legitimate recorded result, not something to make disappear by setting `iommu=pt` before looking.

### D6 — Outputs return to their consumers by task reference

Four changes are blocked on outputs produced here. A result that is recorded but not delivered leaves them blocked anyway, so each has a named return task. The architecture list is the highest-value one: `userspace-and-ml-stack` cannot build a framework from source without it, and cannot honestly claim it will not build one either.

### D7 — Nothing destructive before the recovery path is proven

Rebooting an unreachable machine, writing GRUB, and swapping a kernel module are all ways to end this change with an unusable target. The rollback procedure from `maintenance-and-updates` is rehearsed on a GPU-equipped host first, and the console/out-of-band access path is named before the first destructive step. "SSH broke and there is no console" is the outcome this ordering exists to avoid.

### D8 — Blocked is a state, not a failure

If 0.1 records that no target exists, the change does not pretend to progress. Tasks stay open and labelled, the blocked reason is recorded once, and the change says plainly that it has produced nothing. That is a better outcome than a filled-in task list, and it is the outcome the other five changes need to see when they report their own status.

## Risks / Trade-offs

- **The change may stay blocked indefinitely.** Accepted. The alternative — closing tasks by describing their expected output — is the defect this whole portfolio was created to remove.
- **A GPU-equipped host that is not the target may be unavailable too.** Then `(gpu)` tasks are blocked as well, and the tasks in other changes that assumed `(build)` covered them are wrong in a visible way. Correct and blocked beats convenient and false.
- **The first boot of a hand-authored fragment usually fails.** Expected; the boot-critical check from `build-orchestration` runs against the real fragment before GRUB is trusted, and the failure mode it prevents is exactly this one.
- **Observations age.** A recorded topology is true of a machine on a date. The date in D2 is what makes staleness detectable.

## Open Questions

- **OQ1:** Is there any machine at all — target, or GPU-equipped, or neither? Task 0.1. Everything else in this change is downstream of the answer, and the honest current answer, from every other change's own precondition notes, is "none named".
- **OQ2:** Is there out-of-band access (console, IPMI, serial) to the target? Task 0.2. Without it, D7 forbids the destructive steps entirely rather than ordering them.
- **OQ3:** Is the GPU-equipped host the same machine as the build host? Determines whether `(gpu)` tasks are schedulable at all in the near term.
