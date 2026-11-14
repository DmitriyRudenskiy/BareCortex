# 05 — MuslMan: построчный разбор, замеренная критика, вердикт

Замер: 14.11.2026, локальный клон `github.com/apiwo/MuslMan` + `(build)` (`192.168.0.86`).

Репозиторий: создан **2026-07-30**, последний пуш **2026-08-04**, **4 коммита**, один автор
(`apiwo`), 0 звёзд, 0 issues (в т. ч. закрытых), лицензия MIT, **нет CI и нет тестов**
(`ls -a` → `.git .gitignore config lib LICENSE muslman README.md`). Объём: **1239 строк**
в `muslman` + `lib/*.sh` (1299 с семью шаблонами). То есть это пятидневная вспышка,
не тронутая три месяца и ни разу не проверенная чужими руками.

Проверялось: весь код (`muslman`, `lib/{common,distro,sandbox,driver,kmod,userspace,
glibc-compat,egl-integration,install,status,doctor,uninstall}.sh`, `config/templates/*`),
плюс независимые замеры по payload 580.196.07 и **620.70.06** (скачан, сверен sha256,
распакован), musl 1.2.6 (`ldso/dynlink.c`), gcompat 1.1.0-r4 (apk), sgerrand glibc
(github API), egl-wayland 1.1.18 (исходники).

## 1. Что он делает (архитектура по коду)

`muslman_install` (`lib/install.sh:71`) — шесть шагов:

| шаг | код | суть |
|---|---|---|
| 1 версия + `.run` | `lib/driver.sh` | версия из `https://download.nvidia.com/XFree86/Linux-x86_64/latest.txt` (можно `--driver-version` / `MUSLMAN_DRIVER_FILE`), качается в `~/.cache/muslman`, `sh <run> --extract-only --target` |
| 2 распаковка | `driver_extract` | туда же, `payload-$ver` |
| 3 модули ядра | `lib/kmod.sh` | `kernel-open/`, иначе `kernel/`; в песочнице `make -j$(nproc) modules SYSSRC=/kbuild IGNORE_CC_MISMATCH=1`, `/kbuild` = `--ro-bind /lib/modules/$(uname -r)/build`; `.ko` → `/lib/modules/$(uname -r)/extra/muslman` + `depmod -a` |
| 4 userspace | `lib/{userspace,glibc-compat}.sh` | allowlist из payload (20 glob + 6 бинарников + `nvidia_drv.so`) → `patchelf --set-interpreter/--set-rpath` на вендорский glibc → `/opt/muslman/nvidia/{lib,bin,xorg-modules}` |
| 5 EGL-клей | `lib/egl-integration.sh` | `NVIDIA/egl-wayland 1.1.18` и `NVIDIA/egl-gbm 1.1.2` собираются meson/ninja **под musl** (без glibc) |
| 6 конфиги | `install_configs` | modprobe.d, xorg.conf.d, `10_nvidia.json`, `nvidia_icd.json`, udev-правила, `/etc/modules-load.d` (systemd/openrc/dinit), **дописывается строка в `/etc/ld-musl-x86_64.path`** |

Песочница (`lib/sandbox.sh`): Alpine **3.20.3** minirootfs в `~/.cache/muslman/sandbox-rootfs`;
два входа — `sandbox_exec_net` (только один `apk add` зависимостей) и `sandbox_run_offline`
(**`--unshare-net`**) для каждой сборки/`patchelf`. Флаги:
`--unshare-pid --unshare-ipc --unshare-uts --unshare-cgroup [--unshare-net] --die-with-parent
--bind <rootfs> / --proc /proc --dev /dev --tmpfs /tmp --bind <workdir> /work --chdir /work
--setenv HOME=/root --setenv PATH=…`. Deps: `gcc make musl-dev linux-headers patchelf meson
ninja pkgconf wayland-dev wayland-protocols libdrm-dev mesa-dev eudev-dev tar xz curl git perl`.

