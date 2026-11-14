# 07 — 2.2-lite: загрузка headless-набора под штатным musl (гейт D2/2.0)

14.11.2026. Запуск на `(build)` (`Pop!_OS 24.04`, без root, без GPU): Alpine edge
minirootfs `3.25.0_alpha20260805` (sha256 `5acac12c…`) в `bwrap --unshare-user --uid 0`,
внутри — **штатный** `musl-1.2.6-r6` + `gcompat-1.1.0-r4` + `gcc-15.2.0-r9` + `patchelf-0.18.0-r3`.
Инструменты: `tools/lite-env-setup.sh`, `tools/harness.c` (собирается musl-ным gcc внутри
rootfs, значит `dlopen()` в нём — это и есть загрузчик Alpine), `tools/dlopen-harness.sh`.
Объекты: `~/.cache/nv580/ex/NVIDIA-Linux-x86_64-580.196.07` (полный `.run`, 580.196.07).
Сводки прогонов — `report/data/07-summary-A.tsv` (без активации), `-P.tsv` (`LD_PRELOAD`),
`-B.tsv` (рецепт PR).

## Вердикт: гейт зелёный — шаг 2.0 с CUDA-пути снимается

Ступень 1 лестницы грузится и работает на **непатченом** musl: 8 из 9 файлов, `cuInit`
резолвится и вызывается, `nvmlInit_v2` вызывается. Ни IRELATIVE, ни статического TLS для этого
не понадобилось. `musl-nvidia` (патчи 090/091) остаётся задачей будущей GL-ступени вместе с D3.

| файл | обход зависимостей | dlopen(RTLD_NOW) | ABI | вызов |
|---|---|---|---|---|
| `libcuda.so.580.196.07` | OK | **LOADER_OK** | `cuInit` 1/1 | **`cuInit rc=100`** = CUDA_ERROR_NO_DEVICE |
| `libnvidia-ml.so.580.196.07` | OK | **LOADER_OK** | `nvmlInit_v2`, `nvmlDeviceGetName` 2/2 | **`nvmlInit_v2 rc=9`** = NVML_ERROR_DRIVER_NOT_LOADED |
| `libnvidia-opencl.so.580.196.07` | OK | LOADER_OK | — | — |
| `libnvidia-ptxjitcompiler.so.580.196.07` | OK | LOADER_OK | — | — |
| `libnvidia-glsi.so.580.196.07` | OK | LOADER_OK | — | — |
| `nvidia-smi` | OK | LOADER_OK | — | — |
| `nvidia-modprobe` | OK | LOADER_OK | — | — |
| `nvidia-persistenced` | OK | LOADER_OK | — | — |
| `libnvidia-tls.so.580.196.07` | OK | **LOADER_FAIL** | — | — |

Оба «вызова» — это ровно статус *loader OK, device absent*: код ошибки вместо падения, что и
требуется на машине без GPU (guardrail 1). Падений и таймаутов не было ни в одном режиме.

Единственный провал — `libnvidia-tls`:
`Error relocating …: _nv005tls: initial-exec TLS resolves to dynamic definition` — то есть
барьер патча 091 (`DF_STATIC_TLS`) воспроизведён вживую, впервые. Он **не трогает CUDA**:
`libcuda` не требует `libnvidia-tls` (замер 02), и `libcuda` грузится без него.

Активация gcompat (ниже) — обязательное условие: без неё падают 8 из 9, с той же самой ошибкой
«symbol not found».

## Как на самом деле активируется gcompat (поправка к отчётам 03 и 05)

Замер `--list` (аналог `LD_TRACE_LOADED_OBJECTS`): musl подставляет glibc-имена **на себя** —

```
libc.so.6  => /lib/ld-musl-x86_64.so.1        libm.so.6      => /lib/ld-musl-x86_64.so.1
libdl.so.2 => /lib/ld-musl-x86_64.so.1        libpthread.so.0 => /lib/ld-musl-x86_64.so.1
librt.so.1 => /lib/ld-musl-x86_64.so.1        libc.so        => /lib/ld-musl-x86_64.so.1
```

Поэтому `DT_NEEDED: libc.so.6` до `libgcompat.so.0` **не доводит**, и `gcompat` не участвует в
процессе, пока его явно не загрузили. Без него headless-набор даёт:

```
Error relocating /nv/libcuda.so.580.196.07: gnu_get_libc_version: symbol not found
Error relocating /nv/libnvidia-ml.so.580.196.07: dlvsym: symbol not found
Error relocating /nv/libnvidia-ptxjitcompiler.so.580.196.07: __strtok_r: symbol not found
Error relocating /nv/nvidia-modprobe: __strdup: symbol not found
Error relocating /nv/nvidia-smi: __isnan: symbol not found
```

