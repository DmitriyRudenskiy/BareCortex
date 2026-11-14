# Tasks: honest-scope-and-config-generation

Scope note: every task below is completable without the target machine. Hardware-dependent work lives in the follow-up change named in design.md D9. Tasks blocked by an open question are marked and must not be started before the question is resolved.

## 1. Resolve what blocks the rest

- [x] 1.1 Resolve OQ1 — resolved: passthrough out of scope (design D7); `IRQ_REMAP` stays, for x2APIC
- [x] 1.2 Resolve OQ3 — resolved: v6.12 pinned, `PATCHLEVEL`-gated (design D10)
- [x] 1.3 Record the pinned kernel version in the fragment header — the header records v6.12 (6.12.116, 2026-11-08) as the audited pin, with the `--allow-unaudited-kernel` path named for any other tree
- [ ] 1.4 Re-audit the fragment against the kernel the base book actually ships — LFS 13.1 pins Linux 7.1.8, verified from the book's package list, while this change audited v6.12 (still maintained: 6.12.116 as of 2026-11-08). Until this runs, v6.12 stays the audited pin and every build on a 7.x tree goes through the `--allow-unaudited-kernel` flag, which is the D10 path becoming normal rather than exceptional

## 2. Fragment hygiene

- [x] 2.1 Move `kernel/.config` to `kernel/fragments/ai-workload.fragment`, keeping its content as a starting point — done, and the old path is now the generator's output location (group 3), untracked
- [x] 2.2 Strip every line to `CONFIG_X=value` or `# CONFIG_X is not set` with no trailing text; move all rationale into comments on their own lines
- [x] 2.3 Delete `CONFIG_DRM_NVIDIA` and `CONFIG_DRM_NVIDIA_MODESET`; keep `CONFIG_DRM=m` and `# CONFIG_DRM_NOUVEAU is not set`
- [x] 2.4 Delete `CONFIG_64BIT` and `CONFIG_NR_CPUS` entirely rather than expressing them as commented-out lines (design D2)
- [x] 2.5 Remove `CONFIG_PCI_P2PDMA` and state in a comment that NVIDIA P2P comes from `nvidia_p2p` in `nvidia.ko` — comment present, and no line is written because the symbol is absent from the base, where a `is not set` line would be a no-op
- [x] 2.6 Do not add EDAC to the workload fragment at all — core without a controller driver reports nothing; the whole subsystem belongs in the target fragment (design D5)
- [x] 2.7 Add `CONFIG_MAGIC_SYSRQ=y` and **no** `CONFIG_MAGIC_SYSRQ_DEFAULT_ENABLE` line — `0x1`, the default, already means all functions (`sysrq_on_mask`: "A value of 1 means 'all'"), so any explicit value only narrows it (design D6)
- [x] 2.8 Leave `CONFIG_CPU_FREQ` at its base value; do not disable it (design D4) — the fragment disables nothing and pins `=y` per 2.16
- [x] 2.9 Add `# CONFIG_NUMA_BALANCING is not set` with the jitter rationale and the `numactl` alternative
- [x] 2.10 Mark the symbols already set by the base as pins in a comment, so a reader knows they are not a delta — base-set symbols carry `# pin:` comments; the header states that unmarked value lines are deltas or kept values, not asserted either way, pending the 1.4 re-audit
- [x] 2.11 Remove `CONFIG_VFIO`, `CONFIG_VFIO_PCI`, `CONFIG_PCI_IOV`, `CONFIG_PCI_PASID` — passthrough is out of scope (design D7); `CONFIG_PCI_STUB` went with them, since with no VFIO there is nothing to stub
- [x] 2.12 Keep `CONFIG_IRQ_REMAP` with a comment naming the dependency (`X86_X2APIC` depends on `IRQ_REMAP || HYPERVISOR_GUEST`) — not a CPU-count threshold, which reads as a reason to delete it on a 64-core box
- [x] 2.13 Leave `CONFIG_PANIC_TIMEOUT` at `0` and add no `PSTORE` symbols — pstore without a backend records nothing, and the backend is firmware-dependent (design D6) — no line for either; the base default stands
- [x] 2.14 Remove `CONFIG_IOMMU_SUPPORT`, `CONFIG_AMD_IOMMU`, `CONFIG_INTEL_IOMMU` from the fragment as **redundant pins** — `=y` builds a driver, `CONFIG_INTEL_IOMMU_DEFAULT_ON` decides activation, and the base leaves that unset, so the base already follows the BIOS (design D12) — all three gone, the reasoning kept in the comment that survives with `IRQ_REMAP`
- [x] 2.15 Record the audited kernel version in the fragment header as the single reference for the D10 gate
- [x] 2.16 Add `CONFIG_CPU_FREQ=y` as an explicit pin — `x86_64_defconfig` does not contain it; it is enabled only transitively by `X86_ACPI_CPUFREQ select CPU_FREQ`, so "the base has it" is an inference, and the cpufreq service depends on it
- [x] 2.17 Mark `CONFIG_PREEMPT_NONE=y` as an override of the base's explicit `CONFIG_PREEMPT_VOLUNTARY=y`, or leave the base choice alone — the throughput benefit for training is unmeasured. Left the base choice alone: the override is gone and a comment records why, instead of marking an unmeasured benefit as a decision
- [x] 2.18 Note in the fragment header that `CONFIG_MAGIC_SYSRQ=y` is already set by `x86_64_defconfig` — a pin, not the addition it was believed to be
- [x] 2.19 Delete `# CONFIG_PCI_P2PDMA is not set` (absent from the base, so the line is a no-op); keep `# CONFIG_DRM_NOUVEAU is not set` only with a comment that it is a defensive pin against a future base

