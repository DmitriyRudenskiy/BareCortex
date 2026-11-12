# 1.1 — Chimera (cports PR #6531): разбор + сверка с живым PR

Фаза 1, первый источник (обязательный). База для всего задания.
Источники: блог chimera-linux.org/news/2026/11/musl-nvidia.html (07.11.2026), PR #6531
(Draft, q66), `main/gcompat/template.py`, PR #6504 (glvnd).
Head-коммит запинен: **`c26532983c3b811c2d6b310058761bba39491bbe`** (11.11.2026).
**PR — Draft и меняется: перечитать на старте исполнения и сверить с Приложением А.**

## 1. Состав пакетов

| Путь | Что это |
|---|---|
| `main/musl/patches/090-nvidia-ldso-ifunc-support.patch` (196 строк) | ifunc/IRELATIVE в ldso |
| `main/musl/patches/091-nvidia-ldso-tls-prealloc.patch` (208 строк) | static TLS surplus для dlopen |
| `main/musl/template.py` + симлинк `main/musl-nvidia → musl` | вариант libc `musl-nvidia` |
| `user/nvidia580` (580.196.07), `user/nvidia620` (620.70.06) | userspace из `.run`, sha256 запинены; субпакеты `-libs`, `-ckms`, `-firmware`, `-opencl`; `files/` общие (симлинк) |
| `user/nvidia-modprobe` (setuid 4755), `nvidia-persistenced` (libtirpc), `nvidia-settings` (GTK3), `nvidia-xconfig` | открытые утилиты NVIDIA, нативно из GitHub-тегов |
| `user/nvidia-egl-wayland` (1.1.24), `nvidia-egl-gbm` (1.1.4) | открытые EGL-плагины (meson) |
| `user/nvidia580/files/`: `libnvcompat.c`, `libnvcompat.map`, `modprobe.conf`, `ckms.ini`, `prime-run`, `nvidia-sleep.sh`, `nvidia-drm-outputclass.conf`, сервис `nvidia-powerd` | обвязка |
| `main/gcompat` (вне PR, в master с 01.11.2026) | git-ревизия `0660063…`, pkgver 1.1.0-r3, «no releases in years» |

Хронология PR: 02.11 «собирается, ldd чистый, не тестировалось» → 03.11 «не работает,
есть локальные фиксы» → 04.11 «рабочее состояние, как минимум GL/Vulkan на 580» →
05.11 переименован «580 and 595» → «580 and 620». Блог 07.11: 620 подтверждён на
RTX 3070 Ti, aarch64 частично. Ревьюер sertonix прошёлся по ifunc-патчу (замечания
закрыты).

## 2. Патч 090 — ifunc (главный блокер)

Происхождение: Alexey Kodanev (BellSoft/Alpaquita), `fd3089b3` «ldso/dynlink: add ifunc
support» (2021); q66 обернул в `#ifdef LIBC_NVIDIA_BUILD`, починил сигнатуру
aarch64-резолвера. Механика:
- `REL_IRELATIVE` для x86_64 (`R_X86_64_IRELATIVE`=37) и aarch64 (1032);
  `STT_GNU_IFUNC` добавлен в `OK_TYPES`;
- `resolve_ifunc_addr()` вызывает резолвер (x86_64: `void(*)(void)`; aarch64:
  `void(*)(uint64_t, const void*)` с `(0, NULL)`), **не вызывает в `ldd_mode`**;
- в `do_relocs`: ifunc помечается для `REL_IRELATIVE` и для GOT/PLT-релокаций на
  `STT_GNU_IFUNC`-символы; собственные ifunc самого ldso резолвятся только на stage 2
  (`skip_ifunc = head != &ldso`);
- `do_dlsym` тоже резолвит ifunc (иначе `dlsym` вернул бы адрес резолвера).

## 3. Патч 091 — static TLS surplus (второй блокер)

