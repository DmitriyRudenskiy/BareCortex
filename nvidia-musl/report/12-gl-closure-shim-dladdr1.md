# 12. GL-закрытие (musl): dladdr1 в шиме — LOADER_OK без внешних стабов

**Дата:** 2026-11-15 · **Хост:** (build) `192.168.0.86` (musl-nv sandbox, bwrap)
**Коммиты:** shim `tools/shim/libnvcompat.c` (+dladdr1)

## Замер разрыва GL-closure (glcore + gpucomp + tls)

Провайдеры: musl 1.2.6-r6 + gcompat 1.1.0-r4 + дерево 580.196.07. Не
разрешается GLOBAL — 11 имён:

- 10 — **LFS64-имена** (`__fxstat64`, `__xstat64`, `fopen64`, `fseeko64`,
  `scandir64`, `statfs64`, `alphasort64`, `fallocate64`, …) — загрузчик musl
  1.2.6 резолвит их сам (`get_lfs64()` в ldso/dynlink.c), в символьные таблицы
  они не входят. Не разрыв.
- 1 — **`dladdr1`** (glcore) — glibc-экстензия, в gcompat 1.1.0 её нет.
  Настоящий разрыв.

`gnu_get_libc_version@GLIBC_2.2.5` (glcore, GLOBAL, версионный) в замере не
фигурирует (gcompat экспортирует его без версии), но контрольный прогон
чистого glcore упёрся именно в него — см. ниже про якорь gcompat.

## Фикс: dladdr1 в libnvcompat (оператор D2, без правки системы)

Шим уже `NEEDED` во всех патченных библиотеках, поэтому он сам может
удовлетворять ссылки. Добавлено 4 строки — делегирование в musl `dladdr`
(реальная функция, не заглушка):

```c
int dladdr1(const void *addr, Dl_info *info, void **extra_info, unsigned int flags)
{
    (void)extra_info; (void)flags;
    return dladdr(addr, info);
}
```

Инсталлятор (`nv-lite-install.sh`) шим из того же файла собирает — будущие
инсталлы получают dladdr1 автоматически, правок инсталлятора нет.

## Результат (bwrap: buildroot-корень, /musl-nv = чистый musl-билд, патченные GL-библиотеки)

Операции над копиями glcore/gpucomp/tls — те же, что инсталлятор:
`patchelf --add-needed libnvcompat.so.0` + `--rename-dynamic-symbols` (в glcore
перемеплен один `pthread_mutex_trylock` → `__nvcompat_mutex_trylock`).

```
LOADER_OK  libnvidia-glcore.so.580.196.07
LOADER_OK  libnvidia-gpucomp.so.580.196.07
LOADER_OK  libnvidia-tls.so.580.196.07
```

**Без внешних стабов** — раньше GL-набор грузился только с `stub-dladdr1.so`
через LD_PRELOAD (отчёт 10). Загрузочная цель GL-фазы на musl-стороне достигнута.

## Находка 1: шим = якорь gcompat (роль `--no-as-needed`)

Контроль «чистый glcore + стаб dladdr1» падает:
`Error relocating …: gnu_get_libc_version: symbol not found`. Причина:
стаб не тащит gcompat в граф; алиасы `/lib/libc.so.6`, `libm.so.6` в
buildroot — мелкие, а реальный gcompat в графе оказывается только через
`NEEDED libnvcompat.so.0` шима (он линкован с `-l:libgcompat.so.0` и
`--no-as-needed`). musl-ldso биндит версионную ссылку `@GLIBC_2.2.5` на
неверсионный экспорт gcompat (эмпирически подтверждено прогоном). Вывод:
`--add-needed` шима — не опциональная страховка, а обязательный механизм
доставки gcompat в граф каждой библиотеки (подтверждает отчёты 06/07).

## Находка 2: std::system_error при выходе (открытый вопрос D3)

После LOADER_OK, в статических деструкторах (exit code 134, SIGABRT):

```
terminate called after throwing an instance of 'std::system_error'
  what():  No error information
```

Только в glcore/gpucomp; tls выходит чисто (exit 0). **Препотчеден** —
воспроизводится на непаatched-копии glcore с полным gcompat (преaload шима),
т.е. не следствие патча/шимы. Полная триажка — **отчёт 13**: musl-specific
(glibc-контроль чистый), без потоков и файловых операций, сознательный бросок
голого `std::system_error()` в статическом teardown; обход — `_exit()` в
harness'ах.

## Статус лестницы (отчёт 11 → 12)

| Ступень | Статус |
|---|---|
| 1. ядро-модули из .run (Alpine-rung, Chimera) | **открыт** (нужно окно на Chimera) |
| 2. headless-модули (nvidia-modeset/uvm) | PASS (отчёт 07) |
| 3. headless-userspace (musl + gcompat + шим) | PASS (отчёт 10) |
| 4. PyTorch с CUDA (glibc-тюремка на musl-хосте) | PASS (отчёт 11) |
| GL-closure loader (musl, шим c dladdr1, без стабов) | **PASS (этот отчёт)** |
| D3 GL-рантайм (контекст, рендеринг, exit-teardown) | открыт |
| pinned-driver jail (580.196.07 userspace на 580.159.03 kernel) | опция |

## Дальше

- D3: реальный GL-контекст (нужен DRM/дисплей или headless-эмуляция) +
  разобрать exit-teardown.
- Ступень 1: Chimera-окно (наш payload 580.196.07 на реальном Alpine).
- Опционально: pinned-driver вариант тюремки (чистый mismatch-контроль).