## 3. Generator

- [x] 3.1 Create `kernel/scripts/generate_config.sh` merging all fragments in one pass, without `-m`, so the merge tool's own verification runs (design D1). Created; the merge call is `merge_config.sh -O $TREE $base $fragments` with no `-m`, verified against the real v6.12 `scripts/kconfig/merge_config.sh` (fetched and read: `-m` sets `RUNMAKE=false` and returns before the verification loop, and the interface is positional `BASE FRAG...` with no `-f`)
- [x] 3.2 Treat `Value requested for` in the merge output as a hard error; print the symbol and requested value; exit non-zero. `grep -q "Value requested for"` on the captured log, then `grep -A2` prints the symbol plus the merge tool's requested/actual lines, then `exit 1` — proven by test case 8, where the stub merge exits 0 and only this grep fails the run
- [x] 3.3 Reject malformed fragment lines before merging, so grammar is enforced rather than recommended. A per-line `case` rejects anything that is not blank, a comment, `CONFIG_X=y|m|n`, or `# CONFIG_X is not set` — including trailing text on a value line and commented-out values — and names file and line number; proven by test cases 6 and 7
- [x] 3.4 Require the target fragment to exist — no default path, no warn-and-continue (design D3). `kernel/fragments/target.fragment` is required and its absence fails with an explanation of what the warning-and-continue would have emitted; proven by test case 3. An example fragment (`*.example`) is also refused, so documentation cannot be merged as input (test case 11)
- [x] 3.5 Fail when the target fragment names no network controller driver. Each value line of the target fragment is classified by where the kernel tree defines the symbol — `drivers/net/` is a network controller driver — and the run fails if none qualifies. No driver list ships with the script; the tree's own Kconfig is the classifier. Proven by test case 4
- [x] 3.6 Write the merge log outside the repository, or add it to `.gitignore`. The merge log is a `mktemp` file under `${TMPDIR:-/tmp}`, deleted by the exit trap; nothing is written inside the repository except the generated `kernel/.config`, which is git-ignored
- [x] 3.7 Remove the base-config backup that is never read. There is no backup to remove: the base lives in a scratch `mktemp -d` that the exit trap deletes, and no code reads it after the merge. Nothing in the repository keeps a second copy of a config
- [x] 3.8 Add the kernel-version check: mismatch prints audited version, actual version, and the re-audit procedure, and is overridable only by an explicit `--allow-unaudited-kernel` flag — never by editing a constant (design D10). The audited series is parsed out of the fragment header's `Audited against:` line — the single reference, not a constant in the script — compared against `make -s kernelversion`; a mismatch prints both plus the re-audit procedure and fails, and only the flag bypasses it. Proven by test cases 2 (mismatch fails, flag overrides)
- [x] 3.9 Comment the fact that **`merge_config.sh` exits 0 even when requested symbols did not survive** — its verification loop only prints (v6.12 `scripts/kconfig/merge_config.sh`: the `Value requested for` loop has no `exit`), so the grep is the check and must never be replaced by reliance on the exit status. The comment sits directly above the grep and says exactly this, including the warning that replacing it with exit-status reliance silently removes the only difference from the `-m` version. Confirmed against the fetched v6.12 source: the loop at the end of the script has no `exit`
- [x] 3.10 Resolve fragment paths from one repository-root constant — `${SCRIPT_DIR}/../fragments` matches neither `scripts/build/` nor `kernel/` placement, and a detector writing to a path nobody reads is the same inert-artifact defect. `REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)` is the single constant; `FRAG_DIR`, the workload and target fragment paths, and the output path all hang off it, and the script sits at `kernel/scripts/` where `../..` is the root
- [x] 3.11 Generate the base config unconditionally rather than merging into an existing `.config`, so output does not depend on prior state. The base is `make O=<scratch-dir> alldefconfig` in a fresh `mktemp -d` — no prior `.config` is read, and the scratch dir has none by construction — and is passed to the merge as the positional base file
- [x] 3.12 Compare **values**, not presence: for every fragment line, read the symbol out of the generated configuration and compare against the requested value, including the `is not set` case, failing with symbol, requested, and actual. `merge_config.sh`'s own extraction is `s/^\(CONFIG_[a-zA-Z0-9_]*\)=.*/\1/p` — the value is discarded before comparison, so an invalid value on a real symbol passes its check (design D14). The generator does exactly this per line, including `is not set` and `=n`, failing with `symbol requested=… actual=…`. Proven by test cases 9 and 10, where the symbol survives the merge with the wrong value and only this check fails
- [x] 3.13 Require the target fragment to name a storage controller driver as well as a network driver — an NVMe or HBA symbol is the same class of "boots into an unmountable root" as a missing NIC, and the base defconfig is not responsible for either. The same classifier treats `drivers/nvme/`, `drivers/scsi/`, and `drivers/ata/` as storage controller drivers, and the run fails when the target fragment names none. Proven by test case 5

