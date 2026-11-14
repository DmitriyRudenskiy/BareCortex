# 🎮 Multi-GPU Setup Guide

**Scope: single node.** Everything here runs on one machine with several
GPUs. The multi-node components are not part of this system and are not
verified: no cluster interconnect, no InfiniBand/RDMA configuration, no
multi-machine `torch.distributed` launch, no shared filesystem. The DDP
snippet below is single-node — `mp.spawn` over the local devices, world
size equal to the local device count — and that is the only form this
guide supports.

## Verifying GPU Detection

After installing NVIDIA drivers, verify all GPUs are detected:

```bash
# List all NVIDIA GPUs
nvidia-smi -L

# Expected output — five cards, the one model the pinned table names:
# GPU 0: NVIDIA GeForce RTX 4060 Ti (UUID: GPU-...)
# GPU 1: NVIDIA GeForce RTX 4060 Ti (UUID: GPU-...)
# ... through GPU 4
```

A count short of five is the failure this check exists for, and `bmc-run --check`
reports it as `INDETERMINATE` rather than starting on the survivors.

## GPU Topology

Check how your GPUs are connected:

```bash
nvidia-smi topo -m
```

Example output from an NVLink machine — **not this one**, and the difference is
the point of running the command:

```
        GPU0  GPU1  NIC0  CPU Affinity
GPU0     X    NV18  SYS   0-31
GPU1    NV18   X    SYS   32-63
```

The pinned cards are PCIe GeForce, so expect `PIX`/`PXB`/`PHB`/`SYS` and no `NV#`
row. Whether peer-to-peer engages at all on GeForce is a driver property to
observe in this output, not to assume from a variable set in a config file.

| Symbol | Meaning |
|---|---|
| NV# | NVLink with # links |
| PIX | Same PCIe switch |
| PXB | Multiple PCIe switches |
| PHB | PCIe host bridge |
| SYS | System bus (QPI/UPI) |
| X | Self |

## NCCL Configuration

NCCL environment has one source: `/etc/conf.d/nccl` (installed by
`ai-stack/cuda/setup_cuda.sh`). Login shells get it from the one-line
`/etc/profile.d/nccl.sh` shim; the training service and the deploy script
source the conf.d file directly, because an OpenRC service is not a login
shell. Add a variable there only with a ledger row naming the measurement
that justifies it and the baseline it beat — a default is not a finding,
and a variable with no row is removed.

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