Автор q66. Идея как в glibc: резерв в статическом TLS-блоке для поздно загруженных
initial-exec библиотек:
- `#define STATIC_TLS_PREALLOC 256` байт (TLS-образ `libnvidia-tls` = 0x38 = 56 байт — влезает);
- у каждого DSO флаг `dynamic_tls` вместо старого сравнения `tls_id > static_tls_cnt`;
- при `dlopen`: если `DT_FLAGS & DF_STATIC_TLS` и align ≤ `MIN_TLS_ALIGN` (только
  pointer-aligned) и влезает в резерв → размещается в статическом блоке
  (`static_tls_offset`/`initial_tls_offset`), иначе fallback в dynamic TLS;
- первая dynamic-аллокация сдвигается за резерв; `install_new_tls()` копирует static-образ
  **во все уже существующие потоки**; `update_tls_size()` учитывает резерв;
  `__dls3` фиксирует начальный offset.

## 4. Шим libnvcompat

`libnvcompat.map` — ровно три строки:
```
pthread_mutex_lock __nvcompat_mutex_lock
pthread_mutex_timedlock __nvcompat_mutex_timedlock
pthread_mutex_trylock __nvcompat_mutex_trylock
```
NVIDIA статически инициализирует рекурсивные мьютексы glibc-шным
`PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP`; kind лежит в `__u.__i[4]` (64-bit; `__i[3]`
на 32-bit), musl тип мьютекса хранит в `__i[0]`. `patch_mutex_kind()` перед каждой попыткой
лока: acquire-load kind & 3; k==0 или уже установленный type → выход; k==3 (adaptive) →
просто очистить kind; иначе relaxed-store kind→type; затем release-store kind=0
(гонки безвредны — обосновано комментарием в коде).
Сборка: `cc libnvcompat.c -o libnvcompat.so.0 -nodefaultlibs -shared
-Wl,-soname,libnvcompat.so.0 -Wl,--no-as-needed -l:libgcompat.so.0`.

## 5. Точный patchelf-рецепт (из `_sanitize_elf`)

```
patchelf \
  --remove-needed ld-linux-x86-64.so.2 \
  --remove-needed libdl.so.2 --remove-needed libm.so.6 \
  --remove-needed libpthread.so.0 --remove-needed librt.so.1 \
  --replace-needed libc.so.6 libc.so \
  --add-needed libnvcompat.so.0 \
  --rename-dynamic-symbols libnvcompat.map \
  <file>
```
Бинарники дополнительно: `--set-interpreter /lib/ld-musl-x86_64.so.1` (у Chimera — на
стандартный ldso, т.к. патченая libc системная; на Alpine — на приватный путь, D2).
SONAME-симлинки по `patchelf --print-soname`. Hardening `!pie`.
**620-специфика:** `libnvidia-api.so.1` тянет `libstdc++.so.6` ради `operator new` →
у Chimera `--replace-needed libstdc++.so.6 libc++abi.so.1`; на Alpine проще
`apk add libstdc++` и не менять ничего.

## 6. Модель упаковки musl-nvidia (важно для Alpine)

Chimera musl = **1.2.6-git (`9fa28ece`) + mimalloc 2.2.7** (external malloc). Вариант
собирается в отдельном `build-nv` с `-DLIBC_NVIDIA_BUILD`, `--disable-static`, ставится как
`libc-nv.so`. Субпакет `musl-nvidia` **заменяет системный musl целиком**
(`replaces musl`, `provides so:libc.so=0`) — вся система Chimera живёт на патченой libc.
Для Alpine это решение принимается явно: **D2 — по умолчанию приватная копия** в
`/opt/nvidia-musl-exp/musl-nvidia/lib/` + `--set-interpreter`; системная замена — только с
явного одобрения оператора. Патчи прикладывать к **Alpine-версии musl с её malloc**,
а не к chimera-конфигурации.

## 7. Сверка с живым PR (оператор, 12.11.2026 — все проверки пройдены)

