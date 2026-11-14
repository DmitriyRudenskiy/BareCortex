# 06 — Контрольная точка Фазы 1: D1 (принято), D2 (предложение), D3 (отложить)

14.11.2026. Основание: отчёты 01 (Chimera PR #6531), 02 (ELF-аудит 580.196.07),
03 (git-bruh + символьный зазор), 04 (гист sineemore, Void musl), 05 (MuslMan) и замеры
на `(build)` (`192.168.0.86`, `~/.cache/nv580/`).

## Итог

- **D1 — принято оператором: ветка 580.** Фаза 1 не дала ни одного аргумента против.
- **D2 — приватная раскладка в `/opt/nvidia-musl-exp/`, системную musl не трогаем.** Плюс
  предложение **снять сборку патченной musl с критического пути**: ступень 1 лестницы
  (headless CUDA) не встречает ни одного из двух барьеров Chimera, для неё достаточно штатного
  Alpine musl 1.2.6-r6 + gcompat 1.1.0-r4.
- **D3 — отложить**, как и предусматривал TASK; при переходе к графике — порт `libglvnd` +
  `egl-wayland` из Chimera cports (PR #6504), а не APKBUILD с нуля.

## Решение оператора (14.11.2026, по этому отчёту)

- **D2 утверждено**: приватная раскладка, системный musl не трогаем. В lite-пути приватного ldso
  нет вообще — бинарники на **системный** `/lib/ld-musl-x86_64.so.1`, библиотеки через
  `LD_LIBRARY_PATH`. Независимое подтверждение из первоисточника: шапка патча 091 в cports —
  «NVIDIA driver uses initial-exec TLS in **its glcore**», 090 — одна ifunc-релокация в glcore.
- **Перенос Фазы 2 утверждён**: `2.2-lite → 2.3 → 2.0 «по требованию»`. Гейт: зелёный lite →
  2.0 закрывается как «не требуется для CUDA-пути»; красный → 2.0 возвращается, но уже со
  списком того, что чинят 090/091.
- К harness — 8 обязательных охран (иерархия статусов, изоляция, два режима шима, `__popcountdi2`,
  TLSDESC, weak-бакет, уровень gcompat, логирование). Выполнены в `tools/harness.c` +
  `tools/dlopen-harness.sh`, результаты — отчёт 07.
- **Обоснование выбора 580 vs 620 фиксируем так**: различается только kernel-ветка (`kernel/`
  против `kernel-open/`) и наличие версии в базе; символьный зазор и максимум требуемой glibc
  одинаковы (`GLIBC_2.26`), так что userspace-соображения выбор не меняют.
- ZLUDA закрыта как справочная, отдельный отчёт не нужен. D3 — отложить.

**Итог гейта (тот же день, отчёт 07): зелёный.** 8 из 9 файлов ступени 1 грузятся на штатном
musl 1.2.6-r6, `cuInit` → `rc=100` (CUDA_ERROR_NO_DEVICE), `nvmlInit_v2` → `rc=9`
(NVML_ERROR_DRIVER_NOT_LOADED); TLSDESC во всём дереве 0; weak-символов, оставшихся NULL, нет.
Попутно исправлено важное заблуждение 03/05: **gcompat не активируется через `DT_NEEDED
libc.so.6`** — musl подставляет glibc-имена на себя, поэтому библиотекам нужен
`--add-needed libnvcompat.so.0` (он тянет `libgcompat`), а бинарникам — ничего, у них
`PT_INTERP` = stub gcompat. Единственная непроходившая библиотека — `libnvidia-tls`
(initial-exec TLS), и `libcuda` от неё не зависит.

## D1 — ветка (принято 14.11.2026, оператор)

`NVIDIA-Linux-x86_64-580.196.07.run`, x86_64 sha256 `0aab0518db53cf73c412dc7f32bbc850ca95e839d8923d0de4bff42c1c9169ce`
(сверено с фактом из Приложения А и со скачанным файлом). Ставим **`-no-compat32.run`**
(sha256 `2189c47f…`, 72 ELF вместо 107, −392 МБ, `32/` не попадает в образ). Модули — **закрытые
`kernel/`**, не `kernel-open/`. `libstdc++.so.6` оставляем родной (вопрос libc++abi существует
только в 620). Hardening `!pie`.

Что добавила Фаза 1 к выбору: **620 не перевешивает**. Максимум требуемой версии символов по
всему 64-битному дереву — `GLIBC_2.26` в обеих ветках (в 580 выше 2.10 идёт только
`strfromf128@GLIBC_2.26` из `libcudadebugger`), символьный зазор против musl+gcompat одинаковый
(`dladdr1` в двух файлах), так что выбирать приходится не по «потянет ли musl», а по kernel-ветке
и наличию версии в базе.

## D2 — стратегия размещения

### Замеренная основа

Барьеры Chimera (патчи 090/091) в дереве 580.196.07 локализованы:

| барьер | где реально (замер 02) | есть ли на ступени 1 |
|---|---|---|
| IRELATIVE/ifunc (патч 090) | **1 релокация на всё дерево** — `libnvidia-glcore`, в `.rela.plt`; 0 IFUNC-символов | нет |
| `DF_STATIC_TLS` (патч 091) | 5 библиотек: `libnvidia-glcore`, `libGLX_nvidia`, `libglxserver_nvidia`, `libnvidia-tls`, `libvdpau_nvidia` | нет: на headless-пути не загружается ни одна — `libcuda` от `libnvidia-tls` не зависит (замер 02), остальные четыре это GL/VDPAU |

Символьный зазор ступени 1 (`libcuda`, `libnvidia-ml`, `nvidia-smi`, `nvidia-modprobe`,
`nvidia-persistenced`, `libnvidia-tls`, `libnvidia-opencl`, `libnvidia-ptxjitcompiler`, `libnvidia-glsi`) против
Alpine musl 1.2.6-r6 + gcompat 1.1.0-r4 — **0 неразрешённых GLOBAL** (замер 03, перепроверено
сегодня на 620). Во всём 64-битном дереве не хватает двух имён: `dladdr1` (только
`libnvidia-eglcore`/`libnvidia-glcore` → графическая ступень) и `__popcountdi2`
(`libcudadebugger`, `libnvidia-rtcore`), которое **есть** в Alpine `libgcc-15.2.0-r9`
(`libgcc_s.so.1`). LFS64-имена резолвит сам загрузчик musl (`ldso/dynlink.c`, `get_lfs64`),
шим не нужен; WEAK-символы загрузчик musl терпит.

Резонанс из отчёта 05: gcompat поставляет `/lib/ld-linux-x86-64.so.2` (22 728 Б, статический
stub) и `libc.so.6 → libgcompat.so.0`, т. е. glibc-бинарник NVIDIA запускается на musl **без
`patchelf` вообще** — апстрим: stub «redirecting it to use musl and automatically preloading the
gcompat library». MuslMan, который делает ровно это патчем и вендорингом glibc, — подход A
в костюме B; он не является доказательством, что «полный musl возможен».

### Решение (предлагаю)

1. **Только приватная раскладка**: всё под `/opt/nvidia-musl-exp/`, поиск — строкой в
   `/etc/ld-musl-x86_64.path` (механизм подтверждён: `ldso/dynlink.c:1141-1143`) либо явным
   `--library-path`. Системная замена musl (модель Chimera) — не делаем; TASK требует явного
   одобрения, и Фазой 1 причины для неё не найдены.
2. **Порядок Фазы 2 меняем на `2.2-lite → 2.3 → 2.0`.** Первым шагом — dlopen-harness против
   **штатного** musl 1.2.6-r6 + gcompat на `(build)`, без GPU: он за один прогон отвечает, нужна
   ли вообще приватная патченная libc для ступени 1. Статика предсказывает «не нужна» (таблица
   выше). Если подтверждается — сборка `musl-nvidia` (`-DLIBC_NVIDIA_BUILD`, патчи 090/091,
   smoke-тест) переезжает к графической ступени и собирается вместе с D3. Если harness покажет
   обратное — возвращаем 2.0 на место, потеря один прогон.
3. **Чего не делаем** (по отчёту 05): не вендорим `sgerrand/alpine-pkg-glibc` (EOL 2.35-r1 от
   2023-04-13, в MuslMan — без единой контрольной суммы; нашу зафиксировали: 2 826 689 Б,
   sha256 `276f43ce9b2d5878422bca94ca94e882a7eb263abe171d233ac037201ffcaf06`), не патчим
   `PT_INTERP`, не пишем `egl_vendor`/ICD json руками — ставим `10_nvidia.json`,
   `10_nvidia_wayland.json`, `15_nvidia_gbm.json`, `20_nvidia_xcb.json`, `20_nvidia_xlib.json`,
   `nvidia_icd.json` из payload, переписав `library_path` на абсолютный.
4. **Установщик**: манифест `file|dir|line-append` + `.muslman-backup` + идемпотентный
   `append_line_once` и `doctor`-режим «только показать» — по образцу MuslMan (отчёт 05 §2),
   с sha256-пинами на каждый скачанный артефакт (у MuslMan их нет).
5. **Fallback остаётся один**: bwrap-jail с glibc-rootfs (отчёт 04 §5, 8 шагов). Требование к
   нему по замеру: glibc ≥ 2.26 и только amd64. Другого fallback Фаза 1 не оставила — MuslMan
   им быть не может (отчёт 05 §4).

## D3 — glvnd: отложить (подтверждается)

Alpine 3.25: `libglvnd` нет нигде (APKINDEX 3.22 main+community, 3.25 main; aports master —
404 на `main/` и `community/`), mesa собрана без `-Dglvnd`, `egl-wayland` нет. Для
`nvidia-smi`/CUDA glvnd не нужен, и ни одна из пяти библиотек с `DF_STATIC_TLS` не участвует в
headless-пути, так что отложение безопасно. Когда дойдём:

- порт `libglvnd` + `egl-wayland` из Chimera cports (они мусл-патченные), а не APKBUILD с нуля;
- не собирать клей старше драйвера: payload 580 поставляет `libnvidia-egl-wayland.so.1.1.20` и
  `libnvidia-egl-gbm.so.1.1.3` (MuslMan пинит 1.1.18/1.1.2), а 620 добавляет ещё
  `libnvidia-egl-wayland2.so.1.0.2` + `09_nvidia_wayland2.json`;
- `dladdr1` для `eglcore`/`glcore` — патч gcompat из git-bruh ложится на 1.1.0 как есть
  (в исходниках 1.1.0 этого символа нет — проверено grep).

## Что делает Фаза 1 закрытой

| требует TASK | статус |
|---|---|
| 1.1 Chimera PR | отчёт 01 |
| 1.2 git-bruh | отчёт 03 + `tools/sym-gap.py` |
| 1.3 гист sineemore | отчёт 04 + карта порта Void→Alpine |
| 1.4 MuslMan (bwrap/`.run`/`/opt`/patchelf/manifest, сверка glibc 2.35 с 620, вердикт) | отчёт 05; вердикт «не годен», 7 заимствований, 2.35 ≥ 2.26 → хватает |
| 1.5 ZLUDA (опционально) | **пропущено**: это реимплементация CUDA API для AMD, а не загрузчик; единственная переносимая механика (drop-in `libcuda.so`) нам не нужна — мы ставим настоящий `libcuda.so` от NVIDIA. Справочный отчёт — по запросу, отдельная сессия |
| контрольная точка D1/D2/D3 | этот отчёт |

## Следующий шаг (без GPU)

`tools/dlopen-harness.sh` + прогон против штатного musl (шаг 2.2-lite выше): набор файлов ступени
1, `LD_TRACE_LOADED_OBJECTS=1`, `Error relocating…` из stderr, `strace -e openat,mmap,mprotect`.
Артефакт — `report/07-load-forensics-headless.md`; он же решает судьбу шага 2.0.

## Воспроизведение

```sh
# максимум GLIBC_2.xx по дереву (одинаково для 580.196.07 и 620.70.06)
find <payload> -maxdepth 1 -type f | while read f; do
  head -c4 "$f" | grep -qU "$(printf '\x7fELF')" && readelf -V "$f" 2>/dev/null
done | grep -oE 'GLIBC_[0-9]+\.[0-9]+(\.[0-9]+)?' | sort -u -V | tail -1     # GLIBC_2.26

# символьный зазор (03); для 620 — через симлинк-дерево
NV580=~/.cache/nv580 python3 tools/sym-gap.py
mkdir -p /tmp/nv620/ex && ln -s ~/.cache/nv580/ex620 /tmp/nv620/ex/NVIDIA-Linux-x86_64-620.70.06
ln -s ~/.cache/nv580/symgap /tmp/nv620/symgap && NV580=/tmp/nv620 python3 ~/.cache/nv580/sym-gap.py

# gcompat закрывает PT_INTERP и libc.so.6
tar -tzf ~/.cache/nv580/symgap/gcompat-1.1.0-r4.apk | grep -E 'ld-linux|libc'
strings ~/.cache/nv580/symgap/lib/ld-linux-x86-64.so.2 | grep gcompat
```
