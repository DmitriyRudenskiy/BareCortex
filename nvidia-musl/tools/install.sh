#!/bin/sh
# install.sh — package the lite nvidia-musl recipe (recipes/nvidia-smi.md).
# Every file it creates is appended to $PREFIX/manifest.tsv; anything it
# replaces outside the prefix is moved to $PREFIX/backup/ first.
# uninstall.sh reads the manifest and reverses everything.
#
# usage: install.sh --payload DIR [--prefix DIR] [--syslib DIR]
#                   [--nvcompat FILE] [--map FILE]
#   --payload  dir with nvidia-smi, libnvidia-ml.so.<ver>, libcuda.so.<ver>
#              (an extracted .run, or a flat dir of the host's own files —
#              version MUST equal the loaded kernel modules)
#   --prefix   install root            (default /opt/nvidia-musl-exp)
#   --syslib   dir nvidia-smi scans for NVML (default /usr/lib)
#   --nvcompat libnvcompat.so.0        (default /opt/nvidia-musl-exp-src/libnvcompat.so.0)
#   --map      libnvcompat.map         (default alongside it)
set -eu

PAYLOAD= PREFIX=/opt/nvidia-musl-exp SYSLIB=/usr/lib
NVCOMPAT=/opt/nvidia-musl-exp-src/libnvcompat.so.0
NVCMAP=/opt/nvidia-musl-exp-src/libnvcompat.map
while [ $# -gt 0 ]; do
	case $1 in
	--payload) PAYLOAD=$2; shift ;; --prefix) PREFIX=$2; shift ;;
	--syslib) SYSLIB=$2; shift ;; --nvcompat) NVCOMPAT=$2; shift ;;
	--map) NVCMAP=$2; shift ;;
	*) echo "unknown arg: $1" >&2; exit 64 ;;
	esac; shift
done
[ -n "$PAYLOAD" ] || { echo "need --payload" >&2; exit 64; }
MANIFEST=$PREFIX/manifest.tsv

step() { echo "== $*"; }
note() { printf '%s\t%s\t%s\n' "$1" "$2" "${3:-}" >>"$MANIFEST"; }
place() { # place <src> <dest> : create dest, backing up whatever is there
	if [ -e "$2" ] || [ -L "$2" ]; then
		mkdir -p "$PREFIX/backup"
		mv "$2" "$PREFIX/backup/$(basename "$2").$(date +%s)"
		step "backed up existing $2 -> $PREFIX/backup/"
	fi
	ln -sfn "$1" "$2" 2>/dev/null || cp -f "$1" "$2"
	note link "$2" "$1"
}

# --- preconditions -------------------------------------------------------------
command -v patchelf >/dev/null || { echo "need patchelf" >&2; exit 1; }
[ -f "$NVCOMPAT" ] && [ -f "$NVCMAP" ] || { echo "need --nvcompat and --map" >&2; exit 1; }
libs=$(cd "$PAYLOAD" && ls libnvidia-ml.so.[0-9]* libcuda.so.[0-9]* 2>/dev/null |
	while read -r f; do [ -f "$f" ] && [ ! -L "$f" ] && echo "$f"; done)
[ -n "$libs" ] || { echo "no libnvidia-ml/libcuda real files in $PAYLOAD" >&2; exit 1; }
[ -f "$PAYLOAD/nvidia-smi" ] || { echo "no nvidia-smi in $PAYLOAD" >&2; exit 1; }
# gcompat must provide the stub interp at BOTH paths and a libc.so alias
[ -e /lib/ld-linux-x86-64.so.2 ] || { echo "gcompat not installed (/lib/ld-linux-x86-64.so.2)" >&2; exit 1; }
mkdir -p "$PREFIX" && : >"$MANIFEST"

# --- prefix ---------------------------------------------------------------------
mkdir -p "$PREFIX/bin" "$PREFIX/lib"
note dir "$PREFIX" ; note dir "$PREFIX/bin" ; note dir "$PREFIX/lib"

step "installing nvidia-smi (binary: no patchelf, gcompat stub is the interp)"
cp "$PAYLOAD/nvidia-smi" "$PREFIX/bin/nvidia-smi" ; note file "$PREFIX/bin/nvidia-smi"

step "installing libnvcompat.so.0 -> $PREFIX/lib"
cp "$NVCOMPAT" "$PREFIX/lib/libnvcompat.so.0" ; note file "$PREFIX/lib/libnvcompat.so.0"

for f in $libs; do
	step "sanitizing $f (PR #6531 recipe) -> $PREFIX/lib"
	cp "$PAYLOAD/$f" "$PREFIX/lib/$f"
	patchelf --remove-needed ld-linux-x86-64.so.2 \
		--remove-needed libdl.so.2 --remove-needed libm.so.6 \
		--remove-needed libpthread.so.0 --remove-needed librt.so.1 \
		--replace-needed libc.so.6 libc.so \
		--add-needed libnvcompat.so.0 \
		--rename-dynamic-symbols "$NVCMAP" "$PREFIX/lib/$f"
	note file "$PREFIX/lib/$f"
	soname=$(patchelf --print-soname "$PREFIX/lib/$f")
	ln -sfn "$f" "$PREFIX/lib/$soname" ; note link "$PREFIX/lib/$soname" "$f"
done

# --- system-visible pieces (manifested + backed up) ------------------------------
# nvidia-smi scans absolute paths for NVML and ignores LD_LIBRARY_PATH; the CUDA
# runtime dlopen()s libcuda.so.1 by soname. libnvcompat must be on the search path.
[ -e /lib/libc.so ] || { ln -s libgcompat.so.0 /lib/libc.so ; note link /lib/libc.so libgcompat.so.0 ; }
[ -e /lib64/ld-linux-x86-64.so.2 ] || {
	mkdir -p /lib64 ; ln -s ../lib/ld-linux-x86-64.so.2 /lib64/ld-linux-x86-64.so.2
	note link /lib64/ld-linux-x86-64.so.2 /lib/ld-linux-x86-64.so.2 ; }
for f in $libs; do
	soname=$(basename "$f" | sed 's/\.so\.[0-9].*/.so.1/')
	place "$PREFIX/lib/$f" "$SYSLIB/$soname"
done
place "$PREFIX/lib/libnvcompat.so.0" "$SYSLIB/libnvcompat.so.0"

step "done. manifest: $MANIFEST ($(grep -c . "$MANIFEST") entries)"
