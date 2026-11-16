# 14 — D3: EGL на musl — лоадер пройден, найден runtime-блок (panic в lazy init)

**Дата:** 2026-07-15
**Станция:** (build), 5090, kernel 580.159.03 (5.15.0-119-generic), x86_64

## Итоги (коротко)

1. **Loader level: PASS.** musl-процесс загружает всю host EGL-стек
   580.159.03 (собранный против glibc 2.39) через shim + gcompat + алиасы.
2. **Контрольный glibc-ран: EGL РАБОТАЕТ** — 5 устройств, device display,
   `eglInitialize` OK, **EGL 1.5, vendor NVIDIA, 580.159.03**.
   Стек сам по себе исправен; прежний `num=0` был артефактом
   несовпадения версий (payload 580.196.07 vs kernel 580.159.03), а НЕ musl.
3. **Musl runtime: БЛОК.** Первая реальная вызов драйвера
   (`eglGetProcAddress("eglQueryDevicesEXT")` → lazy init ICD) завершается
   `std::system_error` (code 0, "No error information" — намеренный generic
   internal-failure throw) и abort'ом. GPU-устройства при этом **не
   открываются** — паника в чистом userspace-вычислении после чтения identity
   процесса (cmdline/exe/comm). Детерминировано.
4. Корневая причина panic не установлена (нужен gdb backtrace; apk/DNS на
   (build) лег, core уходит в apport — без root не читать).

## Что сделано

- Пробы: `tools/diag/eglprobe.c` (musl) и `eglprobe-gl.c` (glibc) —
  dlopen диспетчера → eglGetProcAddress → eglQueryDevicesEXT →
  eglGetPlatformDisplayEXT(EGL_PLATFORM_DEVICE_EXT) → eglInitialize.
- `libnvcompat.so.0` расширен (host-сборки 580.x против glibc 2.39+):
  `__isoc23_strtol/strtoul/strtoll/strtoull/sscanf`, `fcntl64`.
  (Payload 580.196.07 этих символов не требовал; host 580.159.03 требует.)
- Контуры:
  - **glibc:** bwrap(nv-jail) + /egl(host 580.159.03: dispatcher+ICD+eglcore+
    gsli+gpucomp, симлинки libEGL_nvidia.so.0/libGLdispatch.so.0) +
    glvnd JSON (/usr/share/glvnd/egl_vendor.d/10_nvidia.json) + dev-bind
    nvidia/dri. `LD_LIBRARY_PATH=/egl`.
  - **musl:** bwrap(buildroot) + /egl(те же файлы + patchelf: add-needed
    libnvcompat.so.0, rename-dynamic-symbols) + /gl(shim, glvnd JSON) +
    /musl-nv (алиасы + gcompat) + `--ro-bind /sys /sys`.
    `LD_LIBRARY_PATH=/egl:/gl:/musl-nv/lib`.

## Результаты

| # | Среда | Результат |
|---|-------|-----------|
| A | glibc, 580.159.03 (совпадает с kernel) | **PASS**: num=5, DEVICE_DISPLAY_OK, init ok=1, EGL 1.5, NVIDIA |
| B | musl, 580.159.03 + shim | **PANIC** после CORE_OK (в eglGetProcAddress), SIGABRT |
| C | musl, 580.159.03, ubuntu-jail | не дошло (reloc __snprintf_chk — нужен путь к glibc; вторично) |

## Strace (musl, panic-ран) — последовательность

```
ICD-стек загружен (libEGL_nvidia → gsli → eglcore → ld-linux(glibc) → gpucomp)
C++-signal setup (membarrier EPERM — безвредно)
open /etc/nvidia/nvidia-application-profiles-rc        = ENOENT (ок)
open /usr/share/nvidia/nvidia-application-profiles-580.159.03-rc = ENOENT (ок)
open /proc/self/cmdline  → "/eglprobe"
readlink /proc/<pid>/exe → "/eglprobe"
open /proc/self/comm     → "eglprobe"
throw std::system_error (code 0) → terminate → SIGABRT
```
Ни одного упавшего syscall'а, ни одного open'а /dev/nvidia*, ни одного ioctl
до момента падения. Вводные для вычисления: только identity процесса +
уже загруженные состояния. glibc-ран (A) с тем же identity проходит.

## Гипотезы (для gdb-сессии)

1. TLS / C++ runtime state: glibc-ELF ICD (статический libstdc++) инициализирует
   C++ в musl-процессе; что-то в thread-local/exception-пути даёт сбой именно
   при musl-хозяине.
2. Контейнер-детекция драйвера (cgroup/mountinfo-профиль bwrap) идёт по
   другому ветвлению в зависимости от окружения процесса.
3. Версия-специфичный баг 580.159.03 (payload 580.196.07 под musl до этого
   места не падал — дошёл до queryDevices и нормально вернул результат).

## Следующие шаги

1. **gdb backtrace** (apk add gdb, как только поднимется DNS):
   `catch throw` в момент eglGetProcAddress → bt в кодах ICD.
