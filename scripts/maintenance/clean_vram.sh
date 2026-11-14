#!/bin/bash
# BareCortex - VRAM Cleanup Script
# Kills orphaned processes holding GPU memory

set -e

echo "=========================================="
echo "  BareCortex VRAM Cleanup"
echo "=========================================="

# Show current VRAM usage
echo ""
echo "--- Current VRAM Usage ---"
nvidia-smi --query-compute-apps=pid,name,used_memory --format=csv,noheader

# Find processes using GPU
GPU_PIDS=$(nvidia-smi --query-compute-apps=pid --format=csv,noheader | sort -u)

if [ -z "${GPU_PIDS}" ]; then
    echo "[INFO] No processes currently using GPU memory."
    exit 0
fi

echo ""
echo "[INFO] Found $(echo ${GPU_PIDS} | wc -w) process(es) using GPU:"
for PID in ${GPU_PIDS}; do
    PROC_NAME=$(ps -p ${PID} -o comm= 2>/dev/null || echo "unknown")
    echo "  PID ${PID}: ${PROC_NAME}"
done

echo ""
read -p "Kill all GPU processes? [y/N] " -r REPLY || REPLY=""
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "[INFO] Aborted."
    exit 0
fi

# Kill processes
for PID in ${GPU_PIDS}; do
    echo "[INFO] Killing PID ${PID}..."
    kill -15 ${PID} 2>/dev/null || true
done

sleep 3

# Force kill remaining processes
REMAINING=$(nvidia-smi --query-compute-apps=pid --format=csv,noheader | sort -u)
if [ -n "${REMAINING}" ]; then
    echo "[WARN] Force killing remaining processes..."
    for PID in ${REMAINING}; do
        kill -9 ${PID} 2>/dev/null || true
    done
fi

# Reset GPUs if needed
echo ""
read -p "Reset GPU state? (clears ECC errors, resets clocks) [y/N] " -r REPLY || REPLY=""
if [[ $REPLY =~ ^[Yy]$ ]]; then
    for i in $(nvidia-smi -L | awk '{print $2}' | tr -d ':'); do
        echo "[INFO] Resetting GPU ${i}..."
        nvidia-smi -i ${i} -r 2>/dev/null || echo "[WARN] GPU ${i} reset failed (may be in use)"
    done
fi

# Verify before reporting: a kill that left a process behind is not a
# cleanup (D11) — success means no compute process still holds VRAM.
STILL=$(nvidia-smi --query-compute-apps=pid --format=csv,noheader 2>/dev/null | sort -u)
if [ -n "${STILL}" ]; then
    echo "[ERROR] Still holding VRAM: $(echo ${STILL} | tr '\n' ' ')"
    exit 1
fi

echo ""
echo "[OK] VRAM cleanup complete: no compute process holds GPU memory."
nvidia-smi

# Superseded by utils/bmc-vram: dry run by default, targets named on the
# command line, another user's process refused rather than offered, and a reset
# refused while a device is held. This file stays as the parity incumbent
# (utils/tests/parity.sh); the language policy moves an incumbent instead of
# deleting it. See utils/rust-coverage.toml.
#
# Line numbers here are cited by openspec/changes/maintenance-and-updates
# (proposal, tasks 7.1-7.3), so notes go at the bottom, not the top.
