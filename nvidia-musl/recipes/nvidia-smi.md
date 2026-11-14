# nvidia-smi natively on musl — the lite recipe (measured 2026-11-14)

Proven on: Alpine edge rootfs (musl 1.2.6-r6, gcompat 1.1.0-r4) inside bwrap,
against loaded NVIDIA open kernel modules 580.159.03, 5× RTX 4060 Ti —
`nvidia-smi` prints the full table, `nvmlInit_v2` and `cuInit` return 0.
No patched libc, no root, no host-system changes. Evidence: `report/08`.

## Inputs

- Alpine rootfs with `gcompat` (any 1.1.0-r4+; ships `libgcompat.so.0`, the
  `ld-linux-x86-64.so.2` stub interp, and `libc.so.6 → libgcompat.so.0`).
- `libnvcompat.so.0` + `libnvcompat.map` from cports PR #6531
  (head `7b121686e15c`), built per PR:
  `cc -O2 -nodefaultlibs -shared -Wl,-soname,libnvcompat.so.0 -Wl,--no-as-needed -l:libgcompat.so.0 -o libnvcompat.so.0 libnvcompat.c`
- An extracted NVIDIA `.run` whose version **equals the loaded kernel
  modules to the digit** (driver-enforced; mismatch = NVML rc 18 / libcuda
  SIGSEGV under musl). `--extract-only`; never run the installer.

## Install layout (inside the prefix/root — here the bwrap root)

1. `libnvcompat.so.0` → `/usr/lib/` (default search path; it must be
   findable by name).
2. gcompat provides `/lib/libc.so.6`; add `libc.so → libgcompat.so.0` in
   `/lib` if the distro's gcompat does not ship `libc.so`.
3. gcompat's stub interp must answer at `/lib64/ld-linux-x86-64.so.2` too
   (symlink to `/lib/ld-linux-x86-64.so.2`) — that is the `PT_INTERP` baked
   into every NVIDIA binary.
4. Sanitize each 64-bit **library** you need (binaries: nothing — the stub
   interp re-execs `ld-musl` with gcompat preloaded):

   ```sh
   patchelf --remove-needed ld-linux-x86-64.so.2 \
     --remove-needed libdl.so.2 --remove-needed libm.so.6 \
     --remove-needed libpthread.so.0 --remove-needed librt.so.1 \
     --replace-needed libc.so.6 libc.so \
     --add-needed libnvcompat.so.0 \
     --rename-dynamic-symbols libnvcompat.map <lib>
   ```

   Load-bearing subset (report/07 ablation): `--add-needed libnvcompat.so.0`.
5. Install where `nvidia-smi` looks for NVML — absolute paths only, it
   ignores `LD_LIBRARY_PATH`: `/usr/lib/libnvidia-ml.so.1` (or
   `/usr/lib64`, `/usr/lib/nvidia`, `/usr/lib64/nvidia`). The payload ships
   no SONAME symlinks; create them: `libnvidia-ml.so.1`, `libcuda.so.1`.

Minimum file set for `nvidia-smi` + driver-API CUDA: `nvidia-smi`,
`libnvidia-ml.so.<ver>`, `libcuda.so.<ver>`, `libnvcompat.so.0`.

## Runtime requirements

- `/dev/nvidia*` (create via `nvidia-modprobe` if udev rules absent),
  `/proc/driver/nvidia`, `/sys` (NVML PCI walk).
- Kernel modules loaded, same version as the userspace, from the same `.run`.

## Verification (the 2.3 bar)

```sh
nvidia-smi                                   # full table
harness /usr/lib/libcuda.so.1 cuInit --calli0 cuInit        # CALL_OK rc=0
harness /usr/lib/libnvidia-ml.so.1 nvmlInit_v2 --call0 nvmlInit_v2  # rc=0
```

## Uninstall

Delete the prefix; the recipe touched nothing outside it (manifest: the four
rootfs additions above are inside the root). On a real Alpine image, track
them in the package manifest instead.

## Full automation

`tools/install.sh` / `tools/uninstall.sh` — the recipe as a manifested
installer: sanitizes into `--prefix` (default `/opt/nvidia-musl-exp`),
creates the system-visible symlinks in `--syslib` (backing up whatever was
there), records every path in `manifest.tsv`; uninstall reverses the
manifest and restores backups. Tested on (gpu) 14.11: install → `nvidia-smi`
table + `cuMemAlloc` rc=0 from the prefix → uninstall → system links
restored, prefix removed. `tools/run-gpu23.sh` is the raw two-pass probe.
