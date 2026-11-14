# 03 — git-bruh/nvidia-musl: разбор KISS-репозитория и проверка его выводов на 580

Замер: 14.11.2026, `(build)` (`192.168.0.86`).

Источник: <https://github.com/git-bruh/nvidia-musl>, head
`e5cd409c64237be4b630017f0360b50c4d260222` (2021-10-06, коммит «disclaimer»),
8 пакетов KISS, драйвер **470.74**, mesa 21.1.4, libglvnd 1.3.4, gcompat из Adélie-git.
Сам автор о своём репозитории: *«just a small experiment … real usage is NOT advised
and there could be possibilities of undefined behaviour due to the inherent hackiness»*.
В проект из него не переносится ни файла — переносится список приёмов и список того,
что больше не нужно.

## 1. Четыре приёма, на которых у него всё держится

1. **Патч gcompat на два символа** (`repo/gcompat/patches/nvidia.patch`):
   `dladdr1()` — обёртка над `dladdr()`, для `RTLD_DL_SYMENT`/`RTLD_DL_LINKMAP`
   возвращает 0; и новый файл `libgcompat/stdfiles.c` с `void *_IO_2_1_stdout_ = NULL;`
   — «NULL-стаб: если драйвер его реально использует, он упадёт, но за моё время
   использования не использовал».
2. **libglvnd без TLS**: `meson -Dx11=disabled -Dtls=disabled`, плюс удаление
   `subdir('tests')`. Причина в README: без `tls=disabled` получаем
   `initial-exec TLS resolves to dynamic definition in ...`.
3. **`patchelf --add-needed libgcompat.so.0` по каждой библиотеке драйвера**
   (`for lib in "$1/usr/lib/"*".so.$2"`), а не `LD_PRELOAD`.
4. **Отказ от X11 и от всего, что тянет `libnvidia-tls`** — в пакет кладутся только
   `nvidia-{smi,persistenced,modprobe}` и шесть библиотек
   (`libEGL_nvidia`, `libGLESv1_CM_nvidia`, `libGLESv2_nvidia`, `libnvidia-eglcore`,
   `libnvidia-glsi`, `libnvidia-ml`); Vulkan ICD-JSON правится на ходу:
   `sed 's|libGLX_nvidia.so.0|libEGL_nvidia.so.0|'` для `nvidia_icd.json` и
   `nvidia_layers.json` (GLX-то нет). `chmod 4755 nvidia-modprobe`, soname-симлинки
   через `readelf -d … | awk '/SONAME/'`, ядра — `make modules` в `kernel/`.