Три рабочих способа активации (все проверены):

1. `LD_PRELOAD=/lib/libgcompat.so.0` — режим A1, годится для прогонов и отладки;
2. **для бинарников не нужно ничего**: их `PT_INTERP` = `/lib/ld-linux-x86-64.so.2`, а это stub
   gcompat, который сам исполняет `ld-musl` с `--preload /lib/libgcompat.so.0` (видно в `strace`:
   `execve("/lib/ld-musl-x86_64.so.1", ["ld-linux-x86-64.so.2", "--argv0", "/nv/nvidia-smi",
   "--preload", "/lib/libgcompat.so.0 ", "--", …])`);
3. **для библиотек — рецепт PR #6531**: `--add-needed libnvcompat.so.0`, а `libnvcompat` уже
   сам тянет `libgcompat.so.0` (собран с `-l:libgcompat.so.0`) — и символы появляются.

### Абляция: что именно даёт шим (guardrail 3)

Объект — `libcuda`, режимы отличаются только набором операций `patchelf`, `LD_PRELOAD` выключен:

| вариант (`tools/ablate-shim.sh`) | результат |
|---|---|
| A: ничего (режим без активации) | LOADER_FAIL — `gnu_get_libc_version: symbol not found` |
| B: только `--add-needed libnvcompat.so.0` | **LOADER_OK**, `cuInit` резолвится |
| C: removes + `--replace-needed libc.so.6 libc.so`, без шима | LOADER_FAIL — тот же символ |
| D: `--replace-needed libc.so.6 libc.so` + shim + rename (= рецепт PR за вычетом removes) | LOADER_OK |
| рецепт PR целиком (режим B прогона: removes + rename + shim) | LOADER_OK, `cuInit rc=100`, `nvmlInit_v2 rc=9` |

Вывод: **единственная обязательная операция над библиотеками — `--add-needed libnvcompat.so.0`**.
`--remove-needed …` и `--replace-needed libc.so.6 libc.so` косметические: musl эти имена всё
равно подставляет на себя, а реальные символы приходят с `libgcompat`, которую тянет шим.
Отдельно: вывод отчётов 03/05 «gcompat поставляет `libc.so.6`, значит `patchelf` не нужен» —
в части активации неверен; верным он оказывается только для бинарников (у них stub-интерпретатор).

## По пунктам охран оператора

1. **Иерархия успеха** — реализована в `tools/harness.c`: `LOADER_OK/FAIL` → `SYM_OK/NULL` →
   `CALL_OK … rc=N`; «device absent» — отдельный статус, не провал; краш = сигнал, ловится
   родителем (`CRASH(sigN)`), таймаут 90 с = `TIMEOUT`.
2. **Изоляция** — каждый `dlopen` в отдельном процессе `bwrap`, на каждый файл свой лог
   (`dlopen.<file>.log`, `ldlist.<file>.log`), сводка в `summary.tsv`.
3. **Два режима + абляция** — см. таблицу выше; шим обязателен и как активатор gcompat для
   библиотек, и (ниже) как фикс рекурсивных мьютексов.
4. **`__popcountdi2`** — потребители в дереве: `libcudadebugger`, `libnvidia-rtcore`. В
   NEEDED-замыкании headless-набора их **нет** (`libcuda` → `libm libc libdl libpthread librt`),
   поэтому `libgcc_s.so.1` в замыкании не нужен и добавлять в sanitize нечего. При установке
   полного набора этим двум файлам нужен `--add-needed libgcc_s.so.1` (символ есть в Alpine
   `libgcc-15.2.0-r9`).
5. **`R_X86_64_TLSDESC`** — счётчик добавлен в `tools/elf-audit.py` (тип 36; для aarch64 137).
   Замер по всему дереву (107 ELF): **0 TLSDESC во всём дереве и 0 в headless-наборе**.
   gnu2-TLS-диалект NVIDIA не использует — ещё одно подтверждение, что 091 про `initial-exec`,
   а не про TLSDESC.
6. **Weak-символы отдельным бакетом** — в headless-наборе: `__cxa_finalize` (6 файлов),
   `pthread_getaffinity_np`, `pthread_setaffinity_np` (`libnvidia-ml`, `nvidia-smi`). Проверено
   `dlsym` под musl+gcompat по каждому имени: **резолвятся все, NULL не остаётся ни одного**,
   так что скрытых «упадём на первом вызове» мест в этом наборе нет.
