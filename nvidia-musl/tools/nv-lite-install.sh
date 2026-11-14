#!/bin/sh
# nv-lite-install.sh — NVIDIA 580 headless userspace on stock musl (Alpine/Chimera).
#
#   nv-lite-install.sh install [payload-dir]
#   nv-lite-install.sh verify
#   nv-lite-install.sh uninstall
#
# Layout (operator D2, report/06/07): real files + the shim in $PREFIX, runtime
# names as symlinks in /usr/lib. System musl is untouched.
#
# Why it is shaped this way (measured, report/07):
#  * gcompat does NOT come in via DT_NEEDED — musl maps libc.so.6 & co to itself.
#    The one mandatory op per library is `--add-needed libnvcompat.so.0`, which
#    pulls libgcompat.so.0; `--rename-dynamic-symbols` fixes recursive mutexes.
#  * Binaries need no patching: their PT_INTERP is gcompat's stub, which re-execs
#    ld-musl with --preload /lib/libgcompat.so.0.
#  * nvidia-smi finds NVML only through absolute paths (/usr/lib64/libnvidia-ml.so.1,
#    /usr/lib/libnvidia-ml.so.1, /usr/lib{64,}/nvidia/…) — LD_LIBRARY_PATH is ignored,
#    and the .run payload ships no SONAME symlinks — hence the /usr/lib symlinks.
#  * No ld.so.conf.d: measured, Alpine ldconfig did not pick up the conf.d dir, and the
#    only names ever dlopen'ed are libcuda.so.1 / libnvidia-ml.so.1 / libnvcompat.so.0.
#  * libnvidia-tls is deliberately excluded: initial-exec TLS needs patched musl
#    (cports patch 091) and nothing in this set links it.
#
# Requires: root; apk gcompat patchelf; gcc (to build the shim) or --shim=/path/to/prebuilt
# Payload: the extracted .run dir (NVIDIA-Linux-x86_64-580.196.07/ with bin/ lib/ flat files)

set -eu
PREFIX=/opt/nvidia-musl-exp
DRIVER=580.196.07
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
LIBS="
libcuda.so.$DRIVER
libnvidia-ml.so.$DRIVER
libnvidia-opencl.so.$DRIVER
libnvidia-ptxjitcompiler.so.$DRIVER
libnvidia-glsi.so.$DRIVER
"
BINARIES="
nvidia-smi
nvidia-modprobe
nvidia-persistenced
"
PREBUILT_SHIM=
# public SONAME names (readelf-verified 14.11) -> real file in $PREFIX/lib;
# libnvidia-glsi has no public alias (its SONAME is the versioned name)
EXPOSE="
libcuda.so.1 libcuda.so.$DRIVER
libnvidia-ml.so.1 libnvidia-ml.so.$DRIVER
libnvidia-ptxjitcompiler.so.1 libnvidia-ptxjitcompiler.so.$DRIVER
libnvidia-opencl.so.1 libnvidia-opencl.so.$DRIVER
libnvcompat.so.0 libnvcompat.so.0
"
SYMLINKS="/usr/lib/libcuda.so.1 /usr/lib/libnvidia-ml.so.1 /usr/lib/libnvidia-ptxjitcompiler.so.1 /usr/lib/libnvidia-opencl.so.1 /usr/lib/libnvcompat.so.0"

payload_find() {
	if [ "$#" -ge 1 ] && [ -n "${1:-}" ]; then echo "$1"; return; fi
	for d in "$HOME"/.cache/nv580/ex/NVIDIA-Linux-x86_64-$DRIVER \
	         /tmp/NVIDIA-Linux-x86_64-$DRIVER /root/NVIDIA-Linux-x86_64-$DRIVER; do
		[ -f "$d/libcuda.so.$DRIVER" ] && { echo "$d"; return; }
	done
	echo "payload dir not found; pass it as: $0 install /path/to/NVIDIA-Linux-x86_64-$DRIVER" >&2
	exit 2
}

