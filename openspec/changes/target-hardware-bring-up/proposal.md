## Why

Five changes close by saying a claim "belongs to `target-hardware-bring-up`", and that change does not exist. A delegation to nothing is not a deferral — it is a claim with no owner, and the reader cannot tell it from a claim that was verified.

`honest-scope-and-config-generation` design D9 made the split and named the destination: "A change whose first task is blocked indefinitely is not a plan; it is how change tracking dies." This is that change. It owns every statement about this machine that can only be made by standing at it.

It is also where the portfolio's missing third host class gets defined. Tasks across four changes need a machine with a GPU that is not the target — to run `ldd` against a built `libtorch_cuda.so`, to watch a driver refuse to start, to rehearse a rollback. Those tasks are currently marked `(build)` or unmarked, which reads as "can be done on the build host" and is false.

## What Changes

- **The three host classes are defined once, here**, and the other changes reference them: build host, GPU-equipped host, target. A task needing a class that has not been named is blocked, not deferred.
- **Hardware facts become recorded output with a machine identity.** A number or a device name without the machine it came from and the date is not evidence, because two hosts with the same GPU model disagree.
- **The target kernel fragment is authored from enumeration**, never by copying the example file.
- **The GPU stack is verified by device state**, not by the presence of packages: module loaded, device nodes present, driver version reported, topology read.
- **Topology claims distinguish availability from bandwidth.** `nvidia-smi topo -p2p r` answers "can it", not "how fast".
- **Outputs flow back to their consumers by task reference**: the architecture list to `userspace-and-ml-stack 0.3`, the observed governor to `honest-scope-and-config-generation 7.4`, the real partition layout to `maintenance-and-updates 4.6`, the boot checklist result to `build-orchestration 7.3`.
- **Nothing destructive runs on the target before the recovery path is proven.**
- **A blocked task stays visibly blocked.** No task in this change closes on a description of what running it would show.

## Capabilities

### New Capabilities
- `target-hardware-bring-up`: Enumerating the machine, building and booting a kernel for it, bringing up the GPU stack to observed device state, validating topology and collective communication, and handing the results back to the changes that asked for them.

### Modified Capabilities
<!-- None. `openspec/specs/` is empty. -->

## Impact

**Depends on:**
- `honest-scope-and-config-generation` — task 11.1 created this change and lists what it carries: NIC drivers, EDAC core plus controller driver, `NR_CPUS` if the target exceeds the base default, pstore backend, the observed governor.
- `base-system-and-init` — a base to install.
- `gpu-stack` — the install layout and provenance this change verifies.
- `build-orchestration` — the phases that produce the system, and the boot checklist (task 7.3) this change runs.
- `userspace-and-ml-stack` — the checks it wrote and marked `(run)` for this change to execute.
- `maintenance-and-updates` — the rollback procedure this change must rehearse before it may break anything.

**Blocked on:** a machine. Task 0.1 is the gate for the whole change; if it records "none", every other task is labelled blocked and the change is honest about producing nothing.

**Out of scope:** multi-node, InfiniBand, RDMA, passthrough and virtualisation (the direction the portfolio settled), and any performance number that was not measured here.
