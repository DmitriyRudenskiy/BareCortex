# Design: gpu-stack

## Context

The stack has six parts: the NVIDIA kernel module, the userspace driver, CUDA Toolkit, cuDNN, NCCL, and `nvidia-persistenced`. Two of them are open source (NCCL, and the `nvidia-open` kernel module since r515), the rest are binary distributions. None of them is currently in this repository's build inputs, and the only documentation pointing at them resolves to a gaming book.

The controlling constraint from `base-system-and-init` is that every package in the image traces to a declared source. That constraint was written with source packages in mind. Applied to a driver installer that drops a few hundred files into `/usr/lib` and `/usr/lib/modules`, it needs a decision about what "declared" means, not a restatement of the rule.

## Goals / Non-Goals

**Goals**

- Every component named, with source, provenance class, licence, and the document it is installed from.
- The from-scratch and zero-abstraction claims survive contact with the fact that the GPU stack is a binary blob.
- One declared answer to "who provides the CUDA runtime that a training process actually loads".
- Verification commands recorded with what each proves, so the hardware change has something to run.

**Non-Goals**

- Running anything on hardware. No `nvidia-smi` output, no P2P result, no benchmark enters this change.
- Multi-node: no InfiniBand, no RDMA, no fabric managers.
- Any X server or display configuration. Headless stays the default.

## Decisions

### D1 — Provenance is a recorded field with three classes

| Class | Meaning | Examples |
|---|---|---|
| `source-built` | Built here from upstream tarball | NCCL, `nccl-tests` |
| `binary-distribution` | Unattended fetch of a published binary | driver `.run`, CUDA Toolkit `.run` |
| `binary-authenticated-fetch` | Fetch needs a human login; build verifies a checksum of a locally supplied file | cuDNN (see OQ1) |

A binary with no `base.lock` entry fails the build exactly as a source package does. For an installer that writes hundreds of files, the entry records the installer's own installed-file list as the file set, so "declared" means "the set is recorded", not "we enumerated it by hand".

**Alternative considered:** treat the GPU stack as outside `base.lock` because it is not a package. Rejected — that is precisely how an unaccounted binary enters an image that advertises provenance.

### D2 — Driver branch and module flavour are derived, not chosen

Which branch supports a given GPU, and whether the `nvidia-open` module supports it at all, is published by NVIDIA in its supported-architecture table. The recorded input is the GPU model per target; the branch and module flavour are looked up. A branch number from a conversation ("r550/r580 or newer") is not an input, and a wrong branch is a module that will not load.

The driver and CUDA versions are recorded as a **pair**, with the compatibility-table row that permits the pairing, plus the two commands that show each half on a running host. Pinning one without the other is how a stack becomes unbuildable after an update.

### D3 — Who owns the CUDA runtime is declared, per environment

Two topologies are coherent and the project currently describes both at once:

- **Toolkit-authoritative.** CUDA Toolkit installed system-wide; frameworks built from source against it. Needed for custom kernels, `nvcc`, and profiling tools.
- **Wheel-authoritative.** A PyTorch wheel carries its own CUDA runtime, cuBLAS and cuDNN. The system Toolkit is then not what makes training run; it is a build dependency at most.

These differ in what `base.lock` must contain, what the loader must be told, and what an update can break. The decision is which one is BareCortex's default, stated in the install guide, with the other documented as a deviation. "Install CUDA to `/opt/cuda` and export the paths in `/etc/profile`" describes neither and hides the question.

**Ownership:** this change owns the answer, because the question is about library placement. `userspace-and-ml-stack` consumes it and must reference task 1.3 rather than running its own probe and recording a second answer — two changes resolving one question is how the two of them ended up with duplicate tasks in the first audit.

### D4 — Libraries are registered, not exported

Loader paths for what the **system** must see — the driver's `libcuda.so.1` and friends, installed into `/usr/lib` — go in `ld.so.conf.d` entries owned by the installation, verified with `ldconfig -p`. There is exactly one copy of the driver's userspace libraries system-wide, and that is the property worth defending.

Toolkit libraries under `/opt` are a different case and are deliberately **not** registered: they are opt-in per consumer, and registering them is how a system binary resolves a CUDA library from the wrong tree. What remains in a consumer's environment is documented per consumer with the reason.

Neither case may depend on a login profile. OpenRC services do not source `profile.d`, which is the same defect already recorded for the NCCL environment in `honest-scope-and-config-generation` D8: settings in `profile.d` never reached the service. A GPU stack installed "with paths in `/etc/profile`" is invisible to `nvidia-persistenced` and to any training service.

### D5 — The unattended-build claim narrows where the licence does

If cuDNN requires an authenticated download (OQ1), the build cannot fetch it. The build accepts a locally supplied artifact and verifies its checksum, and the documentation stops implying a fully unattended build of the whole stack. Mirroring or caching the artifacts is a licence question (OQ1), not a convenience question.

### D6 — Verification commands carry their scope with them

| Command | Proves | Does not prove |
|---|---|---|
| `nvidia-smi` | driver loaded, devices visible | that P2P works |
| `nvidia-smi -q -d P2P` | P2P status per pair | measured bandwidth |
| `nvidia-smi topo -m` | NUMA/PCIe affinity map | that NCCL uses it |
| `nvidia-smi topo -p2p r` | P2P availability per pair (`OK`/`ERR`/`UNS`) | degraded bandwidth — that is not what this reports |
| `nccl-tests` (`all_reduce_perf`) | the collective completes at the stated size | anything about a different size or process count |

