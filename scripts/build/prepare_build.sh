#!/bin/bash
# BareCortex - Build Host Preparation
# Verifies the host toolchain and creates the LFS build tree.
# Run this BEFORE chapter 2 of the LFS (OpenRC Edition) book.

set -e

LFS="${LFS:-/mnt/barecortex}"
LFS_TGT="${LFS_TGT:-x86_64-lfs-linux-gnu}"

echo "=========================================="
echo "  BareCortex Build Host Preparation"
echo "=========================================="

# --- 1. Required host tools (LFS chapter 2.4) -------------------------------
MISSING=""
for TOOL in bash ar as ld gcc g++ make c++ sed grep gawk bison patch \
            perl python3 flex grep tar xz bzip2 diff find grep; do
    command -v "${TOOL}" > /dev/null 2>&1 || MISSING="${MISSING} ${TOOL}"
done

if [ -n "${MISSING}" ]; then
    echo "[ERROR] Missing host tools:${MISSING}"
    echo "Install them with your distro package manager and re-run."
    exit 1
fi
echo "[OK] Host toolchain present"

# --- 2. /bin/sh must be bash, not dash -------------------------------------
if [ "$(readlink -f /bin/sh)" != "$(command -v bash)" ]; then
    echo "[WARN] /bin/sh is not bash — some packages will fail to build."
fi

# --- 3. Build directory -------------------------------------------------------
echo "[INFO] Creating build tree at ${LFS}"
mkdir -p "${LFS}/sources" "${LFS}/lfs"
chmod a+wt "${LFS}/sources" "${LFS}/lfs"

# --- 4. Unprivileged build user ----------------------------------------------
if ! id -u lfs > /dev/null 2>&1; then
    echo "[INFO] Creating the 'lfs' build user"
    useradd -s /bin/bash -m lfs
    echo "lfs ALL=(root) NOPASSWD: SETENV, NOEXEC_ALL" > /etc/sudoers.d/lfs_build
    chmod 0440 /etc/sudoers.d/lfs_build
else
    echo "[INFO] User 'lfs' already exists"
fi

cat > /home/lfs/.bashrc << 'EOF'
export LFS_TGT=x86_64-lfs-linux-gnu
export LFS_MAKE=$(nproc)
alias lfs-chroot='sudo chroot "$LFS" /usr/bin/env -i \
    HOME=/root TERM="$TERM" PS1="[lfs] \u:\w\$ " \
    PATH=/bin:/usr/bin:/usr/local/bin:/opt/bin \
    BUILD_CC=gcc CC=gcc CXX=g++ /bin/bash --login'
EOF

echo ""
echo "=========================================="
echo "  Host ready."
echo "  Export LFS=${LFS} and continue with LFS chapter 3."
echo "=========================================="
