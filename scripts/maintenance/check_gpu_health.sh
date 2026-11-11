#!/bin/bash
# BareCortex - GPU Health Check Script
# Monitors GPU health, temperatures, and error states

set -e

echo "=========================================="
echo "  BareCortex GPU Health Check"
echo "  $(date '+%Y-%m-%d %H:%M:%S')"
echo "=========================================="

# Check if nvidia-smi is available
if ! command -v nvidia-smi > /dev/null 2>&1; then
    echo "[ERROR] nvidia-smi not found. NVIDIA drivers may not be installed."
    exit 1
fi

echo ""
echo "--- GPU Overview ---"
nvidia-smi -L

echo ""
echo "--- Detailed Status ---"
nvidia-smi --query-gpu=index,name,driver_version,persistence_mode,temperature.gpu,utilization.gpu,memory.used,memory.total,power.draw,power.limit --format=csv,noheader

echo ""
echo "--- ECC Errors (if applicable) ---"
nvidia-smi --query-gpu=index,ecc.errors.corrected.volatile.total,ecc.errors.uncorrected.volatile.total --format=csv,noheader 2>/dev/null || echo "ECC not supported on this GPU"

echo ""
echo "--- PCIe Errors ---"
for i in $(nvidia-smi -L | awk '{print $2}' | tr -d ':'); do
    PCIE_ERR=$(nvidia-smi -i ${i} --query-gpu=pcie.replay.counter --format=csv,noheader 2>/dev/null || echo "N/A")
    echo "GPU ${i} - PCIe Replay Count: ${PCIE_ERR}"
done

echo ""
echo "--- Power State ---"
nvidia-smi --query-gpu=index,pstate,clocks.sm,clocks.mem --format=csv,noheader

echo ""
echo "--- GPU Topology ---"
nvidia-smi topo -m

echo ""
echo "=========================================="
echo "  Health check complete."
echo "=========================================="

# Alert if any GPU temperature exceeds threshold
TEMP_THRESHOLD=85
HOT_GPUS=$(nvidia-smi --query-gpu=index,temperature.gpu --format=csv,noheader | awk -F', ' -v t=${TEMP_THRESHOLD} '$2 > t {print $1}')

if [ -n "${HOT_GPUS}" ]; then
    echo ""
    echo "[⚠️  WARNING] GPU(s) ${HOT_GPUS} exceed ${TEMP_THRESHOLD}°C!"
    echo "[⚠️  WARNING] Consider increasing fan speed or reducing workload."
fi

# Superseded by utils/bmc-health, which exits non-zero on the condition it
# prints. This file stays as the parity incumbent — the replacement's evidence
# is this script and that binary run against the same canned driver output
# (utils/tests/parity.sh), and the language policy moves an incumbent rather
# than deleting it. See utils/rust-coverage.toml.
#
# Line numbers in this file are cited by openspec/changes/maintenance-and-updates
# (proposal, tasks 6.2/6.3), so notes go at the bottom, not the top.
