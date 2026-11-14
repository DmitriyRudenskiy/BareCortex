# 08 — 2.3 (gpu): nvidia-smi and cuInit against loaded kernel modules

Date: 2026-11-14. Host: `user@192.168.0.86` (Pop!_OS, kernel 7.0.11, the machine
`gpu-stack` 0.2 names). No root, no host-system changes: everything ran in the
2.2-lite bwrap environment (`~/lite`, Alpine edge rootfs, musl 1.2.6-r6 +
gcompat 1.1.0-r4 + `libnvcompat.so.0` from PR #6531 head `7b121686e15c`).
Driver under test: **NVIDIA UNIX Open Kernel Module 580.159.03**, 5× GeForce
RTX 4060 Ti (Ada), `/dev/nvidia*` mode 0666. Script: `tools/run-gpu23.sh`.

## Verdict: ladder step 2 PASSES on STOCK musl

No patched libc was needed. The 2.2-lite prediction holds against real
devices: the headless set loads, resolves, and talks to the driver.

| probe | userspace | result |
|---|---|---|
| `nvidia-smi` (host binary, gcompat stub interp) | 580.159.03 | **full table printed** — 5 GPUs, processes, memory |
| `harness libnvidia-ml.so.1 --call0 nvmlInit_v2` (sanitized) | 580.159.03 | `LOADER_OK` `SYM_OK ×2` **`CALL_OK rc=0`** |
| `harness libcuda.so.1 --calli0 cuInit` (sanitized) | 580.159.03 | `LOADER_OK` `SYM_OK` **`CALL_OK rc=0`** |
| `nvidia-smi` from the pinned `.run` | 580.196.07 | `Failed to initialize NVML: Driver/library version mismatch` (exit 18) — **NVML reached the driver**; the mismatch is the driver's own answer |
| `harness /nv/libcuda.so.580.196.07 --calli0 cuInit` | 580.196.07 | `LOADER_OK` `SYM_OK`, then **SIGSEGV (exit 139)** |

The mismatch pass is a positive result, not a failure: rc=18 and the segfault
are both answers from *inside* the driver interface — the loader layer is not
what stops a version-mismatched userspace. On glibc the same mismatch returns
`CUDA_ERROR_SYSTEM_DRIVER_MISMATCH` (803) cleanly; under musl the pinned
libcuda dies in its mismatch error path. Not chased: a mismatched pairing is
not a supported configuration, and the supported pairing works. Recorded
because it is a real musl-vs-glibc behavioral difference in vendor code.

## What the recipe actually needs (measured, again, with devices present)

- Stock Alpine musl 1.2.6-r6 + gcompat 1.1.0-r4 + `libnvcompat.so.0`; per
  library: the PR sanitize recipe (`--add-needed libnvcompat.so.0` +
  `--rename-dynamic-symbols` are the load-bearing ops, report/07's ablation
  stands). Binaries need nothing — gcompat's `ld-linux-x86-64.so.2` stub is
  the interp and re-execs `ld-musl` with gcompat preloaded.
- `nvidia-smi` finds NVML by absolute path only → sanitized
  `libnvidia-ml.so.1` must be installed at `/usr/lib` (or `/usr/lib64`,
  `/usr/lib/nvidia`, `/usr/lib64/nvidia`); SONAME symlinks created by hand —
  the payload has none. `libnvcompat.so.0` in the default search path.
- Sandbox needs the real `/dev` (`--dev-bind`, not `--dev`), `/sys` (NVML PCI
  walk) and `/proc` (`/proc/driver/nvidia/version`). Device nodes are 0666 on
  this host, so no group tricks were needed here; the image plan (0660 +
  group video) still stands.
- **Version match is enforced by the driver, to the digit.** The host's loaded
  modules are 580.159.03, so the PASS pass used the host's own installed
  580.159.03 userspace copies, sanitized identically. The pinned 580.196.07
  `.run` will be the right input only where its own modules are loaded —
  i.e. on the Alpine image, where ladder step 1 (build modules from the same
  `.run`) is still the open rung.

## Facts for the other changes

- The (gpu) host runs the **open** kernel module (`NVIDIA UNIX Open Kernel
  Module … 580.159.03`). D1's closed-`kernel/` choice was justified for
  Maxwell/Pascal/Volta; Ada (this host's 4060 Ti) is an open-module part.
  Does not move D1 (the target GPU is still unnamed), but the final image's
  module flavor must follow the target's architecture, and `gpu-stack` should
  know both flavors exist at 580.
- `cuInit rc=0` under musl means TASK.md 2.3's stretch (CUDA native, not in
  jail) is already half-proven at the driver-API level; `deviceQuery` and the
  PyTorch rung are the remaining ladder steps 3–4.

## Reproduce

```sh
# on 192.168.0.86, after tools/lite-env-setup.sh
sh ~/lite/run-gpu23.sh all
```
