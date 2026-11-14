# 2.1 — ELF-аудит проприетарского userspace NVIDIA 580.196.07

Ветка выбрана оператором: **580** (`progress.md`, ruling 14.11.2026), значит аудит —
по `NVIDIA-Linux-x86_64-580.196.07.run`, закрытые `kernel/`.

Инструмент: `tools/elf-audit.py` (pyelftools) + независимая перепроверка `readelf`.
Машина: `(build)` — хост `192.168.0.86`, `~/.cache/nv580/`, venv с `pyelftools`.
`.run` и распакованное дерево в git не попадают.

## Артефакт

| | |
|---|---|
| URL | `https://us.download.nvidia.com/XFree86/Linux-x86_64/580.196.07/NVIDIA-Linux-x86_64-580.196.07.run` |
| размер | 337 686 528 B |
| sha256 | `0aab0518db53cf73c412dc7f32bbc850ca95e839d8923d0de4bff42c1c9169ce` — **совпал с Приложением А** |
| распаковано | `sh <run> --extract-only` (изнутри каталога назначения; `--extract-only=ПУТЬ` установщик 580 не понимает — отдаёт аргумент внутреннему `nvidia-installer` и падает) |
| дерево | 1,5 GB, **107 ELF-файлов**, из них **35 32-битных (392 MB)** в `32/` и `32/libglvnd_install_checker/` |

## Команды воспроизведения

```sh
mkdir -p ~/.cache/nv580 && cd ~/.cache/nv580
python3 -m venv .venv && ./.venv/bin/pip install pyelftools
curl -LO https://us.download.nvidia.com/XFree86/Linux-x86_64/580.196.07/NVIDIA-Linux-x86_64-580.196.07.run
sha256sum NVIDIA-Linux-x86_64-580.196.07.run   # 0aab0518db53cf73c412dc7f32bbc850ca95e839d8923d0de4bff42c1c9169ce
mkdir extracted && cd extracted && sh ../NVIDIA-Linux-x86_64-580.196.07.run --extract-only
cd .. && ./.venv/bin/python elf-audit.py extracted/NVIDIA-Linux-x86_64-580.196.07 --all-elf \
      --json audit-all.json --csv audit-all.csv          # ~4 мин
./.venv/bin/python elf-audit.py --selftest /lib/x86_64-linux-gnu/libc.so.6
```

Перепроверка (binutils, ничего общего с pyelftools; `LC_ALL=C` обязателен — в русской локали
`readelf -d` печатает метки тегов по-русски, и фильтр по `Shared library:` молча даёт пусто):

```sh
for f in $(find extracted -type f); do head -c4 "$f" | grep -q ELF && readelf -V "$f" 2>/dev/null \
  | grep -oE "GLIBC_[0-9]+\.[0-9]+"; done | sed s/GLIBC_// | sort -V | tail -1        # 2.26
for f in $(find extracted -type f); do head -c4 "$f" | grep -q ELF && readelf -r "$f" 2>/dev/null \
  | grep -c R_X86_64_IRELATIV; done | paste -sd+ | bc                                  # 1
for f in $(find extracted -type f); do head -c4 "$f" | grep -q ELF && LC_ALL=C readelf -d "$f" 2>/dev/null \
  | grep -q STATIC_TLS && echo "$f"; done                                              # 11 путей: 5 в 64-бит, 6 в 32/
```

Контроль, что детекторы вообще что-то видят: `libc.so.6` живого хоста (glibc 2.39) даёт
**46 IRELATIVE** и `readelf`, и скриптом. Нули ниже — не нули из-за слепоты.

## Результаты

| метрика | всё дерево (107) | только 64-бит (72) | набор TASK.md (33) |
|---|---|---|---|
| максимальная `GLIBC_2.NN` | **2.26** | 2.10 | 2.10 |
| IRELATIVE-релокаций | **1** | 1 | 1 |
| символов `STT_GNU_IFUNC` | 0 | 0 | 0 |
| TPOFF64 / DTPMOD64 | 0 / 0 | 0 / 0 | 0 / 0 |
| библиотек с `DF_STATIC_TLS` | 11 | **5** | 2 |
| потребителей `libstdc++.so.6` | **0** | 0 | 0 |
| кандидатов «статический mutex» | ≥370 | ≥311 | ≥208 |

(`≥` — у эвристики потолок 50 находок на файл, это нижняя граница, а не перепись.)