need_root() { [ "$(id -u)" = 0 ] || { echo "must run as root" >&2; exit 1; }; }
have() { command -v "$1" >/dev/null 2>&1 || { echo "missing: $1" >&2; exit 1; }; }
needed_of() { readelf -d "$1" 2>/dev/null | sed -n 's/.*Needed:\s*\[\s*//; s/\].*//p'; }
sha_of() { sha256sum "$1" | cut -d' ' -f1; }

cmd_install() {
	need_root
	have patchelf; have gcc; have readelf; have sha256sum
	[ -f /lib/libgcompat.so.0 ] || { echo "gcompat not installed (apk add gcompat)" >&2; exit 1; }
	PAYLOAD=$(payload_find "${1:-}")
	for f in $LIBS $BINARIES; do
		[ -f "$PAYLOAD/$f" ] || { echo "payload missing $f" >&2; exit 1; }
	done
	mkdir -p "$PREFIX/lib" "$PREFIX/share"
	[ -n "$PREBUILT_SHIM" ] || {
		gcc -shared -fPIC -o "$PREFIX/lib/libnvcompat.so.0" "$HERE/shim/libnvcompat.c" \
			-Wl,-soname,libnvcompat.so.0 -Wl,--no-as-needed -l:libgcompat.so.0
	}
	# manifest: file  sha_before  sha_after  op  needed_before  needed_after
	MAN="$PREFIX/share/manifest.tsv"
	printf 'file\tsha256_before\tsha256_after\top\tneeded_before\tneeded_after\n' > "$MAN"
	install_one() { # $1 file, $2 op, $3 patchelf cmd (or empty)
		local b a
		b=$(sha_of "$PAYLOAD/$1")
		cp -p "$PAYLOAD/$1" "$PREFIX/lib/$1"
		[ -n "${3:-}" ] && sh -c "$3"
		a=$(sha_of "$PREFIX/lib/$1")
		printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$1" "$b" "$a" "$2" \
			"$(needed_of "$PAYLOAD/$1" | tr '\n' ';')" \
			"$(needed_of "$PREFIX/lib/$1" | tr '\n' ';')" >> "$MAN"
	}
	for f in $LIBS; do
		install_one "$f" "add-needed+rename" \
			"patchelf --add-needed libnvcompat.so.0 --rename-dynamic-symbols $HERE/shim/libnvcompat.map $PREFIX/lib/$f"
	done
	for f in $BINARIES; do install_one "$f" "none" ""; done
	while read -r name tgt; do
		[ -n "$name" ] && ln -sfn "$PREFIX/lib/$tgt" "/usr/lib/$name"
	done <<EOF2
$EXPOSE
EOF2
	cmd_verify
}

cmd_uninstall() {
	need_root
	for l in $SYMLINKS; do
		case "$(readlink "$l" 2>/dev/null)" in "$PREFIX"/*) rm -f "$l";; esac
	done
	rm -rf "$PREFIX"
	echo "uninstalled"
}

cmd_verify() {
	local bad=0
	for l in $SYMLINKS; do
		[ -e "$l" ] || { echo "MISSING $l" >&2; bad=1; }
	done
	if [ -f "$PREFIX/share/manifest.tsv" ]; then
		while IFS=$'\t' read -r f b a op nb na; do
			[ "$f" = file ] && continue
			[ "$a" = "$(sha_of "$PREFIX/lib/$f")" ] || { echo "SHA MISMATCH $f" >&2; bad=1; }
		done < "$PREFIX/share/manifest.tsv"
	fi
	out=$("$PREFIX/lib/nvidia-smi" 2>&1) || true
	if printf '%s' "$out" | grep -q "couldn't find libnvidia-ml.so"; then
		echo "FAIL: nvidia-smi still cannot find libnvidia-ml.so" >&2; bad=1
	else
		echo "nvidia-smi: NVML found — $(printf '%s' "$out" | head -1)"
	fi
	[ "$bad" = 0 ] || exit 1
	echo "verify OK"
}

for opt in "$@"; do case $opt in --shim=*) PREBUILT_SHIM=${opt#--shim=};; esac; done
case ${1:-} in
	install)   shift; cmd_install "${1:-}";;
	verify)    cmd_verify;;
	uninstall) cmd_uninstall;;
	*) echo "usage: $0 install [payload-dir] | verify | uninstall" >&2; exit 2;;
esac
