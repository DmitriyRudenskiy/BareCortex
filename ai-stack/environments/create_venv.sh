#!/bin/bash
# BareCortex - Python Virtual Environment Creator
# Creates isolated Python environments for AI projects

set -e

PROJECT_NAME="${1:-barecortex_project}"
VENV_BASE="/opt/ai"
PYTHON_BIN="${PYTHON_BIN:-python3}"

echo "=========================================="
echo "  BareCortex Python Environment Setup"
echo "=========================================="
echo "[INFO] Project name: ${PROJECT_NAME}"

# Check Python availability
if ! command -v ${PYTHON_BIN} > /dev/null 2>&1; then
    echo "[ERROR] Python not found. Install Python 3.12+ via BLFS."
    exit 1
fi

PYTHON_VERSION=$(${PYTHON_BIN} --version 2>&1 | awk '{print $2}')
echo "[INFO] Python version: ${PYTHON_VERSION}"

# Create project directory structure
PROJECT_DIR="${VENV_BASE}/${PROJECT_NAME}"
VENV_DIR="${PROJECT_DIR}/venv"
DATA_DIR="${PROJECT_DIR}/data"
MODELS_DIR="${PROJECT_DIR}/models"
LOGS_DIR="${PROJECT_DIR}/logs"

echo "[INFO] Creating project directory: ${PROJECT_DIR}"
mkdir -p "${VENV_DIR}" "${DATA_DIR}" "${MODELS_DIR}" "${LOGS_DIR}"

# Create virtual environment
echo "[INFO] Creating virtual environment..."
${PYTHON_BIN} -m venv "${VENV_DIR}"

# Activate and upgrade pip
echo "[INFO] Upgrading pip and installing base packages..."
source "${VENV_DIR}/bin/activate"

pip install --upgrade pip setuptools wheel
pip install --upgrade numpy scipy

# Install common ML utilities
pip install tensorboard matplotlib tqdm pyyaml

echo ""
echo "=========================================="
echo "  Environment created successfully!"
echo ""
echo "  Project:  ${PROJECT_DIR}"
echo "  Venv:     ${VENV_DIR}"
echo "  Data:     ${DATA_DIR}"
echo "  Models:   ${MODELS_DIR}"
echo "  Logs:     ${LOGS_DIR}"
echo ""
echo "  Activate with:"
echo "    source ${VENV_DIR}/bin/activate"
echo "=========================================="
