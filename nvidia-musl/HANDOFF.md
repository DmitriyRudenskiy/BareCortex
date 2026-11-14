# Handoff — Проприетарный NVIDIA на musl (Alpine): исследовательский прогон

Дата: 12.11.2026 · Репозиторий: `/Users/user/BareCortex` · ветка `main`; коммиты **запушены** (решение оператора 12.11.2026)
Оператор общается на русском; отвечать по-русски, кратко.

## Что это

Долгий исследовательский прогон: заставить проприетарный NVIDIA userspace
(nvidia-smi → CUDA) работать на Alpine x86_64 (musl). Связывающий документ —
`nvidia-musl/TASK.md` (задание v2, одобрено оператором 12.11.2026, верифицировано по
живому cports PR #6531; head-коммит PR `c26532983c3b811c2d6b310058761bba39491bbe`
запинен в Приложении А). **Сначала прочитать его целиком.** Леджер решений и
прогресса: `nvidia-musl/progress.md`. Инициатива записана в Hindsight:
страница `kp-d1460c9f8f824d61b78e14b7da38339c`.

## Что уже сделано (коммит `74e7c9b`)

- `nvidia-musl/TASK.md` — зафиксировано с тремя правками оператора (полный head-коммит,
  patchelf 0.18.0 достаточен, sha256 привязаны к архитектурам).
- **Фаза 1.1 закрыта**: `nvidia-musl/report/01-study-chimera.md` — разбор PR #6531
  (патчи 090/091, libnvcompat, patchelf-рецепт, модель musl-nvidia), таблица сверки
  оператора по живому PR (все строки подтверждены), таблица «компонент Chimera → Alpine».
- `nvidia-musl/tools/elf-audit.py` — **ре-реализация** (оригинал из хэндоффa на машине не
  найден; если оператор его даст — diff и замена). Тест: selftest зелёный на Debian 12
  `libc.so.6` (IRELATIVE>0, max GLIBC_2.35, вердикт NEEDS_PATCHED_MUSL). Запуск:
  `python3 tools/elf-audit.py <dir> [--all-elf] --json a.json --csv a.csv`,
  `--selftest [путь к libc]`. Нужен `pip install pyelftools` (на этом Mac уже установлен,
  python 3.12 user-site).

## Открытые вопросы оператору (спросить до Фазы 2)

1. GPU-машина: модель (→ D1: Maxwell/Pascal/Volta = 580, иначе 620), как зайти,
   `.run` уже скачан?
2. Оригинал `elf-audit.py`, если сохранён где-нибудь.
3. Пускать ли Alpine-VM через QEMU на этом Mac (2.0/2.2 не требуют GPU), или всё
   статическое делать сразу на GPU-машине.

## Следующие шаги (без GPU, по порядку)

1. **Фаза 1.2** — git-bruh/nvidia-musl (архив, ~470-я ветка): клон, разобрать `repo/`
   (KISS-пакеты): патчи gcompat, стаб `_IO_2_1_stdout_`, patchelf-команды, флаги
   mesa/glvnd → `report/01-study-git-bruh.md` (таблица «блокер → причина → обход →
   актуальность для 580/620»). Вывод по контексту: для 580/620 рецепта недостаточно
   (нет ifunc/TLS) — не переоткрывать.
2. **Фаза 1.3** — гист Void (gist.github.com/sineemore/161e4051b2d03f9e3b0f3e16221463ce):
   DKMS-пакет, udev, blacklist, nvidia-modprobe, GSP → карта порта на Alpine + пошаговый
   план fallback glibc-jail (bwrap + свежий glibc-rootfs, бинды /dev/nvidia*) →
   `report/01-study-void-jail.md`.
3. **Фаза 1.4** — MuslMan (github.com/apiwo/MuslMan): построчный разбор, критика
   вендоринга glibc 2.35 (сверить с max GLIBC из аудита 2.1), вердикт «годен / с
   изменениями / не годен» + что заимствуем (манифест, sandbox) →
   `report/01-study-muslman.md`.
