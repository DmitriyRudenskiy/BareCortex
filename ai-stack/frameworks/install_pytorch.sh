#!/bin/bash
# BareCortex - PyTorch Installer with CUDA Support
# Installs PyTorch optimized for the system's CUDA version

set -e

PROJECT_NAME="${1:-barecortex_project}"
VENV_BASE="/opt/ai"
VENV_DIR="${VENV_BASE}/${PROJECT_NAME}/venv"

echo "=========================================="
echo "  BareCortex PyTorch Installer"
echo "=========================================="

# Check venv exists
if [ ! -d "${VENV_DIR}" ]; then
    echo "[ERROR] Virtual environment not found: ${VENV_DIR}"
    echo "Run create_venv.sh first."
    exit 1
fi

# Detect CUDA version (12.8+ dropped version.txt in favour of version.json)
CUDA_HOME="/usr/local/cuda"
if [ -d "${CUDA_HOME}" ]; then
    CUDA_VERSION=$("${CUDA_HOME}/bin/nvcc" --version 2>/dev/null | sed -n 's/.*release \([0-9.]*\),.*/\1/p' | cut -d. -f1,2)
    [ -z "${CUDA_VERSION}" ] && CUDA_VERSION=$(awk -F'"' '/"version"/{print $4}' "${CUDA_HOME}/version.json" 2>/dev/null | cut -d. -f1,2)
    [ -z "${CUDA_VERSION}" ] && CUDA_VERSION=$(awk '{print $3}' "${CUDA_HOME}/version.txt" 2>/dev/null | cut -d. -f1,2)
    CUDA_VERSION="${CUDA_VERSION:-12.8}"
    echo "[INFO] Detected CUDA version: ${CUDA_VERSION}"
else
    echo "[ERROR] CUDA not found. Install CUDA Toolkit first."
    exit 1
fi

# Activate venv
source "${VENV_DIR}/bin/activate"

echo "[INFO] Installing PyTorch with CUDA ${CUDA_VERSION} support..."

# Install PyTorch with CUDA support
# The wheel index only carries specific CUDA point releases (cu118, cu121,
# cu124, cu126, cu128, cu130...). If the detected version has no index,
# pick the closest one from https://download.pytorch.org/whl/ manually.
pip install torch torchvision torchaudio \
    --index-url https://download.pytorch.org/whl/cu${CUDA_VERSION//./}

echo "[INFO] Verifying PyTorch installation..."

# Verify CUDA is available
python -c "
import torch
print(f'PyTorch version: {torch.__version__}')
print(f'CUDA available: {torch.cuda.is_available()}')
if torch.cuda.is_available():
    print(f'CUDA version: {torch.version.cuda}')
    print(f'GPU count: {torch.cuda.device_count()}')
    for i in range(torch.cuda.device_count()):
        print(f'  GPU {i}: {torch.cuda.get_device_name(i)}')
else:
    print('[WARN] CUDA not detected by PyTorch!')
"

echo ""
echo "=========================================="
echo "  PyTorch installation complete!"
echo "=========================================="