## 2. Заимствуем (семь вещей, все — с указанием, куда ложатся)

1. **`detect_libc()`** (`lib/distro.sh:12`) — быстрый путь `ls /lib/ld-musl-*.so.1`, надёжный
   `ldd "$(command -v sh)" | grep ld-musl`, а не разбор баннера `ldd --version` (Gentoo-патченный
   `ldd` печатает «ldd (Gentoo …)» без «GNU» → ложный `unknown`). Заменяет наш вариант из
   отчёта 01 §1.1.
2. **`/etc/ld-musl-<arch>.path`** (`musl_ld_path_file`) — штатный musl-аналог `ld.so.conf`:
   `ldso/dynlink.c:1141-1143` читает `"%.*s/etc/ld-musl-" LDSO_ARCH ".path"`. Это правильный
   способ добавить каталог драйвера (вместо `LD_LIBRARY_PATH`, который мы ругали в 01 §1.3).
3. **Манифест + бэкапы** (`lib/common.sh:47-104`) — `manifest` = строки `file|dir|line-append
   <path>`, `install_managed_file` бэкапит **один раз** в `<path>.muslman-backup`,
   `append_line_once` идемпотентно дописывает строку и помнит её для точного отката,
   `uninstall` разворачивает манифест задом наперёд (`tac`). Берём как есть, это ровно то,
   чего не хватает нашему установщику.
4. **Двухфазная песочница + штампы** — сеть только на `apk add`, `--unshare-net` на любой
   компиляции; файлы-штампы `.sandbox-bootstrapped` / `.sandbox-builddeps-installed`;
   проверки скачанного (`size > 1 МБ`, `gzip -t`, у `.run` — `head -c2 == '#!'`). Готовый
   скелет для нашего jail из отчёта 04 §5.
5. **`doctor` как advisory** (`lib/doctor.sh`) — проверяет libc, arch, bwrap,
   curl/tar/depmod/modprobe/lsmod, дерево заголовков ядра, `lspci` на NVIDIA; **ничего не
   ставит на хост**, а печатает готовую команду с учётом определённого пакетного менеджера.
6. **`render_template` с `@@PREFIX@@`** (`sed "s#…#…#g"`) — префикс подставляется в шаблоны,
   а не хардкодится в коде.
7. **dinit-юнит** (`config/templates/dinit-service.tmpl`) — авторы знают конвенцию Chimera:
   включение через симлинк в `/etc/dinit.d/boot.d/`. Нам пригодится без правок.

## 3. Критика. Каждое утверждение — с проверкой

### C1. Посылка про gcompat не выдерживает замера

README §3: «a minimal shim (like gcompat) **doesn't cover nearly enough** of the glibc symbol
surface for something as large as the GL/EGL/Vulkan/CUDA stack». Наш замер (отчёт 03 §3,
сегодня перепроверен на **620.70.06**): против musl 1.2.6-r6 + gcompat 1.1.0-r4 во всём
64-битном дереве не закрывается **только `dladdr1` (2 файла) и `__popcountdi2` (2 файла)**;
всё остальное (`GLib/Pango`, `X11`, `GL/EGL`, `libstdc++`, X-интерналс) — отсутствие
**пакетов**, а не libc-символов. Ради двух символов MuslMan тащит вендорский glibc и
перелинковывает каждый бинарник.

### C2. gcompat уже закрывает `PT_INTERP` — патчить нечего

В `gcompat-1.1.0-r4.apk` (распаковали, смотрим содержимое):

```
lib/ld-linux-x86-64.so.2   # 22 728 Б, ELF pie executable, static-pie, без DT_NEEDED
lib/libc.so.6 -> libgcompat.so.0
lib/libm.so.6 -> libgcompat.so.0   # и ещё libpthread/librt/libutil/libresolv/libcrypt
```

