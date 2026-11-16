# 15. llama.cpp + CUDA на musl — работает (2026-11-16)

**Станция:** 192.168.0.86, 5×RTX 4060 Ti (sm_89), драйвер 580.159.03, CUDA 13.2 toolkit на хосте,
Alpine edge + патченный musl (LIBC_NVIDIA_BUILD) + shim `libnvcompat.so.0`.

## Итог

`llama-completion` (musl-бинарь Alpine `llama-completion-1.0.0`, v1.0.0 = commit бэкенда)
с `-ngl 99` в musl-песочнице: **CUDA-бэкенд загружен, инференс на GPU**
(632 t/s prompt / 336 t/s gen на Qwen2.5-0.5B Q4_K_M против 158/46 CPU;
10–12 GiB VRAM на устройстве, видно из host-nvidia-smi).

## Схема

| Артефакт | ABI | Откуда |
|---|---|---|
| llama-completion, libllama, libggml, libggml-base | musl | `apk add` (edge) |
| libggml-cuda.so.1 | glibc | сборка `GGML_CUDA=ON GGML_BACKEND_DL=ON GGML_NATIVE=OFF`, sm_89, nvcc 13.2 на хосте |
| libcublas/Lt, libcudart, libnvrtc, libnvrtc-builtins | glibc | `/usr/local/cuda-13.2/lib64` |
| libnccl.so.2 | glibc | хост (apt) |
| libcuda.so.1, libnvidia-* | glibc | драйвер 580.159.03 |

glibc-артефакты получают тот же приём, что NVIDIA-драйверы:
`patchelf --add-needed libnvcompat.so.0 --rename-dynamic-symbols libnvcompat.map`
и исполняются в musl-процессе через патченный лоадер + gcompat + `LD_PRELOAD=libnvcompat`.
`libggml-cuda.so` тянет `libggml-base.so.1` по SONAME — биндится к уже загруженной
musl-копии (C-ABI, один commit), конфликт не возникает.

## Подводные камни (все пойманы)

1. **Шим в edge был старый** (без `__pthread_key_create`) → cublasLt падал с тем же
   `std::system_error(0)`, что EGL ICD. Свежий шим — и вся CUDA-цепочка грузится.
2. **`GGML_BACKEND_DL=ON` обязателен**: без него `libggml-cuda.so` экспортирует только
   `ggml_backend_cuda_init`, а registry ищет `ggml_backend_init` →
   «failed to find ggml_backend_init». Не совместим с `GGML_NATIVE` (нужен OFF).
3. **libcuda.so.1 должен быть по пути поиска** (`/usr/lib/libcuda.so.1`), иначе
   NEEDED `libggml-cuda.so` не резолвится.
4. Гонка двух фоновых cmake-сборок оставила .so старше объектов, make рерилинковать
   отказался, `sh link.txt` — «отказано в доступе» на старое имя; фикс — линк в
   `libggml-cuda.so.new` и замена.
5. `-no-cnv`/`--no-conversation` в v1.0.0 нет — для smoke-теста `llama-completion`.

## Воспроизведение

```
# x1 (одноразово): сборка бэкенда на хосте
cd ~/lite/llama/llama.cpp   # v1.0.0
PATH=/usr/local/cuda-13.2/bin:$PATH cmake -B build-cuda -DCMAKE_BUILD_TYPE=Release \
  -DGGML_CUDA=ON -DGGML_BACKEND_DL=ON -DGGML_NATIVE=OFF \
  -DCMAKE_CUDA_COMPILER=/usr/local/cuda-13.2/bin/nvcc -DCMAKE_CUDA_ARCHITECTURES=89 \
  -DLLAMA_BUILD_EXAMPLES=OFF -DLLAMA_BUILD_TESTS=OFF -DLLAMA_BUILD_APP=OFF
cmake --build build-cuda --target ggml-cuda -j16
# деплой: libggml-cuda.so.1.0.0 + 5 CUDA-рантаймов + libnccl в edge /usr/lib,
#         patchelf-алиасинг (см. cuda-alias.sh / deploy-cuda.sh)
# x2: тест
sh nvidia-musl/tools/test_llama_cuda_musl.sh
```
