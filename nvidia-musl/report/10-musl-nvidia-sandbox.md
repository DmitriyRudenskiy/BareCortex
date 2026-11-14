# 10 — musl-nvidia-песочница: патчи PR #6531 снимают оба барьера лоадера

Дата: 2026-11-15 (ночь) · хост: `(build)` 192.168.0.86 · статус: **ГОТОВО, обе гипотезы подтверждены**

## Цель

Шаг 2.0 (собственный patched-musl «musl-nvidia») выведен с критического пути
решением по 2.2-lite (чистый musl грузит headless-набор). Песочница нужна:
(a) доказать, что патчи musl из cports PR #6531 (head `7b121686e15c`) реально
работают на драйвере 580.196.07; (b) подготовить базу для GL-фазы D3.

## Стек

- **musl 1.2.6** — официальный релизный tarball (pinned-коммит cports
  `9fa28ece…` недоступен с этого хоста; оба патча накатываются на 1.2.6 чисто).
- **Патчи** (из PR #6531, в `tools/musl-patches/`):
  - `090-nvidia-ldso-ifunc-support.patch` — IRELATIVE/ifunc в ldso;
  - `091-nvidia-ldso-tls-prealloc.patch` — «late static» TLS: 256 B
    преаллокация в TLS-блоке main-потока для dlopen-библиотек с
    `DF_1_STATIC_TLS` и initial-exec TLS (условия: `align ≤ MIN_TLS_ALIGN(16)`
    и TLS-блок ≤ 256 B).
- **Сборка**: bwrap + Alpine edge minirootfs;
  `./configure --prefix=… --syslibdir=…/lib --disable-static CFLAGS=-DLIBC_NVIDIA_BUILD`
  (оба патча зашиты под этот `#ifdef`; без флага билд компилируется как no-op).
- **Тест**: harness с `patchelf --set-interpreter <private>/lib/ld-musl-x86_64.so.1`.

## Матрица (измерено)

| конфигурация | libnvidia-tls | libnvidia-glcore | libcuda cuInit |
|---|---|---|---|
| чистый musl + gcompat | **FAIL** initial-exec TLS | FAIL (через tls) | OK, rc=100 |
| **musl-nvidia + gcompat** | **LOADER_OK** | **LOADER_OK*** | OK, rc=100 (регрессии нет) |
| musl-nvidia **без** gcompat | FAIL `gnu_get_libc_version` | — | FAIL (то же) |

\* плюс тестовый stub-объект с `dladdr1` (см. ниже).

## Выводы

1. **090 (IRELATIVE/ifunc) — работает.** `libnvidia-glcore` несёт ровно 1
   `R_X86_64_IRELATIVE` — reloc применён, LOADER_OK.
2. **091 (TLS prealloc) — работает.** `libnvidia-tls`: `.tbss` 56 B, align 8,
   `DF_1_STATIC_TLS`, `.tdata` нет — принимается как late-static (измерено
   в замерах: `noff=56 ≤ 256`, `dynamic_tls=0`). `libnvidia-glcore` сам TLS
   не имеет — все его TLS-символы UND и импортятся из `libnvidia-tls`;
   glcore жёстко `NEEDED libnvidia-tls.so.580.196.07`.
3. **gcompat по-прежнему обязателен.** Patched-лоадер не отменяет активацию
   gcompat (`gnu_get_libc_version` и остальные gaps остаются). Шим
   `libnvcompat.so.0` / `--add-needed` остаются обязательной операцией.
4. **Новый gcompat-gap: `dladdr1`** — glcore ссылается, gcompat 1.1.0 не
   предоставляет. Для GL-набора нужен либо патч gcompat, либо символ в шиме.
   Headless-набор не затрагивает.
5. **После загрузки** конструктор glcore бросает `std::system_error` без
   дисплея/DRM — это runtime, не лоадер; за рамками headless-гейта (GL/D3).

## Подводные камни (задокументировать для будущих сборок)

- `make install` у musl **всегда** ставит ldso в `syslibdir` (по умолчанию
  `/lib`) — в общем rootfs это перетирает системный лоадер. В песочнице
  обязательно `--syslibdir=<prefix>/lib`.
- `configure` пишет конфиг в **`config.mak`**; верхний `Makefile` хранит
  дефолты — проверять префикс по `Makefile` неверно.
- Без `CFLAGS=-DLIBC_NVIDIA_BUILD` сборка проходит, оба патча — no-op,
  поведение идентично чистому musl (симптом: тот же TLS-ошибка).
- Итоговая библиотека сборки — `lib/libc.so` в дереве (не `./libc.so`).

## Итог по плану

- musl-nvidia (PR #6531) снимает оба барьера лоадера — GL-путь на patched-musl
  технически реализуем.
- Для headless чистого musl + lite-инсталлятора достаточно — 2.0 на критическом
  пути не нужен (подтверждено).
- GL/D3 потребуется: musl-nvidia + gcompat с `dladdr1` + шим.
- Критический следующий шаг по-прежнему **2.3** (реальный GPU/Chimera): прогон
  lite-инсталлятора и проверка реальных устройств вместо rc=100/rc=9.
