# 📦 BareCortex Installation Guide

> ⚠️ **This is not a traditional Linux installer.**
> BareCortex is built from source using the LFS methodology.
> Expect 20-60 hours for a full build depending on your hardware.

## Prerequisites

- A working x86_64 Linux system (host)
- Root access on the host
- Internet connection
- Patience and a good book

## Step-by-Step Overview

### Phase 1: Base System (LFS)

Build the core system using the **LFS OpenRC Edition** book.

```bash
# Set up build environment
export LFS=/mnt/barecortex
mkdir -p $LFS

# Follow the LFS book (OpenRC Edition)
# https://www.linuxfromscratch.org/lfs/view/stable-openrc/
```

**Key decisions during build:**
- Architecture: x86_64 (pure 64-bit, no multilib)
- Init system: OpenRC
- Text editor: vim or nano (your choice)

### Phase 2: Server Environment (BLFS)

Install essential server packages from **BLFS OpenRC Edition**.

```bash
# Follow BLFS book (OpenRC Edition)
# https://www.linuxfromscratch.org/blfs/view/stable-openrc/
```

**Required packages:**
- Python 3.12+
- OpenSSH server
- Git
- GCC, CMake, Make, Ninja
- curl, wget
- tmux, htop, nvtop
- FFmpeg (for data preprocessing)
- Xorg Server (optional, for headless mode)

### Phase 3: NVIDIA Drivers & CUDA (GLFS)

Install GPU stack from **GLFS OpenRC Edition**.

```bash
# Follow GLFS book (OpenRC Edition)
# https://www.linuxfromscratch.org/glfs/view/dev-openrc/
```

**Required components:**
- NVIDIA proprietary driver (r580 or r610 branch)
- CUDA Toolkit
- cuDNN
- NCCL

### Phase 4: Apply BareCortex Configurations

```bash
# Clone this repository on your new system
git clone https://github.com/YOUR_USERNAME/BareCortex.git /opt/barecortex
cd /opt/barecortex

# Install kernel config
cp kernel/.config /usr/src/linux/.config

# Install OpenRC services
cp openrc/init.d/* /etc/init.d/
cp openrc/conf.d/* /etc/conf.d/

# Install Xorg configs (if using headless X)
cp graphics/xorg.conf.d/* /etc/X11/xorg.conf.d/

# Enable services
rc-update add nvidia-persistenced default
rc-update add gpu-fancontrol default
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
