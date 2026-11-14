# ЗАДАНИЕ v2: Проприетарный NVIDIA на musl (Alpine) — порт подхода Chimera + агентный REA

Источник: хэндофф оператора 12.11.2026 — задание v2, выданное локальным агентом после
разбора cports PR #6531 и верифицированное оператором по живому PR (таблица сверки —
в `report/01-study-chimera.md`). Применены три правки оператора:
(1) полный head-коммит PR; (2) подтверждение patchelf 0.18.0; (3) привязка sha256 к архитектурам.
Задание v1 от 11.11.2026 устарело, сюда не копировалось.

---

## Роль и цель

Ты — инженер-исследователь по бинарной совместимости Linux. Цель: работа проприетарного
userspace-стека NVIDIA на Alpine Linux x86_64 (musl) — от `nvidia-smi` до CUDA. База:
**подход Chimera (патченый musl + gcompat-git + libnvcompat + patchelf)**, проверенный в
cports PR #6531; fallback: **glibc-jail (подход A, Void/гист sineemore)**. REA
(https://rea.tools) применяется только после Фазы 1. Работа исследовательская;
публикуются только скрипты/патчи, не бинарники.

## Верифицированный контекст (на 12.11.2026; не переоткрывать, источники — Приложение Б)

- Kernel-модули NVIDIA libc-независимы; весь барьер — glibc-userspace. NVIDIA официально
  отказала: nvidia-installer#10, закрыт 10.01.2020.
- **Chimera, блог 07.11.2026 + PR #6531 (Draft/WIP!, head `c26532983c3b811c2d6b310058761bba39491bbe`
  от 11.11.2026)**: полный рабочий рецепт для 580.196.07 и 620.70.06. Барьеры и решения:
  (1) `R_X86_64_IRELATIVE`/ifunc не в musl → патч 090 (из Alpaquita musl-perf,
  автор Alexey Kodanev); (2) initial-exec TLS (`DF_STATIC_TLS`) в dlopen → патч 091
  (резерв 256 байт, только pointer-aligned); (3) статические рекурсивные mutex-инициализаторы
  glibc → шим `libnvcompat` (3 символа, `--rename-dynamic-symbols`); (4) в 620
  `libnvidia-api.so.1` тянет `libstdc++.so.6` → у Chimera замена на **libc++abi.so.1**
  (не libc++!). На Alpine проще поставить родной `libstdc++`. Состав PR — Приложение А.
- **gcompat**: релиз 1.1.0 не покрывает нужные символы; Chimera использует git-ревизию
  `06600632096b14e03e32b234ec0b02a9d4d045e9` (01.11.2026). Берём её или новее.
- **git-bruh/nvidia-musl** (архив 03.01.2026, работа 2021 года, ~470-я ветка):
  gcompat+patchelf хватало для EGL/Wayland; `-Dtls=disabled` для glvnd;
  `_IO_2_1_stdout_` — NULL-стаб; X11 и `libnvidia-tls` не работали. Для 580/620 его рецепт
  **недостаточен** (ifunc/TLS).
- **Alpine**: пакета libglvnd **нет вообще**, mesa собрана с builtin dispatch. Пакетов
  NVIDIA-userspace нет. `nvidia-src` (edge/testing) = **595.45.04, только open-модули
  (Turing+), AKMS**. Версии kernel-модулей и userspace обязаны совпадать до цифры.
