## Scope and markers

Every task here runs on a machine. `(build)`, `(gpu)`, `(target)` mean the classes defined in design D1. **No task in this file can be closed by describing what running it would show** — that is the defect this change exists to prevent, and it is the reason the file stays open if group 0 fails.

Tasks inherited from other changes are marked with their origin, so the delegation can be checked in both directions.

## 0. Preconditions — the gate for the whole change

- [ ] 0.1 Name the target machine, or record that none exists: vendor, model, CPU count, GPU models and count, storage controllers, network controllers, disk layout, and who physically has access. **(target)** — if this records "none", every task below is labelled blocked and this change produces nothing
- [ ] 0.2 Name the console or out-of-band access path (serial, IPMI, iDRAC/iLO, physical console), or record that there is none — design D7 forbids the destructive groups without it **(target)**
- [ ] 0.3 Name the GPU-equipped host that is not the target, or record that none exists, and record whether it is the build host — this answers OQ3 and re-labels tasks in `gpu-stack`, `userspace-and-ml-stack`, and `maintenance-and-updates` that assumed `(build)` was enough **(gpu)**
- [ ] 0.4 Record the three class definitions in `openspec/project.md` and re-label the tasks in the other changes that used `(build)` for GPU-host work, listing each corrected task id
- [ ] 0.5 Confirm no task in this file is closed, and that the change reports itself as blocked when 0.1 says no machine exists

## 1. Enumerate the machine

- [ ] 1.1 Run `lspci -nn` on the target and record the verbatim output with machine identity and date **(target)**
- [ ] 1.2 Run `honest-scope-and-config-generation 4.5`'s detector and record the PCI class `02` devices with vendor:device IDs **(target)**
- [ ] 1.3 Enumerate storage controllers the same way — class `01` — since a missing storage driver is unrecoverable without physical access **(target)**
- [ ] 1.4 Record the memory controller and whether an EDAC controller driver exists for it, closing `honest-scope-and-config-generation 2.6`'s deferral **(target)**
- [ ] 1.5 Record CPU count against the base kernel's `NR_CPUS`, closing `honest-scope-and-config-generation 11.3`'s deferral **(target)**
- [ ] 1.6 Record the firmware backend available for pstore, or record that there is none, closing the deferral from `honest-scope-and-config-generation 2.13` **(target)**
- [ ] 1.7 Record the real partition layout and free space in `/boot`, and hand it to `maintenance-and-updates 4.6` **(target)**

## 2. Author the target fragment

- [ ] 2.1 Write the target fragment from 1.2 and 1.3 by resolving the detector's unresolved markers by hand; confirm it is not the shipped example **(target)**
- [ ] 2.2 Add the EDAC core and controller driver named by 1.4, or record the decision not to and what that means for error reporting **(target)**
- [ ] 2.3 Confirm every driver needed to mount root is built in, not modular — there is no initrd **(build)**
- [ ] 2.4 Run the boot-critical check from `build-orchestration 6.1` against the real fragment rather than the synthetic one, and record the difference **(build)**
- [ ] 2.5 Confirm generation still refuses a fragment with unresolved markers, by leaving one in place once **(build)**

## 3. Build and boot

- [ ] 3.1 Build the kernel for the target fragment **(build)**
- [ ] 3.2 Write the boot loader entry, having confirmed the recovery path from 0.2 and the rehearsal from 8.1 **(target)**
- [ ] 3.3 First boot: record whether it reaches a login, verbatim **(target)**
- [ ] 3.4 Run `build-orchestration 7.3`'s boot checklist and record its result, closing that task's deferral **(target)**
- [ ] 3.5 Record `dmesg` from first boot and name every error the boot is not supposed to produce **(target)**
- [ ] 3.6 Confirm SSH reachability and record the interface name the fragment's driver bound to **(target)**

## 4. GPU stack to device state