7. **Уровень gcompat** — использован `gcompat-1.1.0-r4` из репозитория edge (тот же, против
   которого считался зазор в 03); прогон зелёный, пересобирать из git `0660063…` не потребовалось.
8. **Логирование** — stderr ldso и `--list` по каждому файлу собраны (`out*/trace.txt` —
   сводный обход). Оговорка по `strace`: под `bwrap` с `-f` трассировка `nvidia-smi` теряется
   (stub форкает, `--cap-add all` не помогает), полный `strace -f` — на `(gpu)` под root.

## Мьютексы в headless-наборе

Импорт `pthread_mutex_lock`/`trylock`: 7 из 9 файлов (не импортируют только `nvidia-modprobe`
и `libnvidia-tls`). Кандидаты статических рекурсивных инициализаторов (наша эвристика:
16 нулей → kind 1..3 → 23 нуля, по `.data` и `.data.rel.ro`):

| файл | кандидатов | секция |
|---|---|---|
| `nvidia-smi` | 1 (kind=1) | `.data.rel.ro` |
| `libnvidia-glsi.so` | 10 (kind=1, шаг 96 байт — похоже на массив структур) | `.data.rel.ro` |

Chimera-гейт `user/nvidia580/files/check-mutex.py` (тот же паттерн, но только `.data`) их не
помечает — наша проверка шире. Обе пометки в `.data.rel.ro`, а она после `RELRO` read-only, так
что вероятнее ложный сигнал; решится тестом на `(gpu)`. Шим включается в рецепт независимо —
по пункту 3 выше и потому, что рекурсивные мьютексы проверяются поведением, а не статикой.

## Что это меняет в рецепте и в D2

- Библиотеки: `patchelf --add-needed libnvcompat.so.0` (+ `--rename-dynamic-symbols
  libnvcompat.map`, если включаем фикс мьютексов). Больше для ступени 1 не нужно.
- Бинарники: не патчим вообще — `PT_INTERP` уже указывает на stub gcompat.
- **`/opt/nvidia-musl-exp` самодостаточен только для файлов, но не для `nvidia-smi`**: он ищет
  NVML по абсолютным путям и `LD_LIBRARY_PATH` игнорирует —
  `/usr/lib64/libnvidia-ml.so.1`, `/usr/lib/libnvidia-ml.so.1`, `/usr/lib32/…`,
  `/usr/lib64/nvidia/libnvidia-ml.so.1`, `/usr/lib/nvidia/libnvidia-ml.so.1` (строки бинарника).
  Значит установщик обязан создать симлинки в `/usr/lib` (плюс SONAME-симлинки `libcuda.so.1`,
  `libnvidia-ml.so.1`, которых в распакованном payload нет — их создаёт `.run`-установщик).
  Формулировка D2 «всё живёт в `/opt`» уточняется: payload и приватные файлы в `/opt`,
  рантайм-имена — симлинками в `/usr/lib`.
- `libnvcompat.so.0` обязана лежать в стандартном пути поиска (`/usr/lib`), иначе библиотеки не
  грузятся: `Error loading shared library libnvcompat.so.0` (проверено на копии без неё).
- Исходники шима взяты из PR #6531; **head PR переехал на `7b121686e15c` (13.11.2026)** —
  зафиксированную в TASK ссылку `c2653298` надо обновить.

## Следующий гейт

2.3 на `(gpu)`: `nvidia-smi` и `cuInit` с загруженным модулем. Ожидание — устройства вместо
`rc=100`/`rc=9`. Если там появится поведение, которого нет в статике (мьютексы, weak-пути),
фиксим точечно; возврата 2.0 на критический путь прогон не потребовал.

## Как повторить

```sh
# (build), без root. ~200 МБ в ~/lite/edge
sh tools/lite-env-setup.sh
OUT=~/lite/outA sh tools/dlopen-harness.sh --set headless --call                 # без активации
OUT=~/lite/outP sh tools/dlopen-harness.sh --set headless --call \
        --preload /lib/libgcompat.so.0                                          # A1
OUT=~/lite/outB sh tools/dlopen-harness.sh --set headless --call --shim          # рецепт PR
sh tools/ablate-shim.sh                        # что именно даёт шим (A/B/C/D, таблица выше)
# TLSDESC / weak / mutex-счётчики по дереву
python3 tools/elf-audit.py <payload> --all-elf --csv audit.csv --json audit.json
# Chimera-гейт мьютексов (нужны nm и objcopy)
python3 check-mutex.py <payload>/libcuda.so.580.196.07
```