- **Ветки**: 580 — последняя для Maxwell/Pascal/**Volta**; 620 — текущая production
  (620.70.06). В PR: 580 использует закрытые `kernel/`, 620 — `kernel-open/`.
- **musl**: `LD_DEBUG` не поддерживает (это glibc-фича); ошибки релокаций печатает в stderr.
  Поддерживает прямой запуск `ld-musl-x86_64.so.1 <prog>` и `LD_TRACE_LOADED_OBJECTS=1`.
- **NVK** (`mesa-vulkan-nouveau` в Alpine есть): поддерживает и Maxwell/Pascal/Volta
  (Mesa ≥25.x), но требует nouveau — **несовместим** с загруженными проприетарными модулями;
  CUDA не даёт. Это альтернативная ветка, не ступень.
- **sgerrand/alpine-pkg-glibc**: жив (не было DMCA), но glibc **2.35** (релиз 04.2023) —
  может не покрыть символы 620. Проверять аудитом, предпочитать свежий glibc-rootfs.
- **ZLUDA**: реимплементация CUDA для не-NVIDIA GPU, а не загрузчик NVIDIA-бинарей.
  Справочный источник, низкий приоритет.
- EULA NVIDIA запрещает RE (кроме разрешённого законом) и распространение модифицированных
  бинарей; использование не ограничено «личным». Скрипты/патчи поверх официально скачанного
  `.run` — стандартная практика (git-bruh, Chimera публикуют именно их).

## Окружение и выбор версий (решение D1)

- Хост: Alpine x86_64 в VM со снапшотом, ядро ≥6.6, headers строго под `uname -r`.
  GPU определить автоматически (`lspci -nn | grep -i nvidia`).
- **D1 — ветка**: Maxwell/Pascal/Volta → **580** (закрытые `kernel/` из `.run`);
  Turing+ → **620** (`kernel-open/`) или 580 как консервативный вариант. Модули и userspace
  — **из одного и того же `.run`**. `nvidia-src` использовать только при осознанном выборе
  userspace 595.45.04; по умолчанию — не использовать.
- Инструменты: `patchelf` ≥ 0.18.0 **достаточно — `--rename-dynamic-symbols` появился в
  0.18.0; пакет Alpine edge `patchelf 0.18.0-r3` годен как есть (Chimera использует 0.19.1)**;
  `gcompat` собрать из git `0660063…`; `readelf`, `strace`, `bwrap`, python3+pyelftools,
  gcc/make/linux-headers, REA: `npx rea-agents@latest setup`.
- Рабочий каталог: `/opt/nvidia-musl-exp/` (`src/`, `musl-nvidia/`, `nvidia/`, `gcompat/`,
  `jail/`, `manifest/`). Манифест + бэкап всего, что трогается вне префикса.
  `uninstall.sh` обязателен с первого дня.

## Фаза 1 — Изучить наработки (строго до запуска REA; отчёт на каждый источник: `report/01-study-<имя>.md`)

**1.1. Chimera — обязательно, первым.** Блог
https://chimera-linux.org/news/2026/11/musl-nvidia.html + PR
https://github.com/chimera-linux/cports/pull/6531 (перечитать текущее состояние,
зафиксировать head-коммит; сверить с Приложением А) + `main/gcompat/template.py` + PR #6504
(glvnd).
- Выписать: оба патча musl и их механику; `libnvcompat.c/.map`; точные patchelf-команды;
  различия 580/620 (kernel/ vs kernel-open/, libstdc++); модель упаковки musl-nvidia
  (системная замена libc!).
- Ответить: что из этого переносится на Alpine как есть, что требует адаптации
  (версия/malloc musl, отсутствие glvnd/ckms/libc++abi, модель замены libc).
- Артефакт: таблица «компонент Chimera → аналог/действие на Alpine».
  → ВЫПОЛНЕНО: `report/01-study-chimera.md` (включая таблицу сверки оператора по живому PR).

**1.2. git-bruh/nvidia-musl.** Разобрать `repo/` (KISS): патчи gcompat, стаб
`_IO_2_1_stdout_`, patchelf-команды, флаги mesa/glvnd. Таблица «блокер → причина → обход →
актуальность для 580/620». Подтвердить `apk search glvnd` (ожидание: пусто) → составить план:
упаковать libglvnd (APKBUILD с нуля или порт из cports) и пересобрать mesa; решить D3:
`-Dtls=disabled` (проще, путь git-bruh) vs TLS-enabled + патч 091 (путь Chimera).
**Важно: для nvidia-smi и CUDA glvnd не нужен — D3 можно отложить до графической ступени.**
→ ВЫПОЛНЕНО: `report/03-study-git-bruh.md` (head `e5cd409c…`, 2021, KISS, драйвер 470.74).
Три ответа: (1) `apk search glvnd` пусто — подтверждено APKINDEX 3.22/3.25 и aports (пакета
`libglvnd` нет вовсе; mesa в Alpine собрана без `-Dglvnd`); (2) патч gcompat на два символа
для 580 не нужен — `_IO_2_1_stdout_` в дереве нет вовсе, `dladdr1` остался только в двух
GL-библиотеках (eglcore/glcore); (3) символьный зазор headless-CUDA против musl 1.2.6-r6 +
gcompat 1.1.0-r4 = **ноль** (`tools/sym-gap.py`), без gcompat — 12 имён, и все 12 gcompat
даёт. LFS64 (`open64`, `__xstat64`, …) резолвит сам загрузчик musl (`get_lfs64`), так что
шим не нужен. D3 остаётся отложенным: `libcuda` среди пяти библиотек с `DF_STATIC_TLS` нет.

**1.3. Гист sineemore (Void).**
https://gist.github.com/sineemore/161e4051b2d03f9e3b0f3e16221463ce — DKMS-пакет: udev-правила,
blacklist nouveau/nova_core/nova_drm, modules-load, nvidia-modprobe из исходников,
GSP-прошивки. Карта порта на Alpine: сборка модулей вручную/akms (не DKMS); udev-правила —
если есть udev/eudev, иначе OpenRC-сервис, выполняющий `nvidia-modprobe -c0 -c255 -m -u`
после загрузки модулей; blacklist → `/etc/modprobe.d/`. Спланировать **fallback-jail**:
bwrap + свежий glibc-rootfs (Ubuntu 24.04/Debian 13 base или распакованный CUDA-образ),
бинды `/dev/nvidia*`, `/proc/driver/nvidia`, `/sys`; альтернатива из гиста — Flatpak
GL-расширения (для графики). Артефакт: пошаговый план fallback (подход **A**).

**1.4. MuslMan.** https://github.com/apiwo/MuslMan — построчный разбор `muslman`: bwrap-флаги
(сборка без сети), извлечение `.run`, раскладка `/opt/muslman`, patchelf interpreter/rpath,
manifest/uninstall. Критика: вендоринг sgerrand glibc **2.35** — сверить с требованиями 620
по аудиту 2.1; при нехватке — свежий rootfs и запуск через `ld-linux-x86-64.so.2
--library-path`. Вердикт «годен / годен с изменениями / не годен» + что заимствуем
(манифест, sandbox-идеи).

**1.5. ZLUDA (опционально, справочно).** https://github.com/vosen/ZLUDA — только механика
drop-in `.so` и обработка CUDA API-поверхности; помнить: это реимплементация, не загрузчик.
Не тратить больше одной сессии.

**Контрольная точка:** отчёт с решениями D1 (ветка), D2 (musl-стратегия, см. 2.0), D3 (glvnd,
можно отложить). База — Chimera-гибрид; fallback — A-jail. Порядок «Фаза 1 → REA» обязателен.

## Фаза 2 — Применить REA (каждая задача — отдельная сессия с логом)

**2.0. Сборка приватного musl-nvidia** (новый обязательный шаг, статика, безопасно).
- Узнать версию musl в Alpine (`apk policy musl`), взять её исходники (с Alpine-патчами и
  Alpine-конфигурацией malloc, **не** chimera-конфигурацию), приложить патчи 090/091 из PR
  (адаптировать при конфликтах), собрать с `-DLIBC_NVIDIA_BUILD`.
- **D2 — стратегия размещения.** По умолчанию: приватная установка в
  `/opt/nvidia-musl-exp/musl-nvidia/lib/` (libc.so + симлинк `ld-musl-x86_64.so.1`),
  NVIDIA-бинарники получают `--set-interpreter` на приватный путь; процессы, которые будут
  dlopen'ить NVIDIA-библиотеки, запускаются через приватный ldso явно. Системная замена musl
  (модель Chimera) — **только с явного одобрения оператора**, с бэкапом в манифест. Помнить:
  libc нельзя подменить внутри уже запущенного процесса.
- Smoke-тест: `/opt/.../ld-musl-x86_64.so.1 /bin/busybox ls` и простой многопоточный тест под
  приватной libc. Артефакт: `report/04-musl-nvidia-build.md` + скрипт сборки.

**2.1. ELF-аудит** (статика, до установки драйвера). Скрипт `tools/elf-audit.py` (pyelftools)
по всем `libnvidia-*.so*`, `libcuda.so*`, `nvidia-smi`, `libcudart*` из распакованного `.run`:
DT_NEEDED; типы релокаций (счёт IRELATIVE/TPOFF64/DTPMOD64); DT_FLAGS/DF_STATIC_TLS;
версионные зависимости и **максимальная требуемая `GLIBC_2.xx`** (вход для выбора glibc в
jail); потребители `libstdc++.so.6`; кандидаты на статические mutex-инициализаторы
(эвристика: glibc-kind в offset 16). REA сводит в приоритизированный список: что проходимо с
musl-nvidia+gcompat+libnvcompat, что требует доработки, что безнадёжно.
Артефакт: `report/02-elf-audit.md`.

**2.2. Форензика загрузки** (динамика). Собрать gcompat из git `0660063…` и
`libnvcompat.so.0` (рецепт в Приложении А). Harness `tools/dlopen-harness.sh`: dlopen каждой
библиотеки **под приватным musl-nvidia**
(`/opt/.../ld-musl-x86_64.so.1 --library-path ... ./harness <lib>`), sanitize-проход patchelf
по Приложению А. Логирование: stderr musl (`Error relocating ...`),
`LD_TRACE_LOADED_OBJECTS=1`, `strace -e trace=openat,mmap,mprotect` (или `%file,%memory`).
**`LD_DEBUG` под musl не работает — использовать только внутри glibc-jail.** REA превращает
логи в конкретные фиксы (символ/зависимость/стаб), итеративно.
Артефакт: `report/03-load-forensics.md`.

**2.3. Гонка за nvidia-smi.** Добиться работы нативно: sanitize + `--set-interpreter` на
приватный musl-nvidia + gcompat/libnvcompat в library-path. Полный рецепт:
`recipes/nvidia-smi.md`.

**2.4. CUDA (растяжение).** Минимальный `cuInit(0)` + `deviceQuery` (cuda-samples). При
падении REA локализует слой (loader → relocation → TLS → symbol → kernel-interface).
Отрицательный результат фиксировать честно. `recipes/cuda.md`.

## Фаза 3 — Лестница успеха (статус каждой ступени: работает / нет / почему)

1. Модули из того же `.run` собраны и загружены (Turing+: GSP-прошивки в
   `/lib/firmware/nvidia/<ver>/`), blacklist оформлен, `/dev/nvidia*` создаются
   (nvidia-modprobe + OpenRC/udev).
2. `nvidia-smi` нативно под musl-nvidia.
3. CUDA: **сначала в glibc-jail** (fallback обязан быть доказан: `cuInit` + `deviceQuery`),
   затем нативно, если возможно.
4. PyTorch с CUDA в jail видит GPU; нативно — stretch goal (весь процесс под приватным
   musl-nvidia).
- **Альтернативная страховка (не ступень):** NVK через `mesa-vulkan-nouveau` — требует выгрузки
  проприетарных модулей и снятия blacklist; CUDA не даёт; годится даже для
  Maxwell/Pascal/Volta на свежей Mesa. Оформить как отдельную ветку `recipes/nvk-fallback.md`
  по желанию.
- Графика (GL/EGL/Vulkan через glvnd) — отдельная опциональная ступень после D3.

## Ограничения и безопасность

- Не распространять патченные бинарники NVIDIA и вендоренный glibc; только скрипты/патчи
  поверх официального `.run` (проверять sha256; для 580.196.07/620.70.06 x86_64 — из
  Приложения А).
- Всё в VM/снапшоте; инсталлятор NVIDIA не запускается (только `--extract-only`); системные
  каталоги не трогаются без записи в манифест и бэкапа; замена системного musl — отдельный
  gated-шаг (D2).
- Правило останова: две подряд нерешённые ступени → откат на A-jail, время не жечь.
- Отрицательные результаты — результат. Ничего не маскировать.

## Критерии приёмки

- `report/01-study-*` (все источники Фазы 1, до REA), `report/02-elf-audit.md`,
  `report/03-load-forensics.md`, `report/04-musl-nvidia-build.md`.
- Работающие `tools/elf-audit.py`, `tools/dlopen-harness.sh`, скрипт сборки musl-nvidia,
  patchelf-рецепты, `install.sh` (прозрачные шаги, префикс `/opt/nvidia-musl-exp`) +
  `uninstall.sh` + манифест.
- Итоговая статус-таблица лестницы + финальный рецепт для Alpine.

## Приложение А — известная карта из PR #6531 (не переоткрывать; сверить с текущим PR)

- PR: Draft, автор q66, head-коммит **`c26532983c3b811c2d6b310058761bba39491bbe`**
  (11.11.2026). PR ещё меняется — перечитать на старте.
- Версии и sha256 (привязка к архитектуре, не путать):
  - nvidia580 = 580.196.07: **x86_64 `0aab0518db53cf73c412dc7f32bbc850ca95e839d8923d0de4bff42c1c9169ce`**,
    aarch64 `5b7d23…`
  - nvidia620 = 620.70.06: **x86_64 `29118e3abd08c07559bcca3d9c76238d039490af0362a2e5bcfcbe7204afd4b1`**,
    aarch64 `3fa65a…`
  - вариант без 32-битного кода (предпочтителен для LFS-образа, `gpu-stack` 1.5):
    `NVIDIA-Linux-x86_64-580.196.07-no-compat32.run`, 325 784 082 байта (полный —
    397 209 845), x86_64 **`2189c47fa87ae45b7a166b58c3742dd1629035069cb0fb4103ed375229c55867`**.
    Внутри 72 ELF вместо 107, каталога `32/` нет (замер 14.11.2026, `report/03` п. 6).
  - Hardening `!pie`.
- Sanitize каждой `.so`/бинарника:
```
patchelf --remove-needed ld-linux-x86-64.so.2 \
  --remove-needed libdl.so.2 --remove-needed libm.so.6 \
  --remove-needed libpthread.so.0 --remove-needed librt.so.1 \
  --replace-needed libc.so.6 libc.so \
  --add-needed libnvcompat.so.0 \
  --rename-dynamic-symbols libnvcompat.map <file>
```
- Бинарники: дополнительно `--set-interpreter <путь к musl-nvidia ldso>`; SONAME-симлинки по
  `patchelf --print-soname`.
- `libnvcompat.map`: `pthread_mutex_lock → __nvcompat_mutex_lock`,
  `pthread_mutex_timedlock → __nvcompat_mutex_timedlock`,
  `pthread_mutex_trylock → __nvcompat_mutex_trylock`.
  Сборка: `cc libnvcompat.c -o libnvcompat.so.0 -nodefaultlibs -shared -Wl,-soname,libnvcompat.so.0
  -Wl,--no-as-needed -l:libgcompat.so.0`. Исходник брать из PR
  (`user/nvidia580/files/libnvcompat.c`).
- Патч 090 (ifunc): из Alpaquita `fd3089b3`, под `LIBC_NVIDIA_BUILD`; резолверы не вызываются
  в ldd_mode; собственные ifunc ldso — stage 2; dlsym резолвит ifunc.
- Патч 091 (TLS): `STATIC_TLS_PREALLOC 256`; при dlopen `DF_STATIC_TLS` + align ≤
  MIN_TLS_ALIGN + влезает в резерв → статический блок; иначе dynamic; static-образ копируется
  во все существующие потоки.
- 620: `libnvidia-api.so.1` + libstdc++ → на Alpine `apk add libstdc++` (предпочтительно)
  либо `--replace-needed libstdc++.so.6 libc++abi.so.1`.
- Kernel: 580 → `kernel/`, 620 → `kernel-open/`; сборка
  `make NV_EXCLUDE_BUILD_MODULES='' KERNEL_UNAME=$(uname -r) modules`
  (модули nvidia, nvidia-drm, nvidia-modeset, nvidia-uvm). Blacklist:
  `nouveau, nova_core, nova_drm`.
- Открытые компоненты собирать нативно из GitHub-тегов NVIDIA: nvidia-modprobe (setuid 4755),
  nvidia-persistenced (libtirpc), nvidia-settings, nvidia-xconfig, egl-wayland 1.1.24,
  egl-gbm 1.1.4.
- gcompat: git `06600632096b14e03e32b234ec0b02a9d4d045e9` (Adélie), даёт `libgcompat.so.0`
  и симлинки glibc-имён.

## Приложение Б — источники

Блог Chimera (07.11.2026): chimera-linux.org/news/2026/11/musl-nvidia.html · PR:
github.com/chimera-linux/cports/pull/6531 (Draft) · glvnd-переход: PR #6504 · gcompat:
git.adelielinux.org/adelie/gcompat · git-bruh: github.com/git-bruh/nvidia-musl (архив) ·
Void: gist.github.com/sineemore/161e4051b2d03f9e3b0f3e16221463ce · MuslMan:
github.com/apiwo/MuslMan · ZLUDA: github.com/vosen/ZLUDA · отказ NVIDIA:
github.com/NVIDIA/nvidia-installer/issues/10 · 580 last for Maxwell/Pascal/Volta:
phoronix.com/news/NVIDIA-580-Linux-Driver-Last-HW · nvidia-src:
pkgs.alpinelinux.org/package/edge/testing/x86_64/nvidia-src · REA: rea.tools
