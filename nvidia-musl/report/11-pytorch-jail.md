# 11 — PyTorch с CUDA в glibc-тюремке (подход A) — LADDER STEP 4 PASS

Дата: 2026-11-15 (ночь) · хост: `(build)` 192.168.0.86 · статус: **PASS**

## Коррекция факта

Раньше в сессиях считалось, что `(build)` — без GPU. Неверно: с 09.11.2026 на
`(build)` стоит **5× RTX 4060 Ti**, open kernel module **580.159.03** (загружен),
все `/dev/nvidia*` на месте, userspace `libcuda.so.580.159.03`. Положительный
прогон PyTorch выполнен прямо на `(build)`; `(gpu)` (192.168.0.204) в момент
работы был недоступен (timeout) — не понадобился.

## Что доказано

Тезис подхода A (report/04, гист sineemore: «kernel modules are libc-independent
already, userspace can live in glibc jail/container») реализован до конца:
**glibc-процесс (torch) выполняет CUDA-вычисления на musl-хосте** через bwrap
без root и без каких-либо изменений host-системы.

Измерено в тюремке (ubuntu-base 24.04, python 3.12, torch 2.15.0+cu132):

```
devices: 5
gpu0: NVIDIA GeForce RTX 4060 Ti
gpu matmul ok, c[0,0]= -7.117937088012695   # 1024x1024 randn @ randn на cuda
cuda mem allocated after free: 12582912     # caching allocator жив
```

Совместимость: runtime CUDA 13.2 из колёс torch на драйвере 580.159.03
(ветка 580 = CUDA 13) — работает.

## Как устроено (`tools/nv-jail.sh`, commit 7e796fa + правки этой сессии)

- `setup`: ubuntu-base 24.04 tarball → `$JAIL`; `apt install -d` (только
  скачивание) + **ручная распаковка .deb** через `ar`/`tar --no-same-owner`
  (dpkg в single-uid userns не может chown; postinst не исполняются —
  ca-certificates поднят вручную).
- `torch`: venv + `pip install torch` (CUDA-колёса, ~4 ГБ, --no-cache-dir).
- `run`: bwrap: `$JAIL` как root, `--ro-bind /sys`, driver-библиотеки
  `libcuda.so.1`/`libnvidia-ml.so.1` (версия хоста 580.159.03) и `/dev/nvidia*`
  per-node `--dev-bind`; `--unshare-user --uid 0 --die-with-parent`.
  Никаких модулей/драйверов внутри тюремки — только host-драйвер.
- Диск-пол: `FLOOR_MB` (дефолт 2048) — не убивает хост при малом свободном
  месте (на момент работ `/` был занят на 98%).

### Правки этой сессии

1. Бинды driver-библиотек и `/dev/nvidia*` теперь **условные** (`if [ -e ]`) —
   setup/torch/run работают и на хосте без GPU (negative control). Попутно
   нашлась и починена битая кавычка `"$@` в `sb()` (первый же запуск
   `dash -n` её выловил — скрипт до этого ни разу не исполнялся).
2. Всё остальное — как в 7e796fa.

## Ограничения / не заявлено

- NumPy в venv не ставился (warning при импорте torch; на гейт не влияет).
- Пinned-путь (наши 580.196.07 вместо 580.159.03 хоста) в тюремке не
  гонялся — для ступени 4 достаточно host-драйвера; mismatch-сценарий
  задокументирован в `run-gpu23.sh` (2.3).
- Нативный musl-torch (альтернатива тюремке) — не заявлен: нужны
  musl-собранное torch; тюремка и есть заявленный маршрут (report/04).

## Итог по лестнице

**1** modules-from-`.run` — открыт (Alpine-образу эта ступень ещё не
закрыта); **2** nvidia-smi native — PASS; **3** CUDA cuInit+deviceQuery+alloc —
PASS natively; **4** PyTorch с CUDA — **PASS** (jail, (build), 5× 4060 Ti).