The commands are recorded here so the hardware change can run them. None of them is run in this change, and no number from any of them appears in this change.

### D7 — The boundary with `target-hardware-bring-up`

This change ends where a claim would need a machine. It specifies what is installed, where it lives, how it is accounted for, and how it is checked. It does not check it. The two changes were asked for separately and the split is deliberate: everything here is answerable from documentation and a build tree, and nothing that requires the target host is hidden inside it.

### D8 — Where the external plan is right, and those parts are adopted

A 14-cycle external plan for the same machine was reviewed. It is better engineering than its edition citations suggest, and these decisions are taken from it rather than re-derived:

- **Device nodes without the setuid helper.** `nvidia-modprobe` is a setuid binary; the plan's alternative — module option `NVreg_CreateDeviceFiles=1` as primary, explicit node creation as fallback, decided by a probe at install time — is the smaller attack surface and is adopted.
- **`nvidia.kmod = open|proprietary` as a `base.lock` flag**, chosen from the GPU architecture rather than assumed, with a GSP firmware load gate and a documented fallback.
- **The installed-file list captured by the install step itself** (an `install -D` wrapper recording every path), which answers the "how do you account for a blob that writes hundreds of files" question D1 leaves open.
- **Pruning the CUDA toolkit install** to the components used, keeping the CLI profilers, dropping the GUI profiling bits — the same "used subset" rule applied to disk instead of to languages, where it costs nothing.
- **Baselines before tuning**, as a rule rather than a hope: a later cycle may change a default only against a recorded before/after table. This is the mechanism D14 of `honest-scope-and-config-generation` asks for, stated independently and in the same words.
- **Wheel curation with an air-gap default** — downloads happen on the operator's workstation, hashes are verified, installs run `--no-index`. And `uv` built from source into the system, which is the language policy's adoption route, taken.

The persistence daemon's purpose is also stated correctly there and adopted here: keeping the driver initialised across **job start and stop**, which is the granularity at which the cost is real. "Per epoch" is not — a training process holds its context for the whole run.

### D9 — What the same plan imports that this change must not

Recorded so the argument is not re-run at the next consultation.

| Imported idea | Why not |
|---|---|
| `LD_LIBRARY_PATH` in `profile.d` as the exposure mechanism | Its stated rationale — keep one `libcuda.so.1` in `/usr/lib` and stop system binaries resolving toolkit libraries — is correct, and D4 keeps it. The mechanism is not: a service does not read login profiles, which is D8 of the other change. Driver libraries are system-wide and registered; toolkit libraries are opt-in per consumer; neither reaches a service through `profile.d` |
| Hugepage pool sized as a fraction of RAM | Reserved memory that no measured consumer asked for, unreclaimable while held. On a single node the consumers that genuinely want it — IB memory registration — are multi-node, which is out of scope. A formula is not a measurement, and this is the same unsourced number this project deleted, one step further out |
| Replacing `htop`/`nvtop` with in-house TUIs | Both are maintained; `nvtop` speaks NVML today. Retirement needs a named deficiency in the incumbent, not a preference for the project's own code |
| Hand-written syslogd, cron, and OOM watchdog | The selection order answers these before design starts: sysklogd, cron, and earlyoom are maintained upstreams. These are also the components relied on when everything else has failed, which is the worst place to be the only maintainer |
| `CONFIG_VFIO`, `CONFIG_VFIO_PCI` | Still waiting on the VM requirement D7 of the other change asked for |
| `# CONFIG_DRM is not set` | The same plan installs `nvidia-drm.ko`, which cannot build without DRM. Whichever line is wrong, one of them is |

## Risks / Trade-offs

**A binary stack cannot be traced back to a source tarball.** Accepted, and stated in the README instead of being left for a reader to notice. The alternative — omitting the exception — costs the credibility of every other claim.

**Declaring one CUDA-runtime owner may exclude a workflow.** Mitigated by documenting the other topology as a supported deviation rather than an error.

**`nvidia-persistenced` is documented from a manual page we have not read in this tree.** The service exists here; its description is written from what the daemon does — keeping driver state across process exits and driver unloads — and the task to verify it against `man nvidia-persistenced` from the installed driver is in the task list. It is not described as removing a per-epoch initialisation delay, which is what it does not do.

## Open Questions

- **OQ1:** what do the CUDA Toolkit and cuDNN licences permit regarding fetching, caching and redistribution? Read the licence texts; do not proceed on assumption. This decides whether `binary-authenticated-fetch` is one class or several, and whether an internal mirror is legal.
- **OQ2:** `nvidia-open` or the proprietary module for the eventual GPUs. Depends on architecture and branch; unanswerable until the target hardware is named.
- **OQ3:** how the driver installer's file set is recorded — its own installed-file list, or a post-install scan. Depends on what the installer emits; read its options rather than guessing.
- **OQ4:** which NCCL a training process actually loads. PyTorch wheels can bring their own NCCL, so a source-built NCCL installed system-wide may be shadowed at load time and the build effort becomes inert. Resolve with `ldd` on the loaded `libtorch_cuda.so` in the chosen environment, and let the answer decide whether building NCCL from source is part of this stack at all.
