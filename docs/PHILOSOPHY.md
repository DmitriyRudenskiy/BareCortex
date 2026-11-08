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
- Python `venv` provides dependency isolation without container overhead
- Direct PCIe access to GPUs is simpler without device passthrough
- NCCL multi-GPU communication is more predictable on bare metal

**Alternative:** Python virtual environments + system-level CUDA installation.

### ❌ No systemd

systemd is a powerful init system, but it pulls in dependencies and services
that a headless compute node doesn't need. OpenRC provides:

- Simple, readable init scripts (bash-based)
- Lower memory footprint
- Easier debugging when services fail
- Full compatibility with the LFS ecosystem

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

- `nvidia-persistenced` keeping GPUs initialized
- SSH access for remote management
- tmux for persistent training sessions
- Optional X11 forwarding for GUI tools (Nsight, TensorBoard)

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
