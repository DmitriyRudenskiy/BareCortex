#!/bin/sh
# What does the libnvcompat shim actually contribute? (operator guardrail 3)
#
# Four variants of the SAME libcuda copy, probed with no LD_PRELOAD, so only the
# patchelf operations differ. Run after lite-env-setup.sh + dlopen-harness.sh.
#
# Expected (measured 14.11.2026, report/07):
#   A nothing            LOADER_FAIL  gnu_get_libc_version: symbol not found
#   B +add-needed shim   LOADER_OK
#   C removes+libc.so    LOADER_FAIL  (same symbol — the rename activates nothing)
#   D rename + shim      LOADER_OK    (= the PR #6531 recipe)
#
# usage: ablate-shim.sh [path/to/libcuda.so.X]
set -u
NV=${NV:-$HOME/.cache/nv580/ex/NVIDIA-Linux-x86_64-580.196.07}
ROOTFS=${ROOTFS:-$HOME/lite/edge}
PR=${PR:-$HOME/lite/pr}
SRC=${1:-$(ls "$NV"/libcuda.so.* 2>/dev/null | grep -v '\.so\.1$' | head -1)}
ABL=$HOME/lite/abl
[ -f "$SRC" ] || { echo "no libcuda at $SRC" >&2; exit 1; }
mkdir -p "$ABL" "$ROOTFS/abl" "$ROOTFS/probe"

sb() {
	bwrap --ro-bind "$ROOTFS" / --ro-bind "$PR" /probe --bind "$ABL" /abl \
		--dev /dev --proc /proc --tmpfs /tmp --unshare-user --uid 0 \
		--die-with-parent env -i PATH=/usr/bin:/bin LD_LIBRARY_PATH=/abl:/probe \
		timeout 90 "$@" 2>&1
}

for v in A B C D; do
	cp "$SRC" "$ABL/$v.so"
	[ "$v" = B ] && sb patchelf --add-needed libnvcompat.so.0 /abl/$v.so
	[ "$v" = C ] && sb patchelf --remove-needed ld-linux-x86-64.so.2 \
		--remove-needed libdl.so.2 --remove-needed libm.so.6 \
		--remove-needed libpthread.so.0 --remove-needed librt.so.1 \
		--replace-needed libc.so.6 libc.so /abl/$v.so
	[ "$v" = D ] && sb patchelf --replace-needed libc.so.6 libc.so \
		--add-needed libnvcompat.so.0 \
		--rename-dynamic-symbols /probe/libnvcompat.map /abl/$v.so
	printf '%s %-26s %s\n' "$v" \
		"$(case $v in A) echo nothing;; B) echo '+add-needed shim';; \
		 C) echo 'removes + libc.so rename';; D) echo 'PR recipe (rename+shim)';; esac)" \
		"$(sb /usr/local/bin/harness /abl/$v.so cuInit | tr '\n' ' ')"
done
