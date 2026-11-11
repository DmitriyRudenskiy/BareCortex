<h1 align="center">BareCortex</h1>

<p align="center">
  <b>Bare-metal AI compute OS built on LFS + OpenRC</b><br>
  Optimized for multi-GPU NVIDIA neural network training<br>
  No Docker. No Wayland. No bloat. Pure performance.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/OS-Linux_LFS-orange" alt="OS">
  <img src="https://img.shields.io/badge/init-OpenRC-blue" alt="Init">
  <img src="https://img.shields.io/badge/display-Xorg-green" alt="Display">
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
| **No systemd** | Uses lightweight OpenRC init system |
| **No Wayland** | Xorg for stability or pure headless mode for servers |
| **Bare metal** | Zero abstraction between your code and the GPU |
| **From scratch** | Built with LFS + BLFS + GLFS (OpenRC Edition) |

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
LFS (OpenRC Edition)          → Base system
BLFS (OpenRC Edition)         → Server tools, Python, compilers
GLFS (OpenRC Edition)         → NVIDIA drivers, CUDA Toolkit
Custom kernel config          → AI-optimized
Python venv                   → Framework isolation (instead of Docker)
Xorg (optional/headless)      → Display server
```

## 📖 Documentation

- [Philosophy & Design Decisions](docs/PHILOSOPHY.md)
- [Hardware Requirements](docs/HARDWARE_REQUIREMENTS.md)
- [Installation Guide](docs/INSTALL_GUIDE.md)
- [Multi-GPU Setup Guide](docs/MULTI_GPU_SETUP.md)
- [Pre-build Notes](docs/PREBUILD.md) — what is ready for a build and what a machine still owes

## ⚡ Quick Start

```bash
# Clone the repository
git clone https://github.com/YOUR_USERNAME/BareCortex.git
cd BareCortex

# Review the build preparation script
cat scripts/build/prepare_build.sh

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
- [GLFS (OpenRC Edition)](https://www.linuxfromscratch.org/glfs/) — NVIDIA & CUDA instructions
- [BLFS (OpenRC Edition)](https://www.linuxfromscratch.org/blfs/) — server packages
- The LFS community for patches and hints

---

> *"Zero overhead. Zero abstraction. Pure compute."*