- [ ] 4.1 Record the module rung: is the driver module present, and with which parameters **(target)**
- [ ] 4.2 Record the device-node rung, naming the flag that controls creation if nodes are absent **(target)**
- [ ] 4.3 Record `nvidia-smi` output: device count, names, driver version, firmware version — the record `userspace-and-ml-stack 8.1` runs against **(target)**
- [ ] 4.4 Record the process rung: a process opens a device and returns a result **(target)**
- [ ] 4.5 Record which rung failed, if any, using the rung names from design D4 rather than "GPU stack broken" **(target)**
- [ ] 4.6 Record the loader state after install: `ldconfig -p` output for the driver libraries, closing `gpu-stack 6.1`'s deferral **(gpu)**
- [ ] 4.7 Run the CUDA-runtime owner probe from `gpu-stack 1.3` and `userspace-and-ml-stack 1.1` on the real environment, and record which change's record the answer lives in **(gpu)**

## 5. Topology and P2P

- [ ] 5.1 Record `nvidia-smi topo -m` verbatim with machine identity and date **(target)**
- [ ] 5.2 Record `nvidia-smi topo -p2p r` for every pair, stating availability and the IOMMU state at the time — not bandwidth **(target)**
- [ ] 5.3 Interpret 5.2 with the exhaustive table from `honest-scope-and-config-generation 8.9`, and record an `ERR`/`UNS` on an NVLink pair as a result rather than removing it by setting `iommu=pt` first **(target)**
- [ ] 5.4 Record the IOMMU state observed without rebooting, using the method documented in `honest-scope-and-config-generation 8.3` **(target)**
- [ ] 5.5 Only if 5.3 produced a failure: set `iommu=pt`, re-run 5.2, and record both results as the before/after that justifies the troubleshooting entry **(target)**

## 6. Collective validation

- [ ] 6.1 Record the GPU architecture list and hand it to `userspace-and-ml-stack 0.3` by reference, not by copying the value **(target)**
- [ ] 6.2 Run the device-visibility check written by `userspace-and-ml-stack 8.1` and record its output **(target)**
- [ ] 6.3 Run the collective smoke check from `userspace-and-ml-stack 8.2` on the declared device set and record the device set it actually ran on **(target)**
- [ ] 6.4 Run the mixed-precision step check from `userspace-and-ml-stack 8.3` and record it as a correctness result **(target)**
- [ ] 6.5 Run the loaded-library check from `userspace-and-ml-stack 8.4` against a running process and record which libraries it actually loaded **(target)**
- [ ] 6.6 Record any throughput or speed number only with its command, date, and machine — and confirm `README.md:30`'s "maximum speed" claim is either backed by such a record or removed **(target)**

## 7. Runtime state other changes asked about

- [ ] 7.1 Record the observed governor after `honest-scope-and-config-generation 7.1`'s service runs, counting only CPUs whose write succeeded, and hand it to that change's task 7.4 **(target)**
- [ ] 7.2 Record the temperature readings the health check queries, and which `nvidia-smi` fields the installed driver actually provides, closing `maintenance-and-updates 6.3` **(gpu)**
- [ ] 7.3 Record the clock behaviour the `chrony` task in `userspace-and-ml-stack 3.3` assumed, or leave that assumption standing **(target)**

## 8. Maintenance steps delegated here

- [ ] 8.1 Rehearse the rollback procedure from `maintenance-and-updates` group 4 on a GPU-equipped host before any destructive step in group 3 **(gpu)**
- [ ] 8.2 Repeat the rollback rehearsal against the real layout from 1.7 and record the differences **(target)**
- [ ] 8.3 Perform one kernel and driver bump together, with the GPU stack started, and record what the mismatch guard actually did **(target)**
- [ ] 8.4 Run two users' processes on one device and record what the device-selection rule from `userspace-and-ml-stack` group 7 does **(target)**

## 9. Hand-off and honesty

- [ ] 9.1 Confirm every closed task here has verbatim output, a machine identity, and a date, and reopen any that has prose instead
- [ ] 9.2 Confirm each of the four consuming changes cites this change's record rather than restating a value, and list the task ids checked
- [ ] 9.3 Confirm every hardware claim remaining in the other five changes is either backed by a record here or explicitly labelled unverified
- [ ] 9.4 Confirm no task in this file was closed while its machine class was unnamed
- [ ] 9.5 Record what this change did not establish, so the next reader does not infer it from silence
- [ ] 9.6 `openspec validate target-hardware-bring-up --strict` passes
