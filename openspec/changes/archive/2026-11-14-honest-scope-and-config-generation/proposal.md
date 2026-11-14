# Proposal: honest-scope-and-config-generation

## Why

The repository claims capabilities it does not have (multi-node training, measured performance figures) and its kernel configuration contains symbols that do not exist, so nothing in `kernel/` can be trusted as a specification. Worse, the config generator as designed cannot fail: `merge_config.sh -m` skips the kernel's own missed-dependency check, and `x86_64_defconfig` ships exactly one NIC driver (`CONFIG_VIRTIO_NET`, a KVM paravirtual device), so a "successful" build produces a kernel with no network on real hardware. We cannot fix what we cannot fail at.

## What Changes

- Narrow the declared scope to **single-node, multi-GPU**. Multi-node / InfiniBand / RDMA becomes explicitly enumerated future work. **BREAKING** for anyone who read the current README as cluster support.
- Kernel config becomes a **fragment plus a generator that fails loudly**: merge without `-m`, so the kernel's built-in verification runs, and treat its output as a hard error.
- Fragment files get a **machine-readable line grammar**: only `CONFIG_X=value` or `# CONFIG_X is not set`, nothing else on the line. "We decided not to set this" is expressed by absence, never by a commented-out line.
- Target-device drivers move to a **required, target-derived fragment**. A missing target fragment is an error, not a warning — a warning is how the no-network kernel ships.
- Remove GPU-passthrough machinery (`VFIO`, `VFIO_PCI`, `PCI_IOV`, `PCI_PASID`) as out of scope. **BREAKING** for anyone planning VM passthrough on a BareCortex host.
- NCCL environment gets **one source of truth that every execution path actually reads**, including the OpenRC service, which today never reads `/etc/profile.d/` and hardcodes its own `NCCL_DEBUG`.
- Delete unsourced numeric claims from documentation; a number without a measurement command and a hardware target is not allowed.
- Add the kernel options the project's own claims imply but that are absent: `CONFIG_MAGIC_SYSRQ=y`, and target-derived device drivers. EDAC and panic-evidence handling are deferred with them — both are inert without hardware-specific backends, and an inert option reads like coverage.
- Stop pinning the IOMMU in the fragment, and move `iommu=pt` from a documented configuration choice to a troubleshooting fix for an observed peer-to-peer failure. `=y` on the IOMMU drivers builds a driver; firmware decides activation, which the base defconfig already leaves to it.

## Capabilities

### New Capabilities

- `kernel-config`: Kernel configuration fragments, their line grammar, generation, and the verification that must fail when a symbol does not survive.
- `boot-parameters`: Kernel command-line parameters affecting GPU behaviour, each with the condition under which it applies and an observable check.
- `nccl-runtime`: NCCL runtime environment — where it is defined and how each execution path receives it.
- `docs-scope`: Scope statements and numeric-claim discipline across documentation and scripts.

### Modified Capabilities

None. `openspec/specs/` is empty; this change establishes the first four capabilities.

## Impact

- `kernel/` — `.config` becomes generated and untracked (requires `git rm --cached kernel/.config`); new `fragments/` and `scripts/generate_config.sh`.
- `docs/` — `MULTI_GPU_SETUP.md`, `HARDWARE_REQUIREMENTS.md`, `INSTALL_GUIDE.md`, `PHILOSOPHY.md` reworded; new `BOOTLOADER.md`.
- `README.md` — headline scope.
- `ai-stack/cuda/setup_cuda.sh`, `scripts/deploy/deploy_training_job.sh`, `openrc/init.d/ai-training` — NCCL env consolidated into `/etc/conf.d/nccl`.
- New `openrc/init.d/cpufreq` plus `openrc/conf.d/cpufreq`, replacing the "disable CPU_FREQ" idea.
- Deferred to a follow-up change: everything requiring the physical target machine — NIC enumeration, EDAC core and controller driver, `NR_CPUS`, pstore backend, kernel build, boot, P2P and NCCL validation. No target hardware is currently accessible.
