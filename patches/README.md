# Patches

Placeholder directory. **No patches are shipped** — deliberately.

A `.patch` file is only meaningful against an exact upstream source version.
Shipping a generic "cuda-glibc-compat.patch" that was never rebased against
your glibc/CUDA/driver versions produces one of two outcomes: it fails to
apply, or worse, it applies with fuzz and silently miscompiles. Generate them
against your own tree instead.

## `nvidia/nvidia-drm-modeset.patch`

For driver-branch-specific fixes to the open GPU kernel module's DRM modeset
path (`nvidia-drm.ko`). Only relevant if you run the open kernel module
(Turing+) with `nvidia-drm.modeset=1`. Reproduce yours with:

```bash
cd nvidia-driver-<version>/kernel-open
# ... edit ...
git diff > ../../../patches/nvidia/nvidia-drm-modeset.patch
```

Before the open kernel module, this path was a patch against the .run
installer's bundled source and is not portable between branches.

## `glibc/cuda-glibc-compat.patch`

Historically needed when a new glibc shipped a symbol the prebuilt CUDA
toolkit binaries were not linked against. Modern CUDA (12.x) against glibc
2.38+ does not need it. If `nvcc` fails to build against your toolchain,
prefer fixing the version combination over patching glibc.

## Applying

```bash
patch -Np1 -i ../../patches/<dir>/<name>.patch   # from the extracted source tree
```

`-N` skips already-applied hunks so re-runs are idempotent.