Апстрим-README gcompat: «*gcompat* additionally provides a **loader stub** … when a binary is
run that uses glibc as its dynamic linker, the stub will run, **redirecting it to use musl and
automatically preloading the gcompat library**». То есть на любом musl-дистрибутиве с gcompat
(Alpine/Void/Chimera) у штатного glibc-бинарника NVIDIA интерпретатор существует и работает,
а `DT_NEEDED libc.so.6` резолвится. MuslMan при этом **сам же** пишет `/etc/ld-musl-x86_64.path`
— т. е. rpath можно было решить той же строкой. Итог: 2,8 МБ вендоримого glibc + `patchelf`
по каждому файлу дублируют ~700 КБ пакета из репозитория базы.

### C3. Вендоринг glibc 2.35-r1 (апрель 2023) — без sha256 и без подписи

Замеры: `sgerrand/alpine-pkg-glibc` — последний релиз **2.35-r1, 2023-04-13**, последний пуш
репозитория **2024-06-24**, 49 открытых issues. В коде MuslMan **ни одного** `sha256`/`gpg`
(единственное вхождение слова «signature» — комментарий про формат `.apk`); проверка скачанного
— только `gzip -t`. Мы зафиксировали контрольную сумму, чтобы её было на что повесить:
`glibc-2.35-r1.apk` = **2 826 689 Б**, sha256
`276f43ce9b2d5878422bca94ca94e882a7eb263abe171d233ac037201ffcaf06`.

Дополнительно: это **вторая полноценная libc** в системе (и в процессе — рядом с musl), тогда
как gcompat — тонкий слой поверх musl. Для образа с инвариантом «только amd64, только база»
это ещё и отдельный трек CVE: ветка 2.35 снята с поддержки, 2023–2026 не патчены.

### C4. `patchelf` запекает путь песочницы: установленные бинарники не могут запуститься

`lib/userspace.sh:92-93`:

```sh
patchelf --set-interpreter /glibc-compat/lib/ld-linux-x86-64.so.2 \
         --set-rpath "/glibc-compat/lib:/glibc-compat/usr/lib:$ORIGIN:$ORIGIN/../lib" "$f"
```

`/glibc-compat` — это **точка монтирования внутри bwrap** (`--ro-bind "$glibc_compat"
/glibc-compat`, строка 102). Через две функции `userspace_install` копирует те же файлы в
`/opt/muslman/nvidia/`, второго прохода `patchelf` нет, и `grep -rn "/glibc-compat"` по всему
репозиторию не находит ни создания `/glibc-compat` на хосте, ни симлинка. `${MUSLMAN_PREFIX:-…}`
не спасает: и при `MUSLMAN_PREFIX=`, и при unset получается `/opt/muslman` (проверено в bash).
Следствие: `muslman status` (`lib/status.sh:42`) зовёт `/opt/muslman/nvidia/bin/nvidia-smi` →
`execve` падает на отсутствующем интерпретаторе. Проверить на живой машине — десять секунд:
`readelf -l /opt/muslman/nvidia/bin/nvidia-smi | grep interpreter`. Сам installer здесь запустить
нечем (нужны root + bwrap + musl-хост), поэтому утверждение статическое, но цепочка короткая:
три функции, ни одной ветки, где путь становится хостовым.

### C5. Allowlist userspace уже, чем нужно: 42 файла из 105

Скриптовый сверкой `USERSPACE_LIB_GLOBS` с деревом 580.196.07: забирается **42** ELF,
пропускается **63**. Из пропущенного значимого (не `32/`, не утилиты установщика):