Единственный файл, требующий выше 2.10: `libcudadebugger.so.580.196.07` →
`strfromf128@GLIBC_2.26` (конверсия `_Float128`; в musl отсутствует). Единственная
IRELATIVE-релокация — `libnvidia-glcore.so.580.196.07`, и она в `.rela.plt`.
`DF_STATIC_TLS`: `libnvidia-glcore`, `libGLX_nvidia`, `libglxserver_nvidia`,
`libnvidia-tls`, `libvdpau_nvidia`. В `32/` к ним добавляются `libnvcuvid` и
`libnvidia-encode` — 32-битные сборки поставлены с `DF_STATIC_TLS`, а 64-битные нет,
так что «копия тех же файлов» по этому флагу не совпадает.

## Что это меняет в плане

1. **glibc для jail: 2.26, не 2.35+.** Критика из 1.4 («вендоринг sgerrand glibc 2.35 —
   сверить с требованиями») закрывается измерением: 2.35 покрывает 580 с запасом; подойдёт
   любой rootfs с glibc ≥ 2.26. Спор про «свежий rootfs» становится спором про `strfromf128`
   одного файла, который грузится только под отладчиком CUDA.
2. **Патч 090 (ifunc) нужен не «из-за NVIDIA».** Во всём вендорском дереве одна IRELATIVE и
   ни одного IFUNC-символа. Ifunc-нагрузка, ради которой q66 писал 090, приходит от
   glibc-ных библиотек, которые драйвер грузит в jail (glvnd, X, wayland, glib, сама
   вендоренная glibc). Для 580 это следует из чисел, а не из догадки.
3. **Патч 091 (static TLS surplus) — про GL-путь, а не про CUDA.** `libnvidia-tls.so` нужен
   только `libGLX_nvidia`, `libglxserver_nvidia` и `libnvidia-glcore`; `libcuda.so` его
   **не** хостит (NEEDED: только libm/libc/libdl/libpthread/librt). TPOFF64/DTPMOD64 = 0
   при наличии `DF_STATIC_TLS` = local-exec TLS, то есть резерв в статическом TLS нужен при
   позднем `dlopen`. Рабочая гипотеза на 2.2/2.3: **headless-CUDA (libcuda, nvidia-smi,
   NVML, MPS) проходит без обоих патчей**, а 090/091 решают судьбу GL/Vulkan. Проверять на
   `(gpu)`, не здесь.
4. **`libstdc++` в 580 не нужен никому.** Отдельная история 620 (`libnvidia-api.so.1`); для
   580 вопрос «менять ли libc++abi на родной libstdc++ Alpine» не возникает.