## 4. Target fragment scaffolding

- [x] 4.1 Add `kernel/fragments/target-nic.fragment.example` documenting the manual mapping step, clearly marked as an example that generation rejects. Written; the generator already refuses any `*.example` in the fragments directory (test case 11 of test_generate_config.sh)
- [x] 4.2 Add `kernel/scripts/detect_target_devices.sh` that prints PCI class `02` devices with vendor:device IDs — rename from "generate fragment", since it cannot generate anything (design D3). Written; it lists class 02 devices with vendor:device IDs and writes the fragment with TODO markers. No existing script was present in the repository, so "rename" landed as: the new script is named detect and its header says why it is not generate
- [x] 4.3 Fix the device filter: match the bracketed class field that `lspci -nn` actually emits, and exit non-zero when no network device is found. The awk filter matches the bracketed `\[02xx\]:` field, not the human-readable class name, and the run fails with an explanation when nothing class 02 is present — proven by test cases 15–17 (fake lspci with only a host bridge: rc 1, message, no fragment written)
- [x] 4.4 Fix the human-readable device name extraction so the one unautomatable step is not fed garbage. The name is everything between the class field and the `[vendor:device]` brackets, right-trimmed, and is kept on the comment line above each TODO so the human mapping step sees `00:02.0  Intel Corporation Ethernet Controller I219-V  8086:15bb` — proven by test case 3
- [x] 4.5 Have the detector write the target fragment with vendor:device IDs and unresolved TODO markers, and exit non-zero while any TODO remains. One TODO per class 02 device plus one for the storage controller; a fresh write exits 1 with the remaining count and the mapping instructions — proven by test cases 1, 2, and 6
- [x] 4.6 Refuse to overwrite a target fragment that has no unresolved TODOs, so a hand-mapped file cannot be destroyed by re-running the detector. Re-run on a complete file: exit 0, message, file byte-identical — proven by test cases 10 and 11
- [x] 4.7 Document the collection procedure in `docs/INSTALL_GUIDE.md` as a prerequisite, not a troubleshooting step. New "Collecting the target fragment (prerequisite)" section under Prerequisites, with the two-run procedure and the generator's refusal rules

## 5. Untrack the generated config

- [x] 5.1 `git rm --cached kernel/.config` — `.gitignore` alone does not untrack a tracked file. Done as `git rm kernel/.config` with the 2.1 move: the file's content lives in the tracked fragment now, and the path is free for the generator's output
- [x] 5.2 Anchor ignore rules for generated kernel artifacts — `/kernel/.config` is anchored in `.gitignore` (the old `.config.old` rule stands beside it), with the reason in a comment: the tracked content is the fragment, the generated file is not

## 6. NCCL runtime

