# 04 — Гист sineemore «NVIDIA on musl»: разбор и карта порта на Alpine

Замер: 14.11.2026, `(build)` (`192.168.0.86`).

Источник: <https://gist.github.com/sineemore/161e4051b2d03f9e3b0f3e16221463ce> — создан
2026-04-19, обновлён 2026-05-03, два коммита, два файла: `README` (1108 Б) и
`nvidia-dkms-standalone-580.126.18.patch` (7041 Б, «New package», из коммита `bc7e85c`).
Патч добавляет в void-packages один пакет `nvidia-dkms-standalone`: `template` + пять
файлов (`dkms.conf`, `99-nvidia.rules`, `nvidia-modprobe.conf`, `nvidia-modules-load.conf`,
`nvidia-tegra-bpmp.patch`). Версия драйвера — **580.126.18**, то есть та же ветка 580,
что и наша 580.196.07.

## 1. Тезис гиста (его же словами)

> kernel modules are libc-independent already, userspace nvidia libraries can live in
> glibc Flatpak/chroot/container

и отсюда вся конструкция: `nvidia-dkms-standalone` + бинарник `nvidia-modprobe` из
апстрим-исходников — остальное userspace в glibc-окружении. Это ровно **подход A**
из нашего плана, и он подтверждён чужой практикой на musl-дистрибутиве, а не выдуман
нами. Для нас это значит: ядро и `nvidia-modprobe` делаем по-мусловски, а закрытые
библиотеки (`libcuda` и т. д.) туда вообще не тащим.

`archs="x86_64 x86_64-musl"` в одном шаблоне — тот же пакет собирается и под musl, и под
glibc; `nopie=yes` (совпадает с «Hardening `!pie`» из Приложения А); `repository="nonfree"`.

## 2. Карта порта Void → Alpine 3.25

| у sineemore (Void) | что это | на Alpine 3.25 |
|---|---|---|
| `dkms.conf` + `triggers="dkms initramfs-regenerate"` | DKMS-сборка модулей при установке | **akms 0.3.0-r0**: файл называется `AKMBUILD` в `/usr/src/<mod>-<ver>/`, обязательные переменные `built_modules modname modver` (+`makedepends`); ядро берётся из `/usr/src/linux-headers-$kernelver` (`linux-headers 7.2.1-r0`); сборка идёт через `/usr/libexec/akms/akms-build`, который по умолчанию зовёт `make -C "$kernel_srcdir" M="$builddir" modules`, а при наличии функции `build()` зовёт её |
| `MAKE[0]="'make' -j… NV_EXCLUDE_BUILD_MODULES='' KERNEL_UNAME=${kernelver} modules"` | обход «DKMS передаёт KERNELRELEASE» + список модулей | в akms нет подстановки `${kernelver}`, и дефолтный `make -C … M=` не передаёт `KERNEL_UNAME`/`SYSSRC` → **нужна своя `build()`** (ниже) |
| `files/nvidia-modprobe.conf` → `/usr/lib/modprobe.d` | `blacklist nouveau/nova_core/nova_drm` | тот же путь, тот же синтаксис (kmod 34.3-r0) |
| `files/nvidia-modules-load.conf` → `/usr/lib/modules-load.d` | `nvidia`, `nvidia-drm` | тот же путь; OpenRC-инициалка `modprobe.load` читает его же |
| `files/99-nvidia.rules` → `/usr/lib/udev/rules.d` | создание нод при загрузке модуля + права | **пути совпадают**: eudev 3.2.14-r8 смотрит `/usr/lib/udev/rules.d` и `/etc/udev/rules.d` (проверено в пакете), `udevadm` в `/bin` и `/sbin` |
| `vinstall … 4755 usr/bin` (nvidia-modprobe) | setuid-бинарник | то же; `chmod 4755` |
| `firmware/gsp_ga10x.bin`, `gsp_tu10x.bin` → `/usr/lib/firmware/nvidia/<ver>` | GSP-прошивки | оба файла есть в нашем `.run` (проверено); в Alpine есть `linux-firmware-nvidia 20261019-r0` — но **свои** прошивки из `.run` точнее привязаны к версии драйвера, берём их |
| `hostmakedepends="tar zstd m4"`, `makedepends="kernel-libc-headers"` | зависимости сборки | `tar`, `zstd`, `m4`, `linux-headers`, `build-base` (akms по умолчанию `makedepends="build-base"`) |
| `flatpak install org.freedesktop.Platform.GL.nvidia-580-126-18` | графика через Flatpak GL-расширения | **в Alpine 3.25 flatpak отсутствует** (проверено листингом `main/x86_64/`: 6052 пакета, `flatpak*.apk` нет; есть только `v3.22/community/flatpak-1.16.1-r0.apk` и `edge/community/flatpak-1.18.4-r0.apk`) → на нашей базе этот путь закрыт, остаётся bwrap (`bubblewrap 0.13.1-r0`) |
| `patches/nvidia-tegra-bpmp.patch` | закрыть `#include <soc/tegra/bpmp*.h>` в `kernel/nvidia/nv-clk.c` | **нужен и нам**: в 580.196.07 те же include по-прежнему без `#if IS_ENABLED(CONFIG_TEGRA_BPMP)` (строки 30–31), а `soc/tegra/*` в `linux-headers` Alpine отсутствует |

