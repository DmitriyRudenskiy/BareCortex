# 📦 BareCortex Installation Guide

> ⚠️ **This is not a traditional Linux installer.**
> BareCortex is built from source using the LFS methodology.
> Expect 20-60 hours for a full build depending on your hardware.
>
> **What the build actually is.** This guide is the manual, book-following
> procedure for a person at a keyboard. The build in this repository is the
> phase sequence in [phases.toml](../phases.toml), driven by
> [`scripts/build/build-rootfs.sh`](../scripts/build/build-rootfs.sh), with the
> package list derived from the pinned book into
> [base.lock](../base.lock) and every phase's result recorded under
> [`build-evidence/`](../build-evidence/). The base system is built — eleven
> phase records, the last one `first-boot` — while the server, GPU, and trim
> layers are not phases yet: they are open changes under
> [openspec/changes/](../openspec/changes/).

## Prerequisites

- A working x86_64 Linux system (host)
- Root access on the host
- Internet connection
- Patience and a good book

### Collecting the target fragment (prerequisite)

The kernel is built from fragments: `kernel/fragments/ai-workload.fragment`
(checked into the repository) plus a per-machine
`kernel/fragments/target.fragment` naming this machine's network and storage
controller drivers. On the target machine, before the first build:

```bash
sh kernel/scripts/detect_target_devices.sh
# lists the class 02 devices and writes target.fragment with TODO lines
# replace each TODO with CONFIG_<DRIVER>=y for the vendor:device ID
sh kernel/scripts/detect_target_devices.sh
# again: no TODOs left, the hand-mapped file is not overwritten
```

The generator (`kernel/scripts/generate_config.sh`) refuses to build while
any TODO remains, or while an `*.example` file sits in `kernel/fragments/`.

## Step-by-Step Overview

### Phase 1: Base System (LFS)

Build the core system using the **LFS OpenRC Edition** book.

```bash
# Set up build environment
export LFS=/mnt/barecortex
mkdir -p $LFS

# Build from the LFS OpenRC Edition. It is not in the main view index, so the
# path is the citation:
#   https://www.linuxfromscratch.org/~zeckma/openrc/lfs/13.0/
# Edition 13.0-openrc, published 2026-03-05, patching LFS 13.0. The edition
# publishes no SVN revision, so version plus date is the whole pin — and
# base.lock's [edition] row is what the build actually used. The same directory
# also offers stable/ and development/; both move, so neither is cited here.
```

**Key decisions during build:**
- Architecture: x86_64 (pure 64-bit, no multilib)
- Init system: OpenRC
- Text editor: vim or nano (your choice)

### Phase 2: Server tools, Python, compilers

Not "follow BLFS". The image uses the packages in the table, and each row says
where it comes from — because the BLFS OpenRC material is an SVN snapshot
(r13.0-194, 2026-03-23, at
`https://www.linuxfromscratch.org/~zeckma/openrc/blfs/svn/`) with no released
edition behind it, and its own version page says it targets the LFS
*development* book rather than the 13.0 base built above. Every page path below
is relative to that rendering.

| Package | Where it comes from |
|---|---|
| Python 3.14.3, GCC 15.2.0, Make 4.4.1, Ninja 1.13.2, Meson, Pkgconf, OpenSSL 3.6.1 | `base.lock`, built from chapter 8 of the **base** book. Python and Ninja are LFS packages in this edition, not BLFS ones — so "Python 3.12+" is not a choice to make here, the base pins 3.14.3 |
| OpenSSH, Git, CMake, curl, wget, FFmpeg | Have a page in the pinned BLFS rendering — `postlfs/openssh.html`, `general/git.html`, `general/cmake.html`, `basicnet/curl.html`, `basicnet/wget.html`, `multimedia/ffmpeg.html` — and **no `base.lock` entry yet**. `userspace-and-ml-stack` group 3 adds them, one citation per entry |
| tmux, chrony, htop, nvtop | **No page in the pinned BLFS rendering**, checked against its own index rather than recalled. `userspace-and-ml-stack` 0.7 names the route before any of them installs; `nvtop` is not a BLFS package at all |
| Xorg Server | Optional. The default is no X server at all ([PHILOSOPHY.md](PHILOSOPHY.md)) |

