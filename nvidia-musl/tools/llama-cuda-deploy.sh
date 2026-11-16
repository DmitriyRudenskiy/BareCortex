#!/bin/sh
# Деплой CUDA-стека llama.cpp в musl-rootfs (edge): glibc-артефакты + алиасинг.
#
# usage: sh llama-cuda-deploy.sh <edge-rootfs> <llama-build-bin-dir> [cuda-lib-dir]
#   <llama-build-bin-dir> — каталог с libggml-cuda.so.1.0.0 (host-сборка,
#     flags из reports/15-llama-cuda-musl.md; GGML_BACKEND_DL=ON обязателен)
#   [cuda-lib-dir]        — по умолчанию /usr/local/cuda-13.2/lib64
#
# Исполнение в musl: патченный лоадер + gcompat + LD_PRELOAD=libnvcompat.so.0.
# libggml-cuda.so тянет libggml-base.so.1 по SONAME — биндится к musl-копии,
# уже загруженной llama-бинарником (C-ABI, один commit).
set -eu
E=${1:?edge rootfs}
BIN=${2:?build bin dir with libggml-cuda.so.1.0.0}
C=${3:-/usr/local/cuda-13.2/lib64}
MAP="$HOME/lite/repo/shim/libnvcompat.map"
[ -f "$E/usr/lib/libnvcompat.so.0" ] || { echo "нет $E/usr/lib/libnvcompat.so.0 (сначала nv-lite-install)"; exit 1; }
[ -f "$MAP" ] || { echo "нет map-файла $MAP"; exit 1; }

alias_one() { # $1 = файл в $E/usr/lib
	if ! readelf -d "$1" | grep -q libnvcompat; then
		patchelf --add-needed libnvcompat.so.0 --rename-dynamic-symbols "$MAP" "$1"
		echo "aliased $(basename "$1")"
	fi
}

# bwrap: patchelf/readelf живут в edge
bwrap --bind "$E" / --proc /proc --dev /dev --unshare-user --uid 0 \
	--ro-bind "$BIN/libggml-cuda.so.1.0.0" /src/libggml-cuda.so \
	--ro-bind "$C" /cudadir --ro-bind "$MAP" /map \
	sh -c '
set -e
L=/usr/lib
cp /src/libggml-cuda.so $L/libggml-cuda.so.1.0.0
ln -sf libggml-cuda.so.1.0.0 $L/libggml-cuda.so.1
ln -sf libggml-cuda.so.1     $L/libggml-cuda.so
alias_one() {
	readelf -d "$1" | grep -q libnvcompat || \
		patchelf --add-needed libnvcompat.so.0 --rename-dynamic-symbols /map "$1"
	echo "aliased $(basename "$1")"
}
alias_one $L/libggml-cuda.so.1.0.0
for f in libcublas.so.13.4.0.1 libcublasLt.so.13.4.0.1 libcudart.so.13.2.75 \
         libnvrtc.so.13.2.78 libnvrtc-builtins.so.13.2.78; do
	if [ -e /cudadir/$f ]; then cp -n /cudadir/$f $L/; else echo "skip $f (нет в cudadir)"; fi
done
for n in "libcublas.so.13 libcublas.so.13.4.0.1" \
         "libcublasLt.so.13 libcublasLt.so.13.4.0.1" \
         "libcudart.so.13 libcudart.so.13.2.75" \
         "libnvrtc.so.13 libnvrtc.so.13.2.78" \
         "libnvrtc-builtins.so.13.2 libnvrtc-builtins.so.13.2.78"; do
	set -- $n; ln -sf "$2" $L/$1
done
for f in libcublas.so.13.4.0.1 libcublasLt.so.13.4.0.1 libcudart.so.13.2.75 \
         libnvrtc.so.13.2.78 libnvrtc-builtins.so.13.2.78; do
	if [ -e $L/$f ]; then alias_one $L/$f; fi
done
'
# libnccl: не в toolkit-каталоге, берём с хоста
if [ -e /usr/lib/x86_64-linux-gnu/libnccl.so.2 ]; then
	cp -n /usr/lib/x86_64-linux-gnu/libnccl.so.2 "$E/usr/lib/libnccl.so.2" 2>/dev/null || true
	[ -e "$E/usr/lib/libnccl.so.2" ] && bwrap --bind "$E" / --proc /proc --dev /dev \
		--unshare-user --uid 0 --ro-bind "$MAP" /map sh -c '
		readelf -d /usr/lib/libnccl.so.2 | grep -q libnvcompat || \
			patchelf --add-needed libnvcompat.so.0 --rename-dynamic-symbols /map /usr/lib/libnccl.so.2'
	echo "nccl ok"
fi
echo "DEPLOYED into $E"