`AKMBUILD` для нашей версии (следует из `akms-build`: env `srcdir builddir kernel_ver
kernel_srcdir`):

```sh
modname=nvidia
modver=580.196.07
built_modules="nvidia nvidia-uvm nvidia-modeset nvidia-drm"
makedepends="linux-headers m4 zstd"
build() {
    patch -Np1 -i "$srcdir/nvidia-tegra-bpmp.patch" || return 1
    make -j"$(nproc)" SYSSRC="$kernel_srcdir" KERNEL_UNAME="$kernel_ver" \
         NV_EXCLUDE_BUILD_MODULES='' modules
}
```

Список модулей: в `.run` 580.196.07 лежат `common nvidia nvidia-drm nvidia-modeset
nvidia-peermem nvidia-uvm`. Гист ставит четыре; `nvidia-peermem` — RDMA/GLDRBD-хук, нам
не нужен (в `built_modules` не включаем). `kernel/dkms.conf` внутри `.run` — **шаблон**
(`__VERSION_STRING`, `__EXCLUDE_MODULES`, `__DKMS_MODULES` подставляет `nvidia-installer`
в рантайме), поэтому свой `AKMBUILD` пишем с нуля, а не правим.

## 3. Проверено сегодня

1. **Все семь исходников `nvidia-modprobe` 580.196.07 компилируются против заголовков
   musl 1.2.6-r6 из Alpine 3.25**: `gcc -fsyntax-only -nostdinc -isystem
   <musl-dev>/usr/include …` даёт OK для `nvidia-modprobe.c`,
   `modprobe-utils/{nvidia-modprobe-utils,pci-sysfs}.c`,
   `common-utils/{common-utils,nvgetopt,msg,gen-manpage-opts-helper}.c`
   (с генерируемым `version.h` и `-DPROGRAM_NAME`, как делает `utils.mk`).
   Тег `580.196.07` на GitHub отдаётся (HTTP 200), `BIN_LDFLAGS` в `Makefile` —
   строка 118, то есть команда гиста `make PREFIX=/usr BIN_LDFLAGS=… STRIP_CMD=true`
   к нашей версии применима.
2. **Почему бинарник из `.run` не грузится, а исходник — грузится.** Бинарник
   `nvidia-modprobe` ссылает `__strdup`; в glibc это `T __strdup` и `W strdup` по одному
   адресу, в musl — только `T strdup` (замер `nm -D`). В апстрим-исходниках вызова
   `__strdup` нет вовсе, есть обычный `strdup` (`common-utils/msg.c`, `nvgetopt.c`).
   Значит сборка из исходников снимает зависимость без всякого gcompat. Почему glibc-
   сборка NVIDIA выдала именно `__strdup` — не устанавливал.