2. Альтернатива: core из apport (нужен root на (build)).
3. После понимания — фикс в shim (delegate/эмуляция недостающего поведения)
   либо обходной вызов (например, eglGetPlatformDisplay напрямую, минуя
   проблемную инициализацию — проверить).
4. D2 (GLX) в glibc-jail — по-прежнему не проверен; EGL (D3) на glibc уже
   даёт полноценный display, так что приоритет D2 снижается.

## Обновление матрицы сред (накопительно)

| Среда | GLX | EGL | WGL | D3C |
|-------|-----|-----|-----|-----|
| W1 gcompat (musl, payload) | ✗ | ✗ (загрузка ок, init падает) | — | ✓ |
| W2 glibc-aliase (musl, host) | ✗ (не до init) | **загрузка ✓, init PANIC** | — | ✓ |
| glibc (nv-jail, host 580.159.03) | не проверен | **✓ 1.5, 5 устройств** | — | ✓ |

---

# РАЗВЯЗКА (2026-11-16): корневая причина найдена, EGL на musl работает

**Станция:** 192.168.0.86, 580.159.03, kernel 7.0.11, Alpine edge + патченный musl (LIBC_NVIDIA_BUILD)

## Итог

`eglprobe` в musl-песочнице: **`queryDevices rc=0x1 num=5`, `DEVICE_DISPLAY_OK`,
`eglInitialize` OK, EGL 1.5, vendor NVIDIA, RC=0** (num=5 = числу забинженных
`/dev/nvidia*`; glibc-ран на той же машине даёт num=8). Блок снят.

## Корневая причина

Паника НЕ в lazy-init и не в TLS. Символ, чей нулевой GOT-слот ронял ICD:

```
libnvidia-gpucomp.so.580.159.03:
  UND WEAK __pthread_key_create@GLIBC_2.2.5   → GOT-слот на vaddr 0x43de760
```

Это glibc-внутренний символ (экспортируется glibc ради ABI; в glibc — IFUNC,
слот = адрес реализации). musl его не имеет; для WEAK-импорта загрузчик
**молча пишет в слот 0** (не ошибка). Код gpucomp проверяет слот:
`cmpq $0x0,0x43de760(%rip); je throw` → `std::system_error(error_code(0))`
(что=«No error information» — musl-strerror(0)). Отсюда: детерминированный
abort сразу после CORE_OK, до открытия GPU-нод.

Фикс — экспорт в `libnvcompat.so.0`:

```c
int __pthread_key_create(pthread_key_t *k, void (*destructor)(void *)) {
    return pthread_key_create(k, destructor);
}
```

Попутно в шим добавлены `alphasort64`/`scandir64` (glibc 2.28+ LFS-переименования,
требует glsi). Сборка шима обязательна с `-Wl,-soname,libnvcompat.so.0
-Wl,--no-as-needed -l:libgcompat.so.0` (soname нужен для patchelf --add-needed;
gcompat — для `__snprintf_chk` и прочих `_chk`, иначе glvnd падает на reloc).

## Методика (ptrace недоступен — NVIDIA его детектит)

- Любое прикрепление ptrace вешает/ронит проб: `libEGL_nvidia` читает
  `/proc/self/status` (TracerPid); аппаратные брейкпоинты DR0/DR7 через
  `PTRACE_POKEUSER` на ядре 7.0.11 отдают EIO даже на остановленном трейси.
- Рабочий приём: **int3-патч копии бинаря + SIGTRAP-хук в LD_PRELOAD**
  (без трейсера): правим байт по нужному vaddr на 0xCC, обработчик SIGTRAP
  читает регистры из musl-раскладки ucontext (RSP=uc+0xa0, RIP=uc+0xa8;
  int3 сообщает RIP=bp+1), пишет backtrace, затирает 0xCC обратно и резюмит.
- Цепочка попаданий: вход в `gpucomp+0x1194a10` (флаг=0) → хелпер throw
  `gpucomp+0x2b95ab`. «Флаг» оказался GOT-слотом `__pthread_key_create`
  (релокация R_X86_64_JMP_SLOT, addend 0, символ в .dynsym — WEAK FUNC).
- Сверка с хостом: тот же int3 на glibc показывает слот = указатель внутри
  маппинга libc (IFUNC-реализация) — гипотеза подтверждена за один прогон.
- Инструменты: `tools/shim/test_shim_libnvcompat.sh` (тесты), прообраз
  trapdump: int3+SIGTRAP-дампер (одноразовый дебаг-инструмент, в репозиторий
  не входит).

## Урок для следующих версий драйвера

WEAK-импорты — молчаливые: загрузчик не ругается, зануляет слот, и драйвер
падает в произвольном месте. После каждого апдейта 580.x прогонять скан:
все `UND WEAK` из `/egl/*.so*` должны резолвиться в libc+gcompat+nvcompat
(скрип-однострочник: `readelf -W --dyn-syms` против `nm -D --defined-only`).
Тест `test_shim_libnvcompat.sh` держит известный набор экспортов.

## Матрица (финал)

| Среда | EGL |
|-------|-----|
| musl (edge + патченный musl + shim) | **✓ 1.5, 5 устройств, device display** |
| glibc (nv-jail, host 580.159.03) | ✓ 1.5, 8 устройств |
