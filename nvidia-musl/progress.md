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
  GLIBC max, TLS reported only. Committed fa1fab3 (local, nvidia-musl/** only, no push).
- Ruling: work on the current branch, commit only nvidia-musl/** files (repo has unrelated
  uncommitted changes), never push — push is not part of this task.
- Environment: this Mac has no NVIDIA GPU and no running QEMU guest yet
  (vm-host.sh pipeline exists for the LFS build host; Docker daemon down). GPU work
  (ladder steps 1, 3, 4) needs the operator's GPU machine — D1 (580 vs 620) unresolved.

Open (operator):
1. GPU machine identity (model/branch D1, ssh/console access, .run already present?).
2. Original elf-audit.py, if retained anywhere.
3. Go-ahead to stand up the Alpine VM (qemu) for Phase 2.0/2.2 static work.

Next (no GPU needed): Phase 1.2 (git-bruh), 1.3 (Void gist), 1.4 (MuslMan), 1.5 (ZLUDA, opt);
download .run (default 620 pending D1) + extract + run tools/elf-audit.py → report/02.