4. **(1.5 опц.)** ZLUDA — только механика drop-in .so, не больше одной сессии.
5. Скачать `.run` (дефолт 620.70.06; sha256 x86_64 `29118e3abd08…` — Приложение А
   TASK.md), `--extract-only` в `nvidia-musl/work/` (бинарники **не** в git), прогнать
   `elf-audit.py` → `report/02-elf-audit.md` (max GLIBC_2.NN → выбор rootfs для jail;
   потребители libstdc++; кандидаты mutex-инициализаторов).
6. **Только после Фазы 1** — REA (`npx rea-agents@latest setup`), Фаза 2 по TASK.md.
   Порядок «Фаза 1 → REA» — жёсткое условие задания.

## Факты окружения (проверено 12.11.2026)

- Этот Mac: NVIDIA GPU нет, QEMU не seeded (пайплайн `scripts/build/run-qemu.sh` из
  LFS-проекта существует, но для этого задания не поднят), Docker-демон остановлен.
  Сеть работает.
- Специфика pyelftools (уже учтена в скрипте): `elf.header.e_machine` — строка;
  `st_info` у символа — Container с `.type`; `d_tag` — строка; `.gnu.version_r` парсится
  вручную (Verneed: vn_cnt@+2, vn_file@+4, vn_aux@+8, vn_next@+12; Verneedaux:
  vna_name@+8, vna_next@+12).
- Поправка ground truth (в леджере): glibc-`libc.so.6` (Debian 12) **не** ставит
  DF_1_STATIC_TLS (DT_FLAGS_1 отсутствует) — флаг живёт в dlopen-библиотеках
  (libnvidia-tls). Утверждение «libc → STATIC_TLS=True» из старого хэндоффa не
  воспроизводится.
- Мусор тестов: `/tmp/elftest` (распакованный libc6 Debian 12) — временный;
  воспроизвести: `libc6_2.36-9+deb12u14_amd64.deb` из pool main/g/glibc на deb.debian.org,
  `ar x` + `tar xf data.tar.* ./lib/x86_64-linux-gnu/libc.so.6`.

## Правила, которые нельзя нарушать

- Conventional commits; коммитить только `nvidia-musl/**` (в репо есть чужие
  uncommitted-изменения в `scripts/build/*`, `docs/BUILD-LOG.md`, `build-overrides.toml` —
  не трогать); push разрешён оператором 12.11.2026 — только `nvidia-musl/**`
  (docs/скрипты/отчёты), бинарники и .run в git не попадают.
- EULA NVIDIA: патченные бинарники и вендоренный glibc не публиковать и не коммитить —
  только скрипты/патчи; `.run` вне git (при необходимости — .gitignore для
  `nvidia-musl/work/`).
- Честность: отрицательный результат — результат, фиксировать как есть.
- Правило останова: две подряд нерешённые ступени лестницы → откат на glibc-jail.
- PR #6531 — Draft: перед Фазой 2.0 перечитать и запинить новый head (вход для патчей
  090/091).

## Suggested skills

- `executing-plans` (superpowers) — прогон это inline-исполнение плана; леджер
  `nvidia-musl/progress.md` — возобновлять с первого незакрытого пункта, решения
  записывать строками `Ruling:`.
- `research` (engineering) — Фазы 1.2–1.4: исследование первичных источников с
  фиксацией в Markdown в репо.
- `verification-before-completion` — перед пометкой отчёта/ступени готовой: команда
  запущена, вывод прочитан.
- `systematic-debugging` — если вывод elf-audit / dlopen-harness расходится с
  ожиданием.
- `test-driven-development` — новые инструменты (`dlopen-harness.sh`, скрипт сборки
  musl-nvidia) сдавать с одним минимальным self-check.