- [ ] 6.1 Create `/etc/conf.d/nccl` as the single source, holding only variables that differ from NCCL defaults, with a header stating the single-node scope (design D8). `NCCL_DEBUG` already defaults to `WARN`, so setting it is a no-op line that reads like a decision — leave it commented out
- [ ] 6.1a Comment that OpenRC auto-sources `conf.d/<service-name>` only for the matching service name, which is why `cpufreq` needs no source line and `nccl` does
- [ ] 6.2 Export the sourced variables in each consumer with `set -a; source /etc/conf.d/nccl; set +a` — a non-exported `conf.d` assignment never reaches the training process. Keep the three lines adjacent: anything assigned between them is exported too (design D8)
- [ ] 6.3 Remove `NCCL_SOCKET_IFNAME`, `NCCL_IB_DISABLE`, `NCCL_SHM_DISABLE`, `NCCL_NET_GDR_LEVEL`, `NCCL_P2P_LEVEL` from every location
- [ ] 6.4 Delete `export NCCL_DEBUG=INFO` from `openrc/init.d/ai-training` and source the single file in `start_pre()`
- [ ] 6.5 Delete the NCCL export from `scripts/deploy/deploy_training_job.sh` and source the single file
- [ ] 6.6 Reduce `/etc/profile.d/nccl.sh` to a one-line shim sourcing the `conf.d` file
- [ ] 6.7 Update `ai-stack/cuda/setup_cuda.sh` so the installer writes the `conf.d` file and nothing else
- [ ] 6.8 Verify by inspection that all three entry points — login shell, deploy script, init script — resolve the same exported values
- [ ] 6.9 Keep a tuning ledger next to the file: one row per variable, naming the measurement that justifies it and the baseline it beat. A variable with no row is removed, and a default is not a finding — this is what stops the file refilling with the five variables 6.3 deletes (design D14)

## 7. CPU clocks in the init system

- [ ] 7.1 Add `openrc/init.d/cpufreq` pinning the governor to `performance`, configured in `openrc/conf.d/cpufreq` (design D4)
- [ ] 7.2 Count only CPUs whose governor write **succeeded** — `-w` checks permission bits, not the write, and an unchecked `echo` lets a rejected governor inflate the count — and `eend 1` on zero (design D11)
- [ ] 7.3 Report the count with `einfo`, not as `eend`'s message argument, which belongs to the failure line
- [ ] 7.4 State in the service config why the base `userspace` governor leaves clocks unset

## 8. Boot parameters documentation

- [ ] 8.1 Add `docs/BOOTLOADER.md` with `iommu=pt` stated as conditional on the IOMMU being enabled, naming both branches
- [ ] 8.2 Remove the "~30% throughput loss" and "Full P2P bandwidth" rows; replace with the command whose output shows whether P2P is active
- [ ] 8.3 Document how to observe the running command line and the IOMMU state without rebooting
- [ ] 8.4 Move the NCCL P2P test out of prose into a runnable script with `MASTER_ADDR`/`MASTER_PORT` set, so the snippet in the docs actually runs
- [ ] 8.5 Replace the conditional `iommu=pt` table with a troubleshooting section: default is not to set it, and it is added only for an observed P2P failure on a machine whose firmware enabled the IOMMU (design D12)
- [ ] 8.6 Keep the three system states documented — no IOMMU, IOMMU with `pt`, IOMMU without `pt` — since that is what the `boot-parameters` requirement asks for regardless of section layout
- [ ] 8.7 Do not ship the "most server BIOS default to IOMMU disabled" claim: it is an unsourced frequency claim with no measurement command. Ship `dmesg | grep -i iommu` instead
- [ ] 8.8 Correct the P2P trigger: `nvidia-smi topo -p2p r` reports `OK`/`ERR`/`UNS` availability, not bandwidth, and `NV#`/`SYS` link types come from `topo -m`
- [ ] 8.9 Make the P2P interpretation table exhaustive — an NVLink pair showing `ERR`/`UNS` with IOMMU active is the canonical failure and currently matches no row
- [ ] 8.10 Replace `hugepages=8192` with a formula or placeholder: a bare 16 GB reservation is the unsourced numeric claim the `docs-scope` requirement forbids

## 9. Scope and numeric claims