| Утверждение | Проверка | Статус |
|---|---|---|
| PR Draft, q66, 9 коммитов, +1398/−7, head `c265329` от 11.11 | API: draft=True, head `c26532983c3b…`, updated 11.11 | ✓ |
| nvidia580 = 580.196.07, x86_64 sha `0aab0518…` | template.py: x86_64=`0aab0518…` (aarch64=`5b7d23…`) | ✓ |
| nvidia620 = 620.70.06, x86_64 sha `29118e3a…` | template.py: x86_64=`29118e3a…` (aarch64=`3fa65a…`) | ✓ |
| 580 → `kernel/`, 620 → `kernel-open/`, hardening `!pie` | строки 245/256 template.py | ✓ |
| Патч 090: 196 строк, из Alpaquita `fd3089b3`, Kodanev | шапка патча дословно совпадает | ✓ |
| Патч 091: 208 строк | шапка описывает initial-exec TLS / extra space | ✓ |
| libnvcompat: kind в `__i[4]` (64-bit) | исходник из `user/nvidia580/files/` совпадает | ✓ |
| musl-nvidia — симлинк на musl | `main/musl-nvidia → musl` | ✓ |
| gcompat: git `0660063…` из Adélie | `main/gcompat/template.py`: `_commit="0660063…"` | ✓ |
| patchelf `--rename-dynamic-symbols` | опция в upstream **с 0.18.0**; Alpine edge 0.18.0-r3 годен как есть | ✓ + уточнение |
| libglvnd в Alpine нет, mesa без glvnd | edge/main: пакета нет, флага в mesa/APKBUILD нет | ✓ |
| PR #6504 (glvnd) | существует: «[WIP] switch to libglvnd», q66 | ✓ |
| sgerrand жив, glibc 2.35 | не архивирован, последний релиз **2.35-r1** (тезис о DMCA 2020 не подтвердился) | ✓ |

Правки оператора, применённые в TASK.md: полный head-коммит; примечание про patchelf 0.18.0;
привязка sha256 к архитектурам (Приложение А).

## 8. Компонент Chimera → аналог/действие на Alpine (артефакт 1.1)

| Компонент Chimera | На Alpine | Действие |
|---|---|---|
| musl 1.2.6-git + mimalloc | другая версия/malloc | исходники Alpine-musl (`apk policy musl`) + Alpine-патчи; приложить 090/091 (2.0) |
| патчи 090/091 под `LIBC_NVIDIA_BUILD` | переносимы в принципе (ldso/dynlink) | apply к Alpine-муслу, конфликты — адаптировать (2.0) |
| системная замена libc | по умолчанию **нельзя** | D2: приватная копия + `--set-interpreter`; замена — gated |
| gcompat git `0660063…` | пакета нет | собрать из git Adélie (2.2) |
| libnvcompat | нет | собрать из исходника PR (Приложение А) |
| patchelf 0.19.1 | Alpine 0.18.0-r3 | годен как есть |
| libstdc++ → libc++abi | родной `libstdc++` в Alpine | `apk add libstdc++`, без replace-needed |
| libglvnd (PR #6504) | пакета нет вообще | **отложить (D3)** до графической ступени; nvidia-smi/CUDA не нужен |
| ckms | Alpine: akms (nvidia-src) или ручной `make modules` | план 1.3 / ступень 3.1 |
| modprobe.conf (blacklist nouveau/nova_core/nova_drm) | `/etc/modprobe.d/` | как есть |
| сервис nvidia-powerd (openrc) | OpenRC | порт файла сервиса |
| nvidia-modprobe/persistenced/settings/xconfig, egl-* | нет пакетов | собрать нативно из GitHub-тегов (Приложение А) |
| .run-извлечение, sha256-pin, `!pie` | то же | sha256 по архитектуре (Приложение А) |

## 9. Вывод (контрольная точка 1.1)

База — **Chimera-гибрид**: kernel-модули из `.run` + приватный патченый musl (090/091) +
gcompat-git + libnvcompat + patchelf-sanitize. Всё переносится на Alpine с адаптациями
из таблицы выше; ни один компонент не является тупиковым. Fallback — glibc-jail (1.3).
Открытые: D1 (ветка — ждём GPU), D2 (принято: приватная копия), D3 (отложен).