- `libnvidia-egl-xcb.so.1`, `libnvidia-egl-xlib.so.1` — EGL-платформы X11 (в 620 — 1.0.6);
- `libnvidia-wayland-client.so`, `libnvidia-egl-wayland2.so.1.0.2` (**новое в 620**);
- `libnvcuvid.so` (декод), `libvdpau_nvidia.so` (VDPAU), `libnvidia-opencl.so`,
  `libnvidia-ngx.so` (DLSS/NGX), `libnvidia-gpucomp.so`, `libnvidia-nvvm70.so.4`,
  `libnvidia-vksc-core.so`, `libcudadebugger.so`, `libglxserver_nvidia.so` (GLX под XWayland),
  `libnvidia-api.so.1`, `libnvidia-sandboxutils.so`, `libnvidia-pkcs11*.so`, `libnvidia-present.so`;
- `nvidia-settings`, `nvidia-powerd`, `nvidia-xconfig`, `nvidia-pcc`;
- `firmware/gsp_ga10x.bin`, `firmware/gsp_tu10x.bin` — open-модуль на Turing+ просит GSP-прошивку
  (`kernel-open/common/inc/nv-firmware.h`); MuslMan её ни ставит, ни проверяет (спасает только
  `linux-firmware`, и то не для любой версии драйвера).

Молчание про `32/` здесь правильно по существу (musl multilib не бывает, наш `-no-compat32.run`
о том же), но README об ни слова.

### C6. glvnd-конфиги переписываются руками вместо `install` готовых из payload

В payload **обеих** веток лежат `10_nvidia.json`, `10_nvidia_wayland.json`, `15_nvidia_gbm.json`,
`20_nvidia_xcb.json`, `20_nvidia_xlib.json`, `nvidia_icd.json`, `nvidia_icd_vksc.json`,
`nvidia_layers.json` (+ в 620 ещё `09_nvidia_wayland2.json`). MuslMan из шаблонов пишет **только
два** — `10_nvidia.json` и `nvidia_icd.json`. Значит `libnvidia-egl-wayland.so.1`, которую он
ради «Sway из коробки» собирает из исходников, glvnd'ом **не загружается**: для этого нужен
`10_nvidia_wayland.json` (`"library_path": "libnvidia-egl-wayland.so.1"`), и его не ставит никто —
ни meson, ни autotools egl-wayland (в `meson.build` 1.1.18 нет `install_data` для json, в
`Makefile.am` в `dist_pkgdata_DATA` только XML-протоколы). Ставить обязан пакет/установщик.
`vulkan-icd.json.tmpl` в целом верен — NVIDIA и сама пишет `"library_path": "libGLX_nvidia.so.0"`
(я это проверил на `nvidia_icd.json` из payload; моё первое подозрение про
`libnvidia-vulkan-producer.so` **не подтвердилось**), — но `api_version` застыл на `1.3.0`
против `1.4.312` у NVIDIA.

Наша ленивая замена: не писать шаблоны, а ставить NVIDIA-ны json из payload, переписав
`library_path` на абсолютный в наш префикс. Это заодно закрывает gbm/xcb/xlib/wayland2/vksc/layers.

### C7. Собираемый клей старше драйверного

MuslMan пинит `egl-wayland 1.1.18` / `egl-gbm 1.1.2`, а payload 580.196.07 поставляет
**1.1.20** / **1.1.3** (620 — те же 1.1.20/1.1.3 плюс xcb/xlib 1.0.6 и wayland2 1.0.2).
Сборка более старого клея к более новому драйверу — MTBSI; при этом DT_NEEDED поставляемого
`libnvidia-egl-wayland.so.1.1.20` = `libwayland-client/-server`, `libdrm` (всё есть в базе),
так что патчить его не сложнее, чем собирать.

### C8. Песочница — гигиена, а не граница безопасности

`--unshare-user` нет, `--dev /dev` (внутри реальный хостовый `/dev`, включая `nvidia*`),
`--bind <rootfs> /` при uid 0. Для «компилятор не пишет в хостовый rootfs» — достаточно; для
«сборка не может навредить машине» — нет. Наш jail: `--tmpfs /dev` + `--dev-bind` только нужных
узлов, `--unshare-user` где позволяет база.