3. **`nvidia-persistenced` собрать нечем**: `nvidia-persistenced-580.196.07.tar.gz` —
   404, а `nvidia-persistenced-init.tar.bz2` внутри `.run` содержит только
   `install.sh` + шаблоны systemd/upstart/sysv, без исходников. persistenced берём
   бинарником: ему от gcompat нужны ровно `__cmsg_nxthdr` и `__strdup`.
4. **GSP-прошивки** (`gsp_ga10x.bin`, `gsp_tu10x.bin`) присутствуют в `.run` 580.196.07 —
   как и ожидает гист.
5. Пакеты Alpine 3.25 для плана: `bubblewrap 0.13.1-r0`, `eudev 3.2.14-r8`,
   `akms 0.3.0-r0`, `linux-headers 7.2.1-r0`, `linux-firmware-nvidia 20261019-r0`,
   `patchelf 0.18.0-r3`, `gcompat 1.1.0-r4`, `kmod 34.3-r0`, `openrc 0.63.2-r1`,
   `mesa 26.2.6-r0`. Отдельного репозитория community в 3.25 нет
   (`v3.25/community/` пуст, `…/community/x86_64/APKINDEX.tar.gz` — 404; в 3.22 ещё есть).

## 4. Критика (что в порту делаем иначе)

- **`MODE="0666"` на всех `nvidia*`-нодах.** У гиста `KERNEL=="nvidia[0-9]*|nvidiactl|
  nvidia-modeset|nvidia-uvm|nvidia-uvm-tools", MODE="0666"`. Для машины, где один
  пользователь, это безразлично; для образа, где CUDA доступна нескольким пользователям,
  это общий доступ к GPU без всякой изоляции. Наш вариант: `MODE="0660", GROUP="video"`,
  0666 — только как явный переключатель.
- **`RUN+="/usr/bin/nvidia-modprobe …"` без `-u` во второй строке** — у гиста две
  команды подряд (`-c0 -c255 -m`, затем `-u -c0`); `nvidia-modprobe` из исходников у нас
  ляжет в то же место, но путь надо брать из нашей раскладки (`/usr/bin` — ок).
- **`NV_EXCLUDE_BUILD_MODULES=''`** собирает всё, что есть в дереве; мы перечисляем
  `built_modules` явно и не тянем `nvidia-peermem`.
- **Flatpak-ветка на Alpine недоступна** (пакета нет) — это не «упрощение», а отсутствие;
  планируем bwrap.
- **Патч tegra** — единственная правка исходников драйвера; ложится на 580.196.07 как есть
  (тот же контекст), но проверять придётся на реальной сборке модулей (Фаза 3).

## 5. План fallback-jail (подход A), пошагово

Цель: закрытые библиотеки и CUDA-приложения работают, не будучи собранными под musl.
Ничего из этого не требует `musl-nvidia` (D2) — jail живёт рядом с системой.

1. **Собрать glibc-rootfs.** `debootstrap --variant=minbase trixie /opt/nv-jail/root`
   (или `ubuntu-base-24.04` тарball → `rm -rf /opt/nv-jail/root && tar -xpf … -C …`).
   Внутри — только `libc6`, `libstdc++6`, `zlib1g`, плюс то, что требует CUDA-приложение.
   Размер шага: ~120–200 МБ; проверять `du -sh`.
2. **Положить драйвер в rootfs.** Прогнать `NVIDIA-Linux-x86_64-580.196.07-no-compat32.run`
   (sha256 `2189c47f…`, см. TASK Приложение А) **внутри** rootfs:
   `bwrap --bind $root / … sh NVIDIA-…-no-compat32.run --no-kernel-modules --silent`.
   Флаг `--no-kernel-modules` обязателен: модули уже в musl-системе.
3. **Смонтировать устройства.** Бинды: `/dev/nvidia*` (по одному, не весь `/dev`),
   `/dev/null`, `/proc`, `/sys`, `/dev/shm` (tmpfs), `/tmp` (tmpfs). `/proc/driver/nvidia`
   приходит вместе с `/proc` — через него NVIDIA определяет версию модуля.
