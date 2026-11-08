# Kernel Patches

Placeholder directory — **no patches shipped**, on purpose.

`0001-optimize-for-ai-workloads.patch` would be a `git format-patch` of
out-of-tree changes against one exact kernel version. Anything generic is
immediately wrong for the next LTS. Generate yours against the kernel you
actually build:

```bash
cd /usr/src/linux-<version>
git init && git add -A && git commit -m "vanilla <version>"
# ... apply your changes ...
git format-patch -1 -o /opt/barecortex/kernel/patches/
```

## What actually belongs here

Most "AI performance" tuning is a **runtime** concern, not a patch:

| Goal | Better than a patch |
|---|---|
| Low scheduling latency | `CONFIG_PREEMPT_NONE=y` (already in `.config`) |
| Large-page allocations | `CONFIG_TRANSPARENT_HUGEPAGE_ALWAYS=y` + `nccl` env |
| GPU BAR resize above 4G | kernel param `pci=realloc` (already `CONFIG_PCI_REALLOC_ENABLE_AUTO`) |
| Keep GPUs powered | `nvidia-persistenced` service |
| Lock CPU clocks | `cpupower frequency-set -g performance` |

Real patch candidates, if you need them, are usually backports of a specific
PCIe/NVLink fix for your platform errata — which are, by definition,
version-specific.
