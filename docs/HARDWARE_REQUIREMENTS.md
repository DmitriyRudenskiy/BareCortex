# 🔧 Hardware Requirements

## Minimum Build Environment

To **build** BareCortex, you need a working Linux system with:

| Component | Minimum | Recommended |
|---|---|---|
| CPU | x86_64, 2 cores | 4+ cores |
| RAM | 2 GB | 8 GB |
| Disk | 20 GB | 50 GB |
| OS | Any modern Linux | Ubuntu/Debian/Fedora |

## Target Server Requirements

For the **production** BareCortex system:

| Component | Minimum | Recommended |
|---|---|---|
| CPU | x86_64, 4 cores | 16+ cores (AMD EPYC / Intel Xeon) |
| RAM | 16 GB | 64 GB+ (depends on model size) |
| GPU | 1x NVIDIA (Maxwell+) | 2x+ NVIDIA (Ampere/Hopper/Blackwell) |
| Disk | 50 GB SSD | 500 GB+ NVMe (datasets, checkpoints) |
| Network | 1 Gbps | 10 Gbps+ (fast dataset and checkpoint transfer; there is no cluster to connect to) |

## Supported NVIDIA GPU Architectures

| Architecture | Examples | CUDA Support | Status |
|---|---|---|---|
| Maxwell | GTX 900 series | CUDA 12.x | ✅ Supported |
| Pascal | GTX 10 series | CUDA 12.x | ✅ Supported |
| Volta | V100 | CUDA 12.x | ✅ Supported |
| Turing | RTX 20 series | CUDA 12.x | ✅ Supported |
| Ampere | RTX 30 series, A100 | CUDA 12.x | ✅ Recommended |
| Ada Lovelace | RTX 40 series | CUDA 12.x | ✅ Recommended |
| Hopper | H100 | CUDA 12.x | ✅ Recommended |
| Blackwell | RTX 50 series | CUDA 13.x | ✅ Supported |

## Multi-GPU Interconnect

For optimal multi-GPU training performance:

| Interconnect | Bandwidth | Recommended For |
|---|---|---|
| PCIe Gen3 x16 | ~16 GB/s | 2 GPUs, small models |
| PCIe Gen4 x16 | ~32 GB/s | 2-4 GPUs, medium models |
| PCIe Gen5 x16 | ~64 GB/s | 4+ GPUs, large models |
| NVLink 3.0 | 600 GB/s | Multi-GPU (A100) |
| NVLink 4.0 | 900 GB/s | Multi-GPU (H100) |

## Storage Recommendations

| Purpose | Type | Size |
|---|---|---|
| OS + CUDA | NVMe SSD | 100 GB |
| Datasets | NVMe SSD / HDD RAID | 500 GB+ |
| Model checkpoints | NVMe SSD | 200 GB+ |
| Swap | NVMe SSD | 2x RAM size |