5. **Что чинить patchelf/gcompat (glibc-only `DT_NEEDED`, по числу файлов):** `libc.so.6` 62,
   `libdl.so.2` 59, `libpthread.so.0` 33, `librt.so.1` 25, `ld-linux-x86-64.so.2` 16,
   `libgcc_s.so.1` 2, `libutil.so.1` 1. Отдельно: `libcrypto.so.1.1` и `libcrypto.so.3`
   (PKCS#11-провайдеры), GTK2/GTK3/pango/cairo (`nvidia-settings`).
6. **32-бит — вот где он живёт.** Драйвер несёт 35 32-битных ELF на 392 MB. Опция
   установщика, названная самим `nvidia-installer 580.196.07 --advanced-options --help`:
   `--install-compat32-libs, --no-install-compat32-libs` («Setting --no-install-compat32-libs
   will skip installation of these libraries»); рядом `--compat32-prefix` (по умолчанию
   `/usr`), `--compat32-libdir` (`lib` или `lib32`), `--compat32-chroot`, `--gbm-backend-dir32`.
   Это ответ на `openspec/changes/gpu-stack/tasks.md` 1.5: правило «в образе только
   ELFCLASS64» держится одной опцией, и 392 MB — цена, по которой оно держится.
7. **Кандидаты на `libnvcompat` (статические mutex-инициализаторы, эвристика, руками не
   проверялись):** `libnvidia-glcore` 50 (потолок), `libnvidia-eglcore` 50,
   `nvidia-ngx-updater` 42, `libnvidia-ngx.so` 30, `nv-kernel.o_binary` 27,
   `libnvidia-vksc-core` 25, `libnvidia-gpucomp` 18. Снова: GL/NVEX-сторона, не CUDA.

## Три бага в `tools/elf-audit.py`, которые нашлись только на живом дереве

Скрипт был переписан по спецификации хэндоффа и проверялся только `--selftest` на
`libc.so.6` — этого хватило, чтобы пропустить три ошибки, каждая из которых на 580
приводила к «всё чисто, GCOMPAT_CANDIDATE»:

1. **Вечный цикл в разборе `.gnu.version_r`.** `vn_next`/`vna_next` — смещения
   *относительно своей записи*, а код делал `i = vn_next`, `j = vna_next` вместо `+=`
   (плюс guard `i + 20` при размере записи 16). На файле с ≥3 Verneed-записями — мёртвая
   петля: прогон висел **больше 30 минут** на `32/libnvidia-egl-wayland.so.1.1.20`
   (faulthandler: `parse_cstring_from_stream` ← `version_reqs`). После фикса весь прогон
   дерева занимает ~4 минуты. В `--selftest` добавлена синтетическая проверка цепочки
   (2 библиотеки, 3 версии, разрыв между записями) — на старом коде она падает с
   `{'libc.so.6': ['2.17'], ...}` и работает без glibc, то есть и на macOS.
2. **`reloc_census` смотрел только `.rela.dyn`/`.rel.dyn`** и пропускал `.rela.plt` —
   то есть не видел единственную IRELATIVE дерева. Теперь перебираются секции типов
   `SHT_RELA`/`SHT_REL`.
3. **`static_tls` читал только `DT_FLAGS_1`/`DF_1_STATIC_TLS`**, хотя TASK.md 2.1 требует
   «DT_FLAGS/DF_STATIC_TLS». Библиотеки NVIDIA ставят `DF_STATIC_TLS` (0x10) в `DT_FLAGS`
   (например `libnvidia-tls`: `FLAGS SYMBOLIC STATIC_TLS`, слово 0x12). До фикса —
   `NEEDS_PATCHED_MUSL: 0`, после — 5 файлов, ровно те же, что находит `readelf`.

## Чего этот аудит не говорит

Статика без GPU: ни рантайм-резолвинга (это 2.2), ни поведения под приватным musl-nvidia.
`libcudart*` из набора в `.run` нет — CUDA Toolkit это отдельный артефакт и отдельный
аудит. 32-битные файлы просканированы (и попали в «всё дерево»), но вне области: образ по
правилу `amd64-only-userspace` их не содержит.

## Дозамер 14.11 — пункты 5–7 охран оператора (для 2.2-lite)

`tools/elf-audit.py` расширен: счётчик `R_X86_64_TLSDESC` (тип 36; aarch64 — 137) и бакет
weak-undefined символов (`.dynsym`, `STB_WEAK` + `SHN_UNDEF`, только FUNC/OBJECT/IFUNC).
Прогон по всему дереву 580.196.07 (`--all-elf`, 107 ELF, `musl`-независимая статика):

| метрика | всё дерево | headless-набор (9 файлов ступени 1) |
|---|---|---|
| `R_X86_64_TLSDESC` | **0** | **0** |
| IRELATIVE | 1 (`libnvidia-glcore`) | 0 |
| файлов с `DF_STATIC_TLS` | 11 | 1 (`libnvidia-tls`, от `libcuda` не зависит) |
| `NEEDS_PATCHED_MUSL` | 11 | 1 |

Значит gnu2-TLS-диалекта (который musl не парсит вовсе) в драйвере нет — барьер 091 это
исключительно `initial-exec`, и в headless-наборе он встречается ровно один раз, вне
зависимостей `libcuda`. Проверено вживую в отчёте 07: `_nv005tls: initial-exec TLS resolves to
dynamic definition` — единственный непроходивший файл ступени 1.

**Weak-бакет headless-набора** (musl резолвит слабые UND в NULL молча, поэтому это риск
рантайма, а не загрузки): `__cxa_finalize` — `libcuda`, `libnvidia-ml`, `libnvidia-tls`,
`libnvidia-opencl`, `libnvidia-ptxjitcompiler`, `libnvidia-glsi`;
`pthread_getaffinity_np`/`pthread_setaffinity_np` — `libnvidia-ml`, `nvidia-smi`.
Все три имени проверены `dlsym` под musl 1.2.6-r6 + gcompat 1.1.0-r4 (отчёт 07): резолвятся,
NULL не остаётся ни одного.

**Кандидаты статических рекурсивных мьютексов** (та же эвристика, что `check-mutex.py` в PR, но
по `.data` и `.data.rel.ro`): в headless-наборе — `nvidia-smi` (1, kind=1) и
`libnvidia-glsi.so` (10, kind=1, шаг 96 байт). Оба импортируют `pthread_mutex_lock`, поэтому
шим для них не бесполезен; пометки лежат в `.data.rel.ro` (после RELRO — read-only), так что
возможен ложный сигнал. `check-mutex.py` из PR (только `.data`) эти файлы не помечает: наша
проверка шире, расхождение в сторону избыточности.