### C9. Модули: всегда `kernel-open`, расхождение компилятора заглушено

`driver_kernel_source_dir` безусловно предпочитает `kernel-open/`. Приложение А (Chimera PR)
нашей же проверкой зафиксировано: **580 — закрытый `kernel/`, `kernel-open/` только для 620**.
`IGNORE_CC_MISMATCH=1` молча снимает защиту от сборки модуля не тем компилятором, а `--ro-bind`
дерева сборки ломается на ядрах, которым надо писать в своё дерево (сами признают «works on
Alpine/Void headers»). GSP-прошивка — см. C5.

### C10. Мелочи, которые всё равно придётся чинить

`uninstall` зависит от GNU `tac`; откат `line-append` делает `sed -i '\#^…$#d'` и удаляет **все**
совпадающие строки, включая те, что пользователь вписал сам; `$(render_template …)` съедает
завершающий перевод строки у всех конфигов; `dir`-записи манифеста при удалении игнорируются —
вместо них `rm -rf $MUSLMAN_PREFIX`, то есть уедет всё, что пользователь положил в `/opt/muslman`;
`status` не требует root и не различает «модули не собраны» и «модули не загружены».

## 4. Вердикт

**Не годен** как база и как источник рецептов для нашей ветки.

Причины по убыванию веса: (1) статически неработоспособный шаг 4 — `PT_INTERP`/`RPATH` на
песочничную `/glibc-compat` (C4); (2) заявленный результат «Wayland/EGL из исходников» не
доведён до glvnd — json не ставятся (C6); (3) вся конструкция с вендорским glibc решает задачу,
которой нет: gcompat закрывает и `PT_INTERP`, и `libc.so.6`, а символьный зазор — два символа
(C1, C2), при этом glibc EOL и скачан без контрольной суммы (C3); (4) allowlist режет треть
полезных библиотек и прошивки (C5); (5) ноль тестов, ноль CI, ноль чужих прогонов, четыре
коммита (вводка).

Заимствуем ровно семь вещей из §2 — `detect_libc`, `/etc/ld-musl-<arch>.path`, манифест с
бэкапами и `append_line_once`, двухфазную песочницу со штампами и проверками скачанного,
advisory-`doctor`, `@@PREFIX@@`-шаблоны, dinit-юнит с `boot.d`. Ни одной строчки из
`glibc-compat.sh`, `userspace.sh` и `egl-integration.sh` — их заменяют gcompat (отчёт 03) и
NVIDIA-ны конфиги из payload.

## 5. Влияние на решения D1/D2

- **D2 (musl-стратегия).** MuslMan — это **подход A, замаскированный под B**: он не делает
  userspace мусловским, он прячет рядом glibc. Аргументом «значит полный musl возможен» он не
  работает. Честный B — gcompat + два символа (C1/C2), и он уже посчитан в отчёте 03.
- **D1 (ветка 580 vs 620).** С символьной точки зрения ветки одинаково легки: максимум
  требования — **GLIBC_2.26** в обеих (проверено `readelf -V` по всему 64-битному дереву),
  зазор против musl+gcompat одинаковый — `dladdr1` в двух файлах. Значит D1 надо решать не
  «потянет ли musl», а по kernel-ветке (580 → закрытый `kernel/`, 620 → `kernel-open/` + новый
  EGL-платформенный слой wayland2) и по доступности версий в нашей базе.
