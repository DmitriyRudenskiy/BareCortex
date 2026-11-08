# 🎮 Multi-GPU Setup Guide

## Verifying GPU Detection

After installing NVIDIA drivers, verify all GPUs are detected:

```bash
# List all NVIDIA GPUs
nvidia-smi -L

# Expected output:
# GPU 0: NVIDIA A100-SXM4-80GB (UUID: GPU-xxxx)
# GPU 1: NVIDIA A100-SXM4-80GB (UUID: GPU-xxxx)
```

## GPU Topology

Check how your GPUs are connected:

```bash
nvidia-smi topo -m
```

Example output:
```
        GPU0  GPU1  NIC0  CPU Affinity
GPU0     X    NV18  SYS   0-31
GPU1    NV18   X    SYS   32-63
```

| Symbol | Meaning |
|---|---|
| NV# | NVLink with # links |
| PIX | Same PCIe switch |
| PXB | Multiple PCIe switches |
| PHB | PCIe host bridge |
| SYS | System bus (QPI/UPI) |
| X | Self |

## NCCL Configuration

For optimal multi-GPU communication, set these environment variables:

```bash
# In your training script or /etc/profile.d/nccl.sh
export NCCL_DEBUG=INFO
export NCCL_SOCKET_IFNAME=eth0          # Your network interface
export NCCL_IB_DISABLE=1                # Disable InfiniBand if not available
export NCCL_P2P_LEVEL=NVL               # Use NVLink for P2P
export NCCL_ALGO=Ring                   # Ring algorithm (or Tree)
export NCCL_PROTO=Simple                # Protocol
```

## PyTorch Multi-GPU

### DataParallel (simple, single-node)

```python
import torch

model = MyModel()
if torch.cuda.device_count() > 1:
    print(f"Using {torch.cuda.device_count()} GPUs!")
    model = torch.nn.DataParallel(model)

model = model.cuda()
```

### DistributedDataParallel (recommended)

```python
import torch
import torch.distributed as dist
import torch.multiprocessing as mp

def train(rank, world_size):
    dist.init_process_group("nccl", rank=rank, world_size=world_size)
    model = MyModel().to(rank)
    model = torch.nn.parallel.DistributedDataParallel(model, device_ids=[rank])
    # ... training loop ...
    dist.destroy_process_group()

world_size = torch.cuda.device_count()
mp.spawn(train, args=(world_size,), nprocs=world_size)
```

## Running Multi-GPU Training via OpenRC

See `/etc/init.d/ai-training` for a service that launches your training
script on boot. Configure the script path and GPU selection in
`/etc/conf.d/ai-training`.

## Performance Monitoring

```bash
# Real-time GPU monitoring
nvtop

# Detailed GPU stats every 1 second
nvidia-smi dmon -s pucvmet -d 1

# GPU utilization logging to file
nvidia-smi --query-gpu=timestamp,name,utilization.gpu,utilization.memory,memory.used,memory.total,power.draw,temperature.gpu --format=csv -l 1 >> gpu_log.csv &
```
