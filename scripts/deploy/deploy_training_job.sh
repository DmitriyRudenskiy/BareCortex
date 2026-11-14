#!/bin/bash
# BareCortex - Training Job Deployment Script
# Deploys and runs a training job with proper GPU allocation

set -e

SCRIPT_PATH="${1}"
GPU_IDS="${2:-0,1}"
PROJECT_NAME="${3:-barecortex_project}"

VENV_BASE="/opt/ai"
VENV_DIR="${VENV_BASE}/${PROJECT_NAME}/venv"
LOG_DIR="/var/log/ai-training"
TIMESTAMP=$(date '+%Y%m%d_%H%M%S')

echo "=========================================="
echo "  BareCortex Training Job Deployment"
echo "=========================================="

# Validate arguments
if [ -z "${SCRIPT_PATH}" ]; then
    echo "Usage: $0 <training_script.py> [GPU_IDS] [PROJECT_NAME]"
    echo ""
    echo "Examples:"
    echo "  $0 /opt/ai/my_project/train.py 0,1 my_project"
    echo "  $0 /opt/ai/my_project/train.py 0,1,2,3"
    exit 1
fi

if [ ! -f "${SCRIPT_PATH}" ]; then
    echo "[ERROR] Script not found: ${SCRIPT_PATH}"
    exit 1
fi

if [ ! -d "${VENV_DIR}" ]; then
    echo "[ERROR] Virtual environment not found: ${VENV_DIR}"
    echo "Create it first with: ai-stack/environments/create_venv.sh ${PROJECT_NAME}"
    exit 1
fi

if ! command -v tmux > /dev/null 2>&1; then
    echo "[ERROR] tmux not found. Install it (BLFS) or run the job by hand."
    exit 1
fi

# Create log directory
mkdir -p "${LOG_DIR}"
LOG_FILE="${LOG_DIR}/training_${TIMESTAMP}.log"

echo "[INFO] Training script: ${SCRIPT_PATH}"
echo "[INFO] GPU IDs: ${GPU_IDS}"
echo "[INFO] Python venv: ${VENV_DIR}"
echo "[INFO] Log file: ${LOG_FILE}"

# Set environment (NCCL from the single source, D8 — keep the three lines
# adjacent so only the file's assignments are exported)
export CUDA_VISIBLE_DEVICES="${GPU_IDS}"
set -a
[ -r /etc/conf.d/nccl ] && . /etc/conf.d/nccl
set +a

# Check GPU availability
GPU_COUNT=$(nvidia-smi -L | wc -l)
IFS=',' read -ra GPU_ARRAY <<< "${GPU_IDS}"
REQUESTED_GPUS=${#GPU_ARRAY[@]}

if [ ${REQUESTED_GPUS} -gt ${GPU_COUNT} ]; then
    echo "[ERROR] Requested ${REQUESTED_GPUS} GPUs, but only ${GPU_COUNT} available."
    exit 1
fi

echo "[INFO] GPUs available: ${GPU_COUNT}"
echo "[INFO] GPUs requested: ${REQUESTED_GPUS}"

# Run the training script in tmux for persistence
echo ""
echo "[INFO] Starting training in tmux session 'training_${TIMESTAMP}'..."
echo "[INFO] Attach with: tmux attach -t training_${TIMESTAMP}"
echo "[INFO] Detach with: Ctrl+B, then D"
echo ""

tmux new-session -d -s "training_${TIMESTAMP}" \
    "${VENV_DIR}/bin/python ${SCRIPT_PATH} 2>&1 | tee ${LOG_FILE}"

echo "[OK] Training job deployed!"
echo "[OK] Monitor with: tmux attach -t training_${TIMESTAMP}"
echo "[OK] View logs: tail -f ${LOG_FILE}"

# Superseded by utils/bmc-run: the NCCL environment is read from the single
# source instead of exported inline, the device list is validated index by
# index, the environment reaches the job even when a tmux server is already
# running, and every run is recorded. This file stays as the parity incumbent
# (utils/tests/parity.sh). See utils/rust-coverage.toml.
#
# Line numbers here are cited by openspec/changes/userspace-and-ml-stack and
# honest-scope-and-config-generation, so notes go at the bottom, not the top.
