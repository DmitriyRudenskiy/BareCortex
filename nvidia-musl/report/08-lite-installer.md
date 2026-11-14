# 08 — Установщик lite-рецепта (`tools/nv-lite-install.sh`)

14.11.2026. Прогнан вживую в том же окружении, что и отчёт 07: Alpine edge minirootfs
(`musl-1.2.6-r6`, `gcompat-1.1.0-r4`) в `bwrap`, драйвер 580.196.07, без root снаружи
(root внутри). Это и есть целевая платформа (Chimera — на базе Alpine), так что прогон
считается проверкой, а не моком.

## Интерфейс

```sh
nv-lite-install.sh install [payload-dir]   # payload = распакованный .run
nv-lite-install.sh verify                  # симлинки + sha256 из манифеста + nvidia-smi
nv-lite-install.sh uninstall               # только то, что поставил install (readlink-проверка)
```

## Раскладка (D2 оператора, уточнённая замерами)

| куда | что |
|---|---|
| `/opt/nvidia-musl-exp/lib/` | 5 библиотек ступени 1 (запатченные) + 3 бинарника + `libnvcompat.so.0` (собран из `tools/shim/libnvcompat.c` одной строкой gcc) + `share/manifest.tsv` |
| `/usr/lib/` | 5 публичных имён симлинками: `libcuda.so.1`, `libnvidia-ml.so.1`, `libnvidia-ptxjitcompiler.so.1`, `libnvidia-opencl.so.1`, `libnvcompat.so.0` |

Имена сверены с SONAME файлов (`readelf -d`): у ptxjit и opencl SONAME — `.so.1`, а не
`.so.12`; у `libnvidia-glsi` публичного алиаса нет (SONAME = версионное имя), поэтому он
живёт только в `/opt` — его подгружает `libnvidia-glcore` по относительному пути, а GL-пусть
это не ступень 1. Бинарники не патчатся (stub-интерпретатор gcompat, отчёт 07); библиотеки —
`--add-needed libnvcompat.so.0 --rename-dynamic-symbols shim/libnvcompat.map`.

## Три бага, пойманных прогоном (поэтому install не доверен «на глаз»)

1. **`ld.so.conf.d` не подхватывается**: первая версия кладила `$PREFIX/lib` в
   `/etc/ld.so.conf.d/nvidia-musl-exp.conf` и звала `ldconfig`; после установки `dlopen`
   всё равно не находил `libnvcompat.so.0`. Упростили: `libnvcompat.so.0` — симлинком в
   `/usr/lib`, `conf.d` и `ldconfig` из рецепта удалены. Имена, которые кто-либо
   dlopen-ит, — ровно те, что в таблице.
2. **as-needed съедал NEEDED**: `gcc … -l:libgcompat.so.0` без `--no-as-needed` собирает
   shim **без** `NEEDED libgcompat.so.0` (shim не обращается ни к одному символу gcompat —
   он «транспорт» для gcompat). Симптом: `gnu_get_libc_version: symbol not found` после
   установки. Исправлено флагом в строке сборки.
3. **`nvidia-smi` вне PATH**: verify звал `nvidia-smi` из PATH, а бинарник лежит в
   `$PREFIX/lib` — проверка проходила молча. Теперь вызывается абсолютным путём.

## Матрица (после install, `env -i`, никакого `LD_LIBRARY_PATH`/`LD_PRELOAD`)

| dlopen | результат |
|---|---|
| `libcuda.so.1` | LOADER_OK SYM_OK, `cuInit` → rc=100 (нет устройства) |
| `libnvidia-ml.so.1` | LOADER_OK SYM_OK, `nvmlInit_v2` → rc=9 |
| `libnvidia-ptxjitcompiler.so.1` | LOADER_OK |
| `libnvidia-opencl.so.1` | LOADER_OK |
| `libnvcompat.so.0` | LOADER_OK |
| `nvidia-smi` (процесс) | «couldn't communicate with the NVIDIA driver» — т.е. NVML найден, драйвера нет |

После `uninstall`: симлинков в `/usr/lib` нет, `/opt/nvidia-musl-exp` удалён, `dlopen
libcuda.so.1` падает «No such file or directory». Чужие файлы в `/usr/lib` (например
`libnvidia-ml.so.580.159.03` от прежних экспериментов) не трогаются — uninstall удаляет
только симлинки, чей `readlink` указывает в `$PREFIX`.

## Манифест

`/opt/nvidia-musl-exp/share/manifest.tsv`: `file  sha256_before  sha256_after  op
needed_before  needed_after` на каждый из 8 файлов + shim. `verify` сверяет `sha256_after`
с установленными файлами. Это же таблица «что патч изменил» для отчётности.

## Чего тут нет (сознательно)

- GL/EGL/Wayland (D3 отложена), `libnvidia-tls` (нужен патченный musl, отчёт 07),
  kernel-модуль (это 2.3 на gpu), systemd-сервисы — на ступени 1 лестницы они не нужны.
- Автообнаружение драйвера: payload передаётся явно или ищется в трёх стандартных местах.