- **Требование TASK «сверить sgerrand 2.35 с требованиями 620»**: 2.35 ≥ 2.26 → **хватает**,
  «при нехватке свежий rootfs и `ld-linux-x86-64.so.2 --library-path» не требуется. (Механика
  при этом живая и полезная: `/lib/ld-linux-x86-64.so.2` в системе уже есть из gcompat — но как
  stub, а не как настоящий загрузчик, так что `--library-path` придётся прогонять через jail.)

## 6. Проверка ожиданий задачи 1.4

| требует TASK | где выполнено |
|---|---|
| bwrap-флаги (сборка без сети) | §1 (полный список), C8 (что флаги не дают) |
| извлечение `.run` | §1 шаг 2 (`--extract-only --target`), C5 (что берут из payload) |
| раскладка `/opt/muslman` | §1 (префикс, `nvidia/{lib,bin,xorg-modules}`, `glibc-compat`), C4 |
| patchelf interpreter/rpath | C4 (запечатали песочницу), C2 (патчить нечего) |
| manifest/uninstall | §2.3 (берём), C10 (что чинить) |
| sgerrand glibc 2.35 vs требования 620 | C3 + §5: 620 требует максимум GLIBC_2.26 → хватает |
| вердикт + что заимствуем | §4 |

## 7. Воспроизведение

```sh
# репозиторий и его метаданные
git clone https://github.com/apiwo/MuslMan && cd Muslman && wc -l muslman lib/*.sh
curl -s https://api.github.com/repos/apiwo/MuslMan | jq '{created_at,pushed_at,stargazers_count,open_issues_count}'
curl -s 'https://api.github.com/repos/apiwo/MuslMan/commits?per_page=100' | jq 'length'
curl -s https://api.github.com/repos/sgerrand/alpine-pkg-glibc/releases | jq '.[0] | {tag_name,published_at}'

# glibc-требования payload (580 и 620) — максимум по дереву
curl -fL -O https://download.nvidia.com/XFree86/Linux-x86_64/620.70.06/NVIDIA-Linux-x86_64-620.70.06.run
sha256sum NVIDIA-Linux-x86_64-620.70.06.run   # 29118e3a…afd4b1 = пин из Приложения А
sh NVIDIA-Linux-x86_64-620.70.06.run --extract-only --target ex620
find ex620 -maxdepth 1 -type f | while read f; do
  head -c4 "$f" | grep -qU "$(printf '\x7fELF')" && readelf -V "$f" 2>/dev/null
done | grep -oE 'GLIBC_[0-9]+\.[0-9]+(\.[0-9]+)?' | sort -u -V | tail -3      # → GLIBC_2.26

# символьный зазор 620 против musl 1.2.6-r6 + gcompat 1.1.0-r4 (скрипт из отчёта 03)
mkdir -p /tmp/nv620/ex && ln -s ~/.cache/nv580/ex620 /tmp/nv620/ex/NVIDIA-Linux-x86_64-620.70.06
ln -s ~/.cache/nv580/symgap /tmp/nv620/symgap && NV580=/tmp/nv620 python3 ~/.cache/nv580/sym-gap.py

# gcompat: чем он закрывает PT_INTERP и libc.so.6
tar -tzf ~/.cache/nv580/symgap/gcompat-1.1.0-r4.apk | grep -E 'ld-linux|libc'
readelf -h ~/.cache/nv580/symgap/lib/ld-linux-x86-64.so.2 | grep -E 'Type|Machine'
strings ~/.cache/nv580/symgap/lib/ld-linux-x86-64.so.2 | grep gcompat   # «gcompat ELF interpreter stub»

# musl читает /etc/ld-musl-<arch>.path
curl -O https://musl.libc.org/releases/musl-1.2.6.tar.gz && tar -xzf musl-1.2.6.tar.gz
grep -n 'ld-musl-' musl-1.2.6/ldso/dynlink.c            # 1141-1143
grep -n 'versym\[i\] >= 0' musl-1.2.6/ldso/dynlink.c    # 264, 283 — резолвинг только по имени

# sha256 вендорского glibc (в MuslMan не проверается)
curl -fLO https://github.com/sgerrand/alpine-pkg-glibc/releases/download/2.35-r1/glibc-2.35-r1.apk
sha256sum glibc-2.35-r1.apk    # 276f43ce9b2d5878422bca94ca94e882a7eb263abe171d233ac037201ffcaf06
```