- [ ] 9.1 Add "single-node" to the `README.md` headline
- [ ] 9.2 Replace the `HARDWARE_REQUIREMENTS.md` network row that cites distributed training
- [ ] 9.3 Rewrite `MULTI_GPU_SETUP.md` for single-node, with the missing multi-node components enumerated as unverified
- [ ] 9.4 Remove the unsourced RAM, boot-time, and service-count table from `PHILOSOPHY.md`; replace with the measurement commands
- [ ] 9.5 Fix the boot-time measurement snippet: no `systemd-analyze`, and no `dmesg` string that does not occur
- [ ] 9.6 Replace the "Run NCCL test" install step with a single-node check
- [ ] 9.7 Remove the NCCL environment claim from `kernel/patches/README.md`
- [ ] 9.8 Grep for remaining multi-node, cluster, and distributed-training claims across `docs/`, `README.md`, and `scripts/`
- [x] 9.9 Replace the base-system links in `docs/INSTALL_GUIDE.md`: `lfs/view/stable-openrc/` and `blfs/view/stable-openrc/` return 404. Point Phase 1 and 2 at `lfs/view/stable/` and `blfs/view/stable/` — the SysVinit editions — and state plainly that the OpenRC layer replacing LFS's init chapters is BareCortex's own, with no upstream book
  - Closed, with the instruction's premise corrected. The 404s were read as absence and the cure offered was the SysVinit book; the LFS OpenRC edition exists (`~zeckma/openrc/lfs/13.0/`, `13.0-openrc`, 2026-03-05) and publishes no SysVinit successor, so Phase 1 and 2 now cite the pinned OpenRC renderings — no dead links, and no downgrade to a book this project does not build. `grep -rn stable-openrc` finds the string only in this task's text and in the design correction that recorded the misreading.
- [x] 9.10 Replace the GLFS citation with NVIDIA's own documentation, verified live: the datacenter driver installation guide, the CUDA Installation Guide for Linux, the CUDA Compatibility guide, and the OpenRC repository for the init layer
  - Closed by `base-system-and-init` 2.3: the three NVIDIA guides are cited in Phase 3 and the README, each fetched live before citation, and the OpenRC upstream is named in `docs/INIT-LAYER.md`'s substitution table. GLFS survives in exactly one place — the sentence explaining why it was dropped.
- [x] 9.11 OQ6 resolved: OpenRC, base is the LFS SysVinit edition, systemd not used, GPU stack from NVIDIA docs (design D13)
- [x] 9.12 Remove the `stable-systemd` edition from every reference, and add a line to `PHILOSOPHY.md` stating that systemd is not used — so the next reader does not arrive at this same contradiction
  - Closed, and both halves were already true when checked: `grep -rn stable-systemd` matches only this task's own text, and `docs/PHILOSOPHY.md` states the claim in the checkable form — no systemd as init or service manager and no `logind`, with the udev exception and the command that enforces it. Recorded rather than edited.

## 10. Verification available without hardware

- [ ] 10.1 Run the generator's fragment-grammar check against every fragment in the tree; expect zero malformed lines
- [ ] 10.2 Confirm the generator exits non-zero when the target fragment is absent
- [ ] 10.3 Confirm the **wrapper** exits non-zero for a deliberately misspelled symbol — the test must invoke `generate_config.sh`, not `merge_config.sh` directly, or it verifies the kernel's script and none of this change's logic; and it must not hide the exit status behind a `tee` pipeline
- [ ] 10.4 Confirm the version check fails on a mismatched tree and succeeds with `--allow-unaudited-kernel`
- [ ] 10.5 Grep documentation for numeric claims and confirm each names a measurement command and a hardware target
- [ ] 10.6 Grep every shipped script for an unconditional success report and confirm each is gated on work actually done (design D11)
- [ ] 10.7 `openspec validate --strict` passes for this change

## 11. Hand off hardware-dependent work

- [x] 11.1 Create the `target-hardware-bring-up` change owning: `lspci -nn` collection, target fragment authoring, kernel build, boot, SSH reachability, `nvidia-smi topo -p2p r`, and single-node NCCL validation — **created 2026-11-08**, 54 tasks; the seven items above land at its tasks 1.1, 2.1, 3.1, 3.3, 3.6, 5.2, 6.3 respectively. Its group 0 gates the whole change on a machine existing, so the deferral cannot quietly become a claim
- [ ] 11.2 Move from this change's acceptance criteria every claim that cannot be checked without the target machine
- [ ] 11.3 Carry into that change: NIC drivers, EDAC core plus controller driver, `NR_CPUS` if the target exceeds the base default, pstore backend, and the observed governor once settable — **mapped 2026-11-08** to its 1.2/2.1, 1.4/2.2, 1.5, 1.6, 7.1; each of that change's tasks cites back to the deferral it closes
