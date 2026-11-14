#!/bin/sh
# 2.3 (gpu): run nvidia-smi + cuInit under stock Alpine musl (lite env: musl
# 1.2.6-r6 + gcompat 1.1.0-r4 + libnvcompat shim) against the host's LOADED
# kernel modules. No root, no host-system changes; everything under $G.
#
# Two passes, both meaningful:
#   pinned  580.196.07 from the extracted .run -> expect a version-mismatch
#           error reported by the DRIVER (musl stack reached the kernel
#           interface; the loader is not the failing layer)
#   host    580.159.03 copied from the host's installed userspace, exact
#           match to the loaded modules -> expect real devices (the PASS bar)
#
# usage: run-gpu23.sh [pinned|host|all]   (default: all)
# env: LITE (lite env from lite-env-setup.sh), NV (extracted 580.196.07 dir),
#      HOSTLIB (host libcuda/libnvidia-ml dir), HOSTBIN (host nvidia-smi)
set -eu

LITE=${LITE:-$HOME/lite}
NV=${NV:-$HOME/.cache/nv580/ex/NVIDIA-Linux-x86_64-580.196.07}
HOSTLIB=${HOSTLIB:-/usr/lib/x86_64-linux-gnu}
HOSTBIN=${HOSTBIN:-/usr/bin/nvidia-smi}
ROOTFS=$LITE/edge
G=$LITE/gpu23
MODE=${1:-all}

[ -x "$ROOTFS/usr/local/bin/harness" ] || { echo "run lite-env-setup.sh first" >&2; exit 1; }
[ -d "$NV" ] || { echo "no extracted .run at $NV" >&2; exit 1; }
mkdir -p "$G"

# gcompat's stub loader must answer at BOTH interp paths; the shim must sit in
# the default search path (report/07 constraints).
[ -e "$ROOTFS/lib/libc.so" ] || ln -s libgcompat.so.0 "$ROOTFS/lib/libc.so"
[ -e "$ROOTFS/lib64/ld-linux-x86-64.so.2" ] || { mkdir -p "$ROOTFS/lib64";
	ln -s ../lib/ld-linux-x86-64.so.2 "$ROOTFS/lib64/ld-linux-x86-64.so.2"; }

# --- the sandbox: real /dev (nvidia nodes are 0666), /sys for NVML's PCI walk --
PRE=""
sb() {
	bwrap --bind "$ROOTFS" / --dev-bind /dev /dev --ro-bind /sys /sys \
		--proc /proc --tmpfs /tmp --unshare-user --uid 0 --die-with-parent \
		--ro-bind "$NV" /nv --bind "$G" /g \
		env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin $PRE timeout 90 "$@"
}

echo "== host driver: $(sed -n 1p /proc/driver/nvidia/version)"

# ============ PASS 1: pinned 580.196.07 vs modules 580.159.03 =================
if [ "$MODE" != host ]; then
	echo
	echo "== PASS 1: pinned 580.196.07 (expect a DRIVER-reported version mismatch)"
	# nvidia-smi finds NVML by absolute paths only (report/07); point it at the
	# pinned NVML. Unsanitized files need gcompat preloaded (report/07).
	ln -sf /nv/libnvidia-ml.so.580.196.07 "$ROOTFS/usr/lib/libnvidia-ml.so.1"
	echo "-- nvidia-smi:"
	PRE=LD_PRELOAD=/lib/libgcompat.so.0 sb /nv/nvidia-smi \
		|| echo "(exit $? — read the message above)"
	echo "-- libcuda cuInit:"
	PRE=LD_PRELOAD=/lib/libgcompat.so.0 sb /usr/local/bin/harness \
		/nv/libcuda.so.580.196.07 cuInit --calli0 cuInit \
		|| echo "(exit $?)"
	PRE=""
fi

# ============ PASS 2: host 580.159.03 — exact match, the PASS bar =============
if [ "$MODE" != pinned ]; then
	echo
	echo "== PASS 2: host 580.159.03 (exact match to loaded modules)"
	mkdir -p "$G/host"
	cp -f "$LITE/pr/libnvcompat.map" "$G/"
	cp -f "$HOSTBIN" "$G/host/nvidia-smi"
	cp -f "$HOSTLIB"/libnvidia-ml.so.580.159.03 "$G/host/"
	cp -f "$HOSTLIB"/libcuda.so.580.159.03 "$G/host/"
	echo "-- sanitizing host copies with the PR #6531 recipe"
	for b in libnvidia-ml.so.580.159.03 libcuda.so.580.159.03; do
		bwrap --bind "$ROOTFS" / --proc /proc --dev /dev --tmpfs /tmp \
			--bind "$G" /g --unshare-user --uid 0 --die-with-parent \
			env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin \
			patchelf --remove-needed ld-linux-x86-64.so.2 \
				--remove-needed libdl.so.2 --remove-needed libm.so.6 \
				--remove-needed libpthread.so.0 --remove-needed librt.so.1 \
				--replace-needed libc.so.6 libc.so \
				--add-needed libnvcompat.so.0 \
				--rename-dynamic-symbols /g/libnvcompat.map \
				"/g/host/$b"
	done
	cp -f "$LITE/pr/libnvcompat.so.0" "$ROOTFS/usr/lib/libnvcompat.so.0"
	cp -f "$G/host/libnvidia-ml.so.580.159.03" "$ROOTFS/usr/lib/"
	ln -sf libnvidia-ml.so.580.159.03 "$ROOTFS/usr/lib/libnvidia-ml.so.1"
	cp -f "$G/host/libcuda.so.580.159.03" "$ROOTFS/usr/lib/"
	ln -sf libcuda.so.580.159.03 "$ROOTFS/usr/lib/libcuda.so.1"

	echo "-- nvidia-smi (sanitized NVML at /usr/lib/libnvidia-ml.so.1):"
	sb /g/host/nvidia-smi
	echo "-- libcuda cuInit:"
	sb /usr/local/bin/harness /usr/lib/libcuda.so.1 cuInit --calli0 cuInit
	echo "-- NVML nvmlInit_v2:"
	sb /usr/local/bin/harness /usr/lib/libnvidia-ml.so.1 \
		nvmlInit_v2 nvmlDeviceGetName --call0 nvmlInit_v2
fi