`base.lock` is the authority; this table is a reading of the pinned rendering's
index, and nothing is installed because it appears here.

### Phase 3: NVIDIA drivers and CUDA

GLFS is a gaming book: its OpenRC rendering installs drivers for a desktop, not
for a headless node with five cards. The GPU stack comes from the vendor
documentation, which is what actually describes the datacenter driver installer,
the CUDA runfile layout, and the driver/toolkit compatibility matrix:

- [NVIDIA Data Center GPU Driver Installation Guide](https://docs.nvidia.com/datacenter/tesla/driver-installation-guide/index.html)
- [CUDA Installation Guide for Linux](https://docs.nvidia.com/cuda/cuda-installation-guide-linux/)
- [CUDA Compatibility Guide](https://docs.nvidia.com/deploy/cuda-compatibility/index.html)

All three resolved and were read on 2026-11-11. Citing a book that does not
describe the installer being run is how a driver gets installed by guesswork.
The datacenter guide is cited for the `.run` installer, the kernel-module
parameters and the persistence daemon; the branch that supports the pinned card
is read from the driver's own supported-device list, not chosen from a document.

**Required components:**
- NVIDIA proprietary driver — **no branch is decided here.** It comes out of the
  supported-device list for the pinned card, and `HARDWARE_REQUIREMENTS.md`
  records the driver as `unknown` until a build records it. A branch number in an
  install guide is a version claim with no hardware behind it
- CUDA Toolkit
- cuDNN
- NCCL

**No X server.** Compute work does not need a display server: the kernel module,
the device nodes and `nvidia-smi` exist without one, and the headless default in
[PHILOSOPHY.md](PHILOSOPHY.md) is not a limitation to work around. Installing X
so that a GPU "works" is how a training host ends up booting a display manager
that nothing talks to.

### Phase 4: Apply BareCortex Configurations

```bash
# Clone this repository on your new system
git clone https://github.com/DmitriyRudenskiy/BareCortex.git /opt/barecortex
cd /opt/barecortex

# Generate the kernel config from kernel/fragments/ (requires the
# per-machine target.fragment — see "Collecting the target fragment"
# above) and install it
sh kernel/scripts/generate_config.sh /usr/src/linux
cp kernel/.config /usr/src/linux/.config

# Install OpenRC services
cp openrc/init.d/* /etc/init.d/
cp openrc/conf.d/* /etc/conf.d/

# Install Xorg configs (if using headless X)
cp graphics/xorg.conf.d/* /etc/X11/xorg.conf.d/

# Enable services
rc-update add nvidia-persistenced default
rc-update add gpu-fancontrol default
rc-update add cpufreq default
rc-update add sshd default
```

### Phase 5: AI Stack Setup

```bash
# Install CUDA environment
bash ai-stack/cuda/setup_cuda.sh

# Create a virtual environment for your project
bash ai-stack/environments/create_venv.sh my_project

# Install PyTorch with CUDA support
bash ai-stack/frameworks/install_pytorch.sh my_project
```

### Phase 6: Verify Installation

```bash
# Check NVIDIA driver
nvidia-smi

# Check CUDA
nvcc --version

# Check multi-GPU communication
python -c "import torch; print(torch.cuda.device_count())"

# Run NCCL test (if installed)
/usr/local/cuda/samples/1_Utilities/deviceQuery/deviceQuery
```

## Post-Installation

### Set up tmux for training sessions

```bash
tmux new-session -s training
# Run your training script here
# Detach: Ctrl+B, then D
# Reattach: tmux attach -t training
```

### Configure SSH X11 forwarding (optional)

```bash
# On the server, ensure /etc/ssh/sshd_config has:
X11Forwarding yes

# Connect from your workstation:
ssh -X user@barecortex-server
```

## Troubleshooting

| Issue | Solution |
|---|---|
| `nvidia-smi` not found | Check driver installation, run `modprobe nvidia` |
| CUDA not detected | Verify `nvcc` is in PATH, check `/usr/local/cuda` |
| Multi-GPU not working | Check NCCL, verify PCIe topology with `nvidia-smi topo -m` |
| System OOM during training | Increase swap, tune `vm.overcommit_memory` |
| GPU falls off PCIe | Disable ASPM, check power supply, update firmware |
