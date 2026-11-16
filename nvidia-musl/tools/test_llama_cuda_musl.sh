#!/bin/sh
# llama.cpp + CUDA на musl: smoke-тест
#
# Схема: musl-llama (Alpine edge) + glibc-артефакты CUDA (libggml-cuda.so,
# libcublas/Lt, libcudart, libnvrtc, libnccl) — запускаются в musl-процессе
# через патченный лоадер + gcompat/nvcompat. nvcc и сборка — на glibc-хосте.
#
# usage: sh test_llama_cuda_musl.sh
#   EDGE=~/lite/edge  MODEL=~/lite/models/qwen2.5-0.5b-q4km.gguf
set -u
E=${EDGE:-$HOME/lite/edge}
HL=${HOSTLIB:-/usr/lib/x86_64-linux-gnu}
M=${MODEL:-$HOME/lite/models/qwen2.5-0.5b-q4km.gguf}
fails=0
pass() { echo "PASS: $1"; }
fail() { echo "FAIL: $1"; fails=$((fails + 1)); }

[ -x "$E/usr/bin/llama-completion" ] && pass "llama-completion в edge" || fail "llama-completion нет в $E"
[ -f "$M" ] && pass "модель $M" || fail "нет модели $M"
[ -f "$E/usr/lib/libggml-cuda.so.1.0.0" ] && pass "libggml-cuda.so задеплоен" || fail "libggml-cuda.so нет в edge"

out=$(bwrap --bind "$E" / --proc /proc --dev /dev --unshare-user --uid 0 --dev-bind-try /sys /sys \
	--ro-bind "$M" /m.gguf \
	--ro-bind "$HL/libcuda.so.580.159.03" /usr/lib/libcuda.so.1 \
	$(for d in /dev/nvidia*; do echo --dev-bind "$d" "$d"; done) \
	sh -c 'LD_PRELOAD=/usr/lib/libnvcompat.so.0 \
		llama-completion -m /m.gguf -ngl 99 -p "Q: What is 2+2? Answer with just the number.
A:" -n 16 -t 4 --temp 0' 2>&1)
rc=$?

[ $rc -eq 0 ] && pass "llama-completion rc=0" || fail "llama-completion rc=$rc"
echo "$out" | grep -q "CUDA : ARCHS" && pass "CUDA-бэкенд загружен (system_info)" || fail "CUDA-бэкенд не в system_info"
echo "$out" | grep -q "eval time" && pass "инференс завершён" || fail "нет eval time"

[ $fails -eq 0 ] && echo "ALL PASS" || echo "$fails FAILED"
exit $((fails != 0))
