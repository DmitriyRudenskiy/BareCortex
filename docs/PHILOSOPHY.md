# 🧠 BareCortex Philosophy

## Why build from scratch?

Commercial Linux distributions ship with hundreds of packages that a dedicated
AI compute server will never use. Each unnecessary daemon, each abstraction
layer, each compatibility shim consumes CPU cycles, RAM, and — most
critically — introduces potential points of failure during long training runs.

BareCortex is built on the principle: **if it's not directly involved in
feeding data to the GPU or keeping the system stable, it doesn't belong here.**

## Key Design Decisions

### ❌ No Docker / No Containers

Docker adds filesystem isolation, network namespacing, and cgroup management
overhead. For a dedicated AI training server, this overhead provides no benefit:

- You control the entire system yourself
- A `uv`-managed venv, installed from a lock file, isolates dependencies without a daemon or a second filesystem above the one being debugged
- Direct PCIe access to GPUs is simpler without device passthrough
- NCCL multi-GPU communication is more predictable on bare metal

**Alternative:** a uv-managed virtual environment per project, resolved once into
a lock file, over a system-level CUDA installation. The property containers were
offered for is reproducibility, and a lock file is what actually provides it —
decided in [docs/LANGUAGE-POLICY.md](LANGUAGE-POLICY.md), where `create_venv.sh`
still saying `venv` and `pip` is recorded as the change that has to follow.

### ❌ No systemd

systemd is a powerful init system, but it pulls in dependencies and services
that a headless compute node doesn't need. OpenRC provides:

- `openrc-run` service scripts, one file per service
- Lower memory footprint
- Easier debugging when services fail
- Full compatibility with the LFS ecosystem

The claim, stated so that it can be checked: **no systemd as init or service
manager, and no `logind`.** Not "no systemd source is compiled" — the pinned
edition builds udev from `systemd-259.1.tar.xz`, with `-D link-udev-shared=false`
exactly so that udev does not resolve `libsystemd-shared`. The command behind the
claim is `scripts/build/check-no-systemd.py --root <image>`, and what PID 1 is
lives in [the init layer](INIT-LAYER.md). A claim of this kind that no command
verifies is a sentence waiting for the first package that links it anyway.

### ❌ No Wayland (by default)

Wayland compositors add complexity for multi-GPU NVIDIA setups. Xorg is:

- Battle-tested for multi-GPU configurations
- Better supported by NVIDIA proprietary drivers
- Easier to configure headless (no compositor needed)
- Supports X11 forwarding for remote visualization

**Default mode:** Pure headless (no X server running).
**Optional:** Xorg in headless/dummy mode if specific tools require it.

### ✅ Bare-Metal AI Stack

All AI components are installed directly on the host system:

```
NVIDIA Driver → CUDA Toolkit → cuDNN → NCCL
     ↓
Python venv → PyTorch / TensorFlow → Your training script
```

This eliminates:
- Container filesystem overhead
- Network namespace translation
- GPU device mounting complexity
- NCCL topology detection issues in containers

### ✅ Headless-First Design

A training server doesn't need a desktop environment. BareCortex boots into
a multi-user text console with:

- `nvidia-persistenced` holding the driver's device state while no client is using the GPU
- SSH access for remote management
- tmux for persistent training sessions
- Optional X11 forwarding for GUI tools (Nsight, TensorBoard)

### Decisions, so they stop arriving with the next consultation

Three things get re-proposed by every piece of outside advice about this project.
They are decided here, and the reason is the kind that can be checked.

**IOMMU and VFIO do not come back "in case a VM is needed".** They come back on
a stated VM requirement — a named guest, on a named device, doing a named job.
`CONFIG_VFIO` and `CONFIG_VFIO_PCI` are out of the fragment for having no such
requirement behind them, and the `=y` IOMMU pins are redundant in a different
way: `=y` builds a driver, while activation is `CONFIG_INTEL_IOMMU_DEFAULT_ON`,
which the base leaves unset, so the base already follows the BIOS.

**Out-of-memory during training is handled by `vm.overcommit_memory` and a cgroup
v2 memory limit — not by `ulimit`.** `ulimit -v` does not disable the OOM killer;
it makes individual allocations fail, which converts one clean kill into an
unpredictable failure inside whatever library happened to ask. The troubleshooting
row in the install guide is correct; anything suggesting `ulimit` as OOM policy is
not describing the mechanism it names.

**`nvidia-persistenced` is described by what it does.** From the man page shipped
with the installed driver: it "is used to enable persistent software state in the
NVIDIA driver. When persistence mode is enabled, the daemon prevents the driver
from releasing device state when the device is not in use. This can improve the
startup time of new clients in this scenario." It does not remove a per-epoch
initialisation delay, and it is not what keeps a fan spinning or a clock pinned.

## Performance Impact

| Component | Traditional Distro | BareCortex |
|---|---|---|
| Init system | systemd (~50+ services) | OpenRC (~10 services) |
| Display server | Wayland compositor | None / Xorg headless |
| Container runtime | Docker + containerd | Not present |
| GPU access | Container passthrough | Direct PCIe |
| RAM overhead | ~500-800 MB | ~80-120 MB |
| Boot time | ~15-30s | ~5-8s |

## Naming

**Bare** — bare metal, no abstractions, no containers
**Cortex** — the cerebral cortex, the thinking part of the brain

> BareCortex: the raw, unfiltered intelligence of your hardware.
