<h1 align="center">BareCortex</h1>

<p align="center">
  <b>Bare-metal AI compute OS built on LFS + OpenRC</b><br>
  Optimized for multi-GPU NVIDIA neural network training<br>
  No Docker. No Wayland. No bloat. Pure performance.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/OS-Linux_LFS-orange" alt="OS">
  <img src="https://img.shields.io/badge/init-OpenRC-blue" alt="Init">
  <img src="https://img.shields.io/badge/display-Headless-green" alt="Display: no X server by default; Xorg headless/dummy is the option">
  <img src="https://img.shields.io/badge/containers-None-red" alt="Containers">
  <img src="https://img.shields.io/badge/license-MIT-yellow" alt="License">
</p>

---

## 🔥 What is BareCortex?

**BareCortex** is a highly optimized Linux distribution built from scratch
using the **Linux From Scratch (LFS)** ecosystem. It is purpose-built for
running neural network training workloads on servers with **multiple NVIDIA GPUs**.

Unlike traditional distros that ship with unnecessary services and abstraction
layers, BareCortex is stripped down to the bare minimum needed to:

- Initialize and maintain multiple NVIDIA GPUs
- Provide direct access to CUDA, cuDNN, and NCCL
- Run Python-based ML frameworks at maximum speed
- Ensure 24/7 stability under heavy compute loads

## 🎯 Philosophy

| Principle | Implementation |
|---|---|
| **No containers** | All AI frameworks run directly on the host system via Python venvs |
| **No systemd as init or service manager** | OpenRC is PID 1 (`readlink /proc/1/exe` → `openrc-init`); `scripts/build/check-no-systemd.py --root /` fails the build if any binary resolves `libsystemd`. udev is still built from the systemd tarball — [docs/INIT-LAYER.md](docs/INIT-LAYER.md) |
| **64-bit only, no 32-bit code** | Every ELF file in the image is `ELFCLASS64` `EM_X86_64` and there is no `lib32`/`libx32` directory; `scripts/build/check-amd64-only.py --root <image>` reads `EI_CLASS` and `e_machine` from every header and refuses to run without an image root — [docs/GENTOO-AS-BASE.md](docs/GENTOO-AS-BASE.md) |
| **No Wayland** | Xorg for stability or pure headless mode for servers |
| **Bare metal** | Zero abstraction between your code and the GPU — except the GPU driver and CUDA userspace, which are NVIDIA binaries, not built here |
| **From scratch** | Built from the LFS and BLFS OpenRC editions — the GPU stack comes from NVIDIA's own guides, because GLFS is a gaming book |

## 🚀 Key Features

- **OpenRC init system** — fast boot, minimal overhead, full control
- **Multi-GPU NVIDIA support** — optimized for 2+ GPUs with NVLink/PCIe
- **Bare-metal AI stack** — CUDA, cuDNN, NCCL installed natively
- **Headless-first design** — no GUI required for training workloads
- **X11 forwarding support** — optional Xorg for remote visualization
- **Optimized kernel** — custom config for AI compute workloads
- **Custom OpenRC services** — GPU persistence, fan control, training jobs

## 📋 Base Technology Stack

```
LFS 13.0-openrc               → Base system (Python, Ninja and Meson are LFS packages here)
BLFS r13.0-194 (svn)          → Server tools: OpenSSH, Git, CMake, curl
NVIDIA + CUDA guides          → Driver, toolkit, cuDNN, NCCL
Custom kernel config          → AI-optimized
uv-managed venv               → Framework isolation (instead of Docker)
Xorg (optional/headless)      → Display server
```

## 📖 Documentation

- [Philosophy & Design Decisions](docs/PHILOSOPHY.md)
- [Hardware Requirements](docs/HARDWARE_REQUIREMENTS.md)
- [Installation Guide](docs/INSTALL_GUIDE.md)
- [Multi-GPU Setup Guide](docs/MULTI_GPU_SETUP.md)
- [Pre-build Notes](docs/PREBUILD.md) — what is ready for a build and what a machine still owes
- [Language and dependency policy](docs/LANGUAGE-POLICY.md) — what gets adopted instead of written, and what never gets rewritten

## ⚡ Quick Start

```bash
# Clone the repository
git clone https://github.com/DmitriyRudenskiy/BareCortex.git
cd BareCortex

# See what a build actually does, phase by phase
cat phases.toml

# Run one phase against the build host
python3 scripts/build/build.py --help

# Follow the installation guide
cat docs/INSTALL_GUIDE.md
```

## 🗂 Repository Structure

```
BareCortex/
├── docs/           # Documentation and guides
├── kernel/         # Kernel config and patches
├── openrc/         # OpenRC init scripts and configs
├── graphics/       # Xorg and headless display configs
├── ai-stack/       # CUDA, cuDNN, framework setup scripts
├── scripts/        # Build, maintenance, and deploy scripts
├── utils/          # Shipped system utilities (Rust) and the coverage ledger
├── openspec/       # Specifications and in-flight change proposals
└── patches/        # NVIDIA and glibc compatibility patches
```

## 📜 License

This project is licensed under the MIT License — see the
[LICENSE](LICENSE) file for details.

## 🙏 Acknowledgments

- [Linux From Scratch](https://www.linuxfromscratch.org/) — the foundation
- [LFS OpenRC Edition](https://www.linuxfromscratch.org/~zeckma/openrc/lfs/13.0/) — the pinned base, 13.0-openrc of 2026-03-05
- [BLFS OpenRC Edition](https://www.linuxfromscratch.org/~zeckma/openrc/blfs/svn/) — server packages, an SVN rendering with no released edition
- [NVIDIA Data Center GPU Driver Installation Guide](https://docs.nvidia.com/datacenter/tesla/driver-installation-guide/index.html), [CUDA Installation Guide for Linux](https://docs.nvidia.com/cuda/cuda-installation-guide-linux/), [CUDA Compatibility Guide](https://docs.nvidia.com/deploy/cuda-compatibility/index.html) — the GPU stack
- The LFS community for patches and hints

---

> *"Zero overhead. Zero abstraction. Pure compute."*

One exception, stated where the claim is rather than in a footnote: the GPU
driver, CUDA Toolkit and cuDNN are binary distributions from NVIDIA. They are not
built from source here, and they are the layer between this system and the
hardware.
