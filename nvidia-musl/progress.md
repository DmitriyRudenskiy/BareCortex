# SDD ledger — plan: nvidia-musl/TASK.md

Task v2 approved by operator (handoff 12.11.2026; claims verified against live cports PR #6531).
Initiative page: kp-d1460c9f8f824d61b78e14b7da38339c.

2026-11-12 (this session):
- Handoff received: task v2 + PR analysis + operator verification table + elf-audit.py artifact.
- Ruling: original elf-audit.py (claimed "in downloads") not found on this machine
  (mdfind + find ~/Downloads|Desktop|Documents) — re-implemented from the handoff spec,
  tested against a real glibc libc.so.6. If operator has the original, diff and replace.
- Scaffolded nvidia-musl/ {TASK.md, report/, tools/, recipes/}; report/01-study-chimera.md =
  Phase 1.1 artifact (PR analysis + verification table + Chimera→Alpine table).
- tools/elf-audit.py re-implemented from handoff spec; selftest GREEN on Debian 12
  libc.so.6 (IRELATIVE=1, max GLIBC_2.35, verdict NEEDS_PATCHED_MUSL). Ground-truth
  correction: Debian 12 glibc libc does NOT set DF_1_STATIC_TLS (no DT_FLAGS_1 at all)
  — the handoff's 'libc STATIC_TLS=True' claim does not hold for this build; the flag
  lives on dlopen'd libs (libnvidia-tls). Detector is a bit test; asserted IRELATIVE +
  GLIBC max, TLS reported only. Committed fa1fab3 (nvidia-musl/** only; amended to
  74e7c9b before first push).
- Ruling: work on the current branch, commit only nvidia-musl/** files (repo has unrelated
  uncommitted changes). Push APPROVED by operator 12.11.2026 — first push: 17 commits
  (this task + prior build work) to origin/main.
- Environment: this Mac has no NVIDIA GPU and no running QEMU guest yet
  (scripts/build/run-qemu.sh pipeline exists for the LFS build host; Docker daemon down). GPU work
  (ladder steps 1, 3, 4) needs the operator's GPU machine — D1 (580 vs 620) unresolved.

Open (operator):
1. GPU machine identity (model/branch D1, ssh/console access, .run already present?).
2. Original elf-audit.py, if retained anywhere.
3. Go-ahead to stand up the Alpine VM (qemu) for Phase 2.0/2.2 static work.

2026-11-14 (operator answers D1):
- Ruling: **D1 = ветка 580** (оператор, 14.11.2026). Модель GPU не названа — ветка
  выбрана явно (для Turing+ по D1 это консервативный вариант). Следствия: userspace
  и модули из одного `.run` = `NVIDIA-Linux-x86_64-580.196.07.run`, x86_64 sha256
  `0aab0518db53cf73c412dc7f32bbc850ca95e839d8923d0de4bff42c1c9169ce` (Приложение А);
  закрытые `kernel/`, не `kernel-open/`; `libstdc++.so.6` не менять на libc++abi
  (на Alpine родной `libstdc++`); hardening `!pie`.
- Ruling: ELF-аудит (2.1) выполняется **до** 1.2–1.4, потому что 1.4 (MuslMan)
  требует «сверить вендоринг glibc 2.35 с требованиями по аудиту 2.1» — то есть
  числу максимальной `GLIBC_2.xx` нужно появиться раньше. Твёрдое условие
  «Фаза 1 → REA» не нарушается: аудит статический, REA не запущен.
- Исполнение 2.1 — на `(build)` (хост `192.168.0.86`, `~/.cache/nv580/`, venv с
  pyelftools): `.run` и распакованное дерево вне git, в репозиторий попадает только
  `report/02-elf-audit.md`.

2026-11-14 (Phase 2.1 closed):
- Done: `report/02-elf-audit.md`. sha256 of the downloaded `.run` matches Приложение А.
  107 ELF files, 35 of them 32-bit (392 MB under `32/`). Max required `GLIBC_2.NN` =
  **2.26** tree-wide (`strfromf128` in `libcudadebugger.so`), **2.10** for the 64-bit
  target set. **1** IRELATIVE relocation (`libnvidia-glcore`, in `.rela.plt`), 0 IFUNC
  symbols, 0 TPOFF64/DTPMOD64. **5** 64-bit libraries set `DF_STATIC_TLS` — all
  GLX/glcore/VDPAU — and `libcuda.so` does not need `libnvidia-tls`. **0** `libstdc++.so.6`
  consumers, so the 620-only libc++abi question does not exist for 580.
- Finding for the ladder: the headless CUDA path (libcuda, nvidia-smi, NVML, MPS) trips
  neither Chimera barrier; 090/091 decide the GL/Vulkan path. To be tested in 2.2/2.3 `(gpu)`.
- Finding for `gpu-stack` 1.5: the installer's own `--advanced-options --help` names
  `--no-install-compat32-libs`; what it skips is those 35 files / 392 MB.
- Ruling: three defects in `tools/elf-audit.py` fixed, all found only on a real tree —
  the `.gnu.version_r` walk advanced by the *value* of `vn_next`/`vna_next` instead of
  adding it (infinite loop on any file with ≥3 Verneed entries; the run hung >30 min),
  `reloc_census` read only `.rela.dyn`/`.rel.dyn` and missed the tree's one IRELATIVE in
  `.rela.plt`, and `static_tls` read only `DT_FLAGS_1` while TASK.md 2.1 asks for
  `DT_FLAGS`/`DF_STATIC_TLS` (=0x10). Before: hang, then `NEEDS_PATCHED_MUSL: 0`.
  After: whole tree in ~4 min, `NEEDS_PATCHED_MUSL: 5`, every headline number equal to
  `readelf`. `--selftest` gained a synthetic verneed-chain check that fails on the old
  code and needs no glibc, so it runs on macOS too.

2026-11-14 (Phase 1.2 closed):
- Done: `report/03-study-git-bruh.md` + `tools/sym-gap.py`, `tools/sym-gap-which.py`.
  Source pinned at `e5cd409c64237be4b630017f0360b50c4d260222` (2021-10-06, KISS, 470.74).
- Headline: against Alpine 3.25's musl 1.2.6-r6 + gcompat 1.1.0-r4 the headless CUDA set
  (libcuda, NVML, nvidia-smi, modprobe, persistenced, OpenCL, PTX JIT, glsi, tls) has
  **zero** unresolved GLOBAL symbols. Without gcompat it is 12 names, and gcompat supplies
  all 12 (`__register_atfork dlmopen dlvsym gnu_get_libc_version __sched_cpualloc/free
  __isnan __strdup __strtok_r __cmsg_nxthdr`). So gcompat stays, but as a supplier of ten
  symbols, not as a glibc layer — and no LFS64 shim, no `_IO_2_1_stdout_` stub and no
  Chimera `libnvcompat` is needed for ladder step 1.
- Two measurement traps fixed in the committed scripts, both of which made a first pass lie:
  WEAK undefined symbols are skipped by musl's `do_relocs` (22 names: `__gmon_start__`,
  `_ITM_*`), and musl ≥ 1.2.3 resolves glibc LFS64 names inside the loader (`get_lfs64`,
  strings present in Alpine's 1.2.5-r13 and 1.2.6-r6 `ld-musl-x86_64.so.1`), so a
  symbol-table-only census over-reports by 16 names / 16 files.
- Remaining libc gap in the whole 64-bit tree: `dladdr1` (libnvidia-eglcore, libnvidia-glcore)
  and `__popcountdi2` (libcudadebugger, libnvidia-rtcore — present in Alpine libgcc 15.2.0-r9).
  Everything else unresolved is GLib/Pango (nvidia-settings), Xorg DDX internals (nvidia_drv),
  wayland/drm/xcb and X11 — packages, not libc.
- Alpine facts for later tasks: no `libglvnd` anywhere (APKINDEX 3.22 main+community, 3.25
  main; aports master 404 for both main/ and community/), mesa built without `-Dglvnd`, no
  `egl-wayland`; `gcompat 1.1.0-r4` source has neither `dladdr1` nor `_IO_2_1_stdout_`;
  `akms 0.3.0-r1`, `linux-firmware-nvidia`, musl 1.2.6-r6 (3.25) / 1.2.5-r13 (3.22).
- Bonus for `gpu-stack` 1.5 and Приложение А: `NVIDIA-Linux-x86_64-580.196.07-no-compat32.run`
  exists — sha256 `2189c47f…`, 72 ELF instead of 107, no `32/`. Prefer it over the installer
  flag: the 32-bit code never reaches the image.
- Method note: my first reply this session called LFS64 the biggest blocker. It is not —
  that was the symbol-table-only reading. The loader resolves them; do not build a shim.

Next (no GPU needed): Phase 1.3 (Void gist → akms/udev/modprobe map + jail plan),
1.4 (MuslMan verdict), 1.5 (ZLUDA, opt).