4. **Проверить изнутри.** `nvidia-smi` (берётся из rootfs, не из musl-системы), затем
   `deviceQuery`/`bandwidthTest` из CUDA-samples, затем реальное приложение. Ожидаем
   совпадение версии модуля снаружи и библиотеки внутри — расхождение версий = 700-я
   ошибка CUDA.
5. **Сделать запуск удобным.** `/usr/local/bin/nv-jail` (sh, ~20 строк): bwrap с фиксированными
   биндами + проброс `CUDA_VISIBLE_DEVICES`, `--dev-bind` для рабочих каталогов,
   `--unshare-pid --unshare-uts --die-with-parent`, **без сети по умолчанию**
   (сверено с MuslMan, см. 1.4).
6. **Персистентность.** `nvidia-persistenced` — **снаружи**, в musl-системе (бинарник +
   gcompat), иначе внутри jail не будет daemon-сокета; сокет пробросить биндом.
7. **Права.** Группа `video` (0660) вместо 0666; членство в группе — единственный
   переключатель доступа к GPU.
8. **Зафиксировать в образе.** Jail — это `/opt/nv-jail/{root,bin}` + один OpenRC-сервис
   для persistenced; в `base-image`-проверке на «только amd64» он обязан пройти
   (32-битного кода в rootfs быть не должно — отсюда и `-no-compat32.run`).

Альтернатива «Flatpak GL-расширения», которую предлагает гист, на Alpine 3.25 закрыта
(пакета flatpak нет) и остаётся запасным вариантом, если база переедет на 3.22/edge.

## 6. Что это меняет в решениях

- **D1 (ветка 580)** — ещё один довод за 580: гист собран на 580.126.18, то есть связка
  «musl-ядро + glibc-userspace» обкатана именно на этой ветке.
- **D2 (musl-стратегия)** — jail не требует ни приватной, ни системной подмены musl.
  Если A-путь устраивает по производительности, `musl-nvidia` (Фаза 2.0) остаётся
  исследованием, а не обязательством.
- **D3 (glvnd)** — не меняется: графика всё равно упирается в glvnd/mesa, а не в jail.
- **Лестница** — шаг «`nvidia-smi`» получает второй, независимый путь проверки:
  musl-модули + `nvidia-smi` из glibc-jail. Если один не выйдет, второй покажет, где
  именно рвётся.

## 7. Воспроизведение

```sh
curl -sL https://gist.githubusercontent.com/sineemore/161e4051b2d03f9e3b0f3e16221463ce/raw -o gist.txt
curl -sL https://api.github.com/gists/161e4051b2d03f9e3b0f3e16221463ce | jq -r '.files|keys[]'
# проверка компиляции nvidia-modprobe под musl-заголовки (на (build), /tmp):
curl -sO https://dl-cdn.alpinelinux.org/alpine/v3.25/main/x86_64/musl-dev-1.2.6-r6.apk && tar -xf musl-dev-1.2.6-r6.apk -C mdev
curl -sL https://github.com/NVIDIA/nvidia-modprobe/archive/refs/tags/580.196.07.tar.gz | tar -xz
cd nvidia-modprobe-580.196.07 && mkdir -p gen && echo '#define NVIDIA_VERSION "580.196.07"' > gen/version.h
gcc -fsyntax-only -nostdinc -isystem /tmp/mdev/usr/include -isystem /usr/include \
    -isystem /usr/include/x86_64-linux-gnu -Igen -Imodprobe-utils -Icommon-utils -I. \
    -DPROGRAM_NAME='"nvidia-modprobe"' nvidia-modprobe.c
# листинг репозитория Alpine 3.25 (6052 пакета, community отсутствует):
curl -s https://dl-cdn.alpinelinux.org/alpine/v3.25/main/x86_64/ | grep -o ">flatpak[^<]*\.apk<"
```