Попутно: mesa пересобрана с `-Dglvnd=true -Dglx=disabled -Dplatforms=wayland
-Ddri-drivers=nouveau -Dgallium-drivers= -Dvulkan-drivers=`, из `meson.build` удалена
строка `pre_args += '-DUSE_ELF_TLS'` (баг freedesktop #35268, musl+firefox), в CFLAGS
добавлен `-DGLX_X86_READONLY_TEXT`; плюс пакеты `egl-wayland` (+ `eglexternalplatform`
из git, JSON в `/usr/share/egl/egl_external_platform.d/`) и `patchelf`.

## 2. Блокер → причина → обход git-bruh → актуальность для 580

| блокер | причина | обход git-bruh (2021, 470.74) | актуально ли для 580.196.07 (измерено) |
|---|---|---|---|
| `dladdr1` | glibc-символ, нет в gcompat | патч gcompat | **да, но уже не для CUDA**: `dladdr1` требуют ровно 2 файла — `libnvidia-eglcore`, `libnvidia-glcore` |
| `_IO_2_1_stdout_` | glibc-символ | NULL-стаб | **нет**: в 580 не встречается ни в одном из 64 64-битных ELF |
| LFS64 (`open64`, `__xstat64`, `mmap64`, …) | glibc компилирует с `_FILE_OFFSET_BITS=64`, в musl нет таких символов в таблице | в 2021 — раздувать gcompat | **нет**: musl ≥ 1.2.3 разрешает их сам, внутри загрузчика (`get_lfs64`); есть в Alpine 3.22 и 3.25 |
| `initial-exec TLS resolves to dynamic definition` | glvnd собран с TLS | `-Dtls=disabled` | **спорно**: это тот же выбор, что D3 (`tls=disabled` vs патч 091). По аудиту 02 `DF_STATIC_TLS` стоит только на 5 GL-библиотеках, `libcuda` к ним не относится |
| нет glvnd в дистрибутиве | — | собрать libglvnd и mesa с `-Dglvnd=true` | **да**: в Alpine libglvnd нет вообще (п. 5) |
| X11 не работает | `libnvidia-tls` + glibc-измы | не ставить X11-библиотеки драйвера | **частично**: `libGLX_nvidia` требует 47 символов вида `X*`/`_X*`/`Xext*` — это API `libX11`/`libXext`, оба пакета в Alpine есть (по одному файлу не проверял, что каждый экспорт на месте); к libc это не относится |
| 32-битный compat | `-no-compat32.run` | брать урезанный `.run` | **да**: в полном 580 `.run` 35 32-битных ELF / 392 MB; урезанный вариант существует и для 580 (п. 6) |

## 3. Символьный зазор 580 против Alpine 3.25 — главный результат задачи 1.2

**Метод.** Для каждого из 64 64-битных ELF дерева: `readelf --dyn-syms`, взять
`UND`-записи, отделить `WEAK` от `GLOBAL`, и проверить разрешимость против
`musl 1.2.6-r6` + `gcompat 1.1.0-r4` (оба — реальные `.so` из пакетов Alpine 3.25)
+ все символы, которые дерево определяет само (11 109 имён).

**Две ловушки, в обе я наступил в первой версии замера.**

1. *WEAK не блокируют загрузку.* `ldso/dynlink.c` musl:
  `if (!def.sym && (sym->st_shndx != SHN_UNDEF || sym->st_info>>4 != STB_WEAK)) { … error … }` —
   то есть неразрешённый `WEAK` молча пропускается. Это `__gmon_start__` (64 файла),
   `_ITM_registerTMCloneTable`/`_deregisterTMCloneTable` (по 51), `_ITM_RU*`,
   `ZSTD_trace_*` — 22 имени, ноль проблем.
2. *LFS64 резолвит загрузчик, а не таблица символов.* В musl (с осени 2022,
   в 1.2.3+) есть `get_lfs64()`: если имя кончается на `64` и база есть в списке
   (`open fopen mmap lseek pread pwrite fseeko ftello ftruncate fallocate statfs
   scandir alphasort __xstat __fxstat __lxstat …`), символ ищется как база.
   Строки этого списка есть в бинарниках `ld-musl-x86_64.so.1` и из Alpine 3.22
   (`musl-1.2.5-r13`), и из Alpine 3.25 (`musl-1.2.6-r6`) — проверил `grep -a`.
   Замер «по таблице символов» завышал пропуск на 16 имён и 16 файлов.

**Числа (после обеих поправок).**

| | GLOBAL не разрешается | WEAK (безразличны) |
|---|---|---|
| ступень 1 лестницы: `libcuda`, `libnvidia-ml`, `nvidia-smi`, `nvidia-modprobe`, `nvidia-persistenced`, `libnvidia-tls`, `libnvidia-opencl`, `libnvidia-ptxjitcompiler`, `libnvidia-glsi` | **0** | 1–3 |
| весь 64-битный набор, против musl+gcompat+дерево | 1041 имя, из них libc-ных — **только `dladdr1`** (2 файла) и `__popcountdi2` (2 файла) | 22 |

`__popcountdi2` (`libcudadebugger`, `libnvidia-rtcore`) есть в Alpine
`libgcc-15.2.0-r9` (`libgcc_s.so.1`) — проверено в пакете. Остальные категории —
GLib/Pango 553 (это `libnvidia-gtk2/3` из `nvidia-settings`), X server internals 41
(`nvidia_drv.so`, `libglxserver_nvidia`), прочая графика 115 (`wl_*`, `drm*`, `xcb*` —
`libwayland-client`, `libdrm 2.4.134`, `libxcb`), X11 59 (`libX11 1.8.14`, `libXext`),
GL/EGL 15 (ссылки на GL-диспетч — тот самый отсутствующий glvnd), C++ 13 (`libstdc++ 15.2.0`).

**Вывод, который меняет план.** Против `musl + gcompat` из Alpine 3.25 headless-CUDA
ступень не нехватает **ни одного** символа. Без gcompat не хватает 12 уникальных
имён, и все их gcompat даёт: `__register_atfork`, `dlmopen`, `dlvsym`,
`gnu_get_libc_version`, `__sched_cpualloc`, `__sched_cpufree`, `__isnan`, `__strdup`,
`__strtok_r`, `__cmsg_nxthdr` — то есть gcompat нужен, но ровно как поставщик этих
десяти, а не как «слой совместимости с glibc». Ни LFS64-шима, ни патча
`_IO_2_1_stdout_`, ни `libnvcompat` (в PR Chimera он про три `pthread_mutex_*`)
для первой ступени не нужно.

Про барьеры из Chimera это согласуется с аудитом 02: `IRELATIVE` в дереве один
(`libnvidia-glcore`), `DF_STATIC_TLS` стоит на пяти GL-библиотеках, `libcuda`
`libnvidia-tls` не хостит. Все три барьера — про графику, не про CUDA.

## 4. Что переносим, что отказываем

Переносим: `patchelf --add-needed` вместо `LD_PRELOAD`; минимальный набор файлов для
установки; soname-симлинки по `readelf -d`; `chmod 4755 nvidia-modprobe`; правку
Vulkan ICD-JSON при отсутствии GLX; обвязку `egl-wayland` +
`/usr/share/egl/egl_external_platform.d/10_nvidia_wayland.json`; саму идею брать
`-no-compat32.run`.

Отказываемся: патч gcompat (ни одного из двух символов для 580 на Alpine 3.25 не
нужен — `dladdr1` понадобится только на графической ступени и только в eglcore/glcore);
mesa 21/gl 1.3.4 версии; `-Dtls=disabled` как решение по умолчанию (это D3, его
решаем после графической ступени).

## 5. Проверка ожиданий задачи 1.2

- **`apk search glvnd` → пусто: подтверждено.** APKINDEX `v3.22/main`, `v3.22/community`,
  `v3.25/main` (индекс `v3.25/community` отдал 404, проверял иначе): пакета `libglvnd`
  нет; в aports master `main/libglvnd/APKBUILD` и `community/libglvnd/APKBUILD` — 404.
  `egl-wayland` там же нет.
- **mesa в Alpine собрана без glvnd**: в `main/mesa/APKBUILD` (pkgver 26.2.6) ключа
  `-Dglvnd` нет вовсе, есть `-Dglx=dri -Dplatforms=x11,wayland`. Значит «пересобрать
  mesa» = собрать mesa с `-Dglvnd=true` и упаковать libglvnd с нуля (APKBUILD в
  Alpine писать негде — портируем из cports, как и говорил план 1.2).
- **gcompat 1.1.0-r4** (`main/gcompat`, из Adélie-git, `provides=libc6-compat`):
  в исходнике 1.1.0 нет ни `dladdr1`, ни `_IO_2_1_stdout_` (проверено grep по
  `gcompat-1.1.0.tar.gz`); экспортирует 266 символов. Если графическая ступень
  потребует `dladdr1` — патч git-bruh ложится как есть.
- **Для 1.3 (гист sineemore)**: `akms 0.3.0-r0` в Alpine есть, `linux-firmware-nvidia`
  есть (20261019 в 3.25) — GSP-прошивки брать оттуда.
- **Для 2.0**: версия musl в Alpine 3.25 — **1.2.6-r6** (в 3.22 — 1.2.5-r13);
  актуальные ветки — 3.22 и 3.25, обе проданы индексом.

## 6. `-no-compat32.run` существует и для 580

git-bruh брал `NVIDIA-Linux-x86_64-470.74-no-compat32.run`. Для нашей ветки такой
же файл лежит на том же зеркале:

```
https://us.download.nvidia.com/XFree86/Linux-x86_64/580.196.07/NVIDIA-Linux-x86_64-580.196.07-no-compat32.run
  размер   325 784 082 (полный .run — 397 209 845)
  sha256   2189c47fa87ae45b7a166b58c3742dd1629035069cb0fb4103ed375229c55867
```

Распакован на (build) (`sh nc32.run --extract-only`; `nc32.run` — это он и есть, короче имя): **72 ELF вместо 107** — ровно на те самые
35 32-битных файлов меньше, каталога `32/` нет. Для LFS-сборки это лучший ответ на
`gpu-stack` 1.5, чем флаг установщика: 32-битный код не попадает в образ даже на
секунду, и `test-arch.sh` не видит чего удалять.

## 7. Воспроизведение

```sh
git clone --depth 1 https://github.com/git-bruh/nvidia-musl   # head e5cd409…
# символьный зазор (на (build), дерево и apk лежат в ~/.cache/nv580, путь — NV580=…):
python3 tools/sym-gap.py                  # с gcompat; USE_GCOMPAT=0 — без него
python3 tools/sym-gap-which.py            # пофайлово: «даёт gcompat / нет нигде»
# провайдеры:
curl -O https://dl-cdn.alpinelinux.org/alpine/v3.25/main/x86_64/musl-1.2.6-r6.apk
curl -O https://dl-cdn.alpinelinux.org/alpine/v3.25/main/x86_64/gcompat-1.1.0-r4.apk
curl -O https://dl-cdn.alpinelinux.org/alpine/v3.25/main/x86_64/libgcc-15.2.0-r9.apk
```

Скрипты — `nvidia-musl/tools/sym-gap.py` (переключатель `USE_GCOMPAT`, каталог — `NV580`)
и `tools/sym-gap-which.py`; обе методические поправки (WEAK и LFS64) вписаны прямо в
них, чтобы повторный прогон не врёл.
