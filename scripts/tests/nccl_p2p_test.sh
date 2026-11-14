#!/bin/sh
# nccl_p2p_test.sh — verify NCCL peer-to-peer on this machine (docs/BOOTLOADER.md).
#
# usage: sh scripts/tests/nccl_p2p_test.sh
#
# Runs an NCCL allreduce across every visible GPU and reports pass/fail.
# MASTER_ADDR and MASTER_PORT are set here, which is what the prose
# versions of this test usually forget. Exit codes:
#   0  allreduce passed on every visible GPU
#   1  the test ran and failed
#   2  the test could not run here (no torch, fewer than two GPUs)
set -u

NGPU=$(nvidia-smi -L 2>/dev/null | grep -c '^GPU') || NGPU=0
if [ "$NGPU" -lt 2 ]; then
    echo "nccl_p2p_test: need at least 2 GPUs, found ${NGPU} — not run" >&2
    exit 2
fi

if ! command -v python3 >/dev/null 2>&1 || ! python3 -c 'import torch' 2>/dev/null; then
    echo "nccl_p2p_test: python3 with torch is required — not run" >&2
    exit 2
fi

export MASTER_ADDR=127.0.0.1
export MASTER_PORT=29501

cat > /tmp/nccl_p2p_worker.py <<'EOF'
import os
import sys

import torch
import torch.distributed as dist

ngpu = int(sys.argv[1])
rank = int(os.environ["RANK"])
local_rank = int(os.environ["LOCAL_RANK"])
world_size = int(os.environ["WORLD_SIZE"])

dist.init_process_group("nccl", rank=rank, world_size=world_size)
device = torch.device(f"cuda:{local_rank}")
tensor = torch.ones(1 << 24, device=device) * (rank + 1)
dist.all_reduce(tensor, op=dist.ReduceOp.SUM)
expected = world_size * (world_size + 1) / 2
ok = torch.allclose(tensor, torch.full_like(tensor, expected))
dist.destroy_process_group()
if rank == 0:
    print(f"nccl_p2p_test: allreduce across {world_size} GPUs: {'PASS' if ok else 'FAIL'}")
sys.exit(0 if ok else 1)
EOF

exec python3 -m torch.distributed.run --nproc_per_node="$NGPU" /tmp/nccl_p2p_worker.py
