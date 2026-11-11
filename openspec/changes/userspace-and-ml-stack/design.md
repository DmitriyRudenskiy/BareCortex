# Design: userspace-and-ml-stack

## Context

`gpu-stack` D3 states two coherent topologies and defers the choice. This change cannot defer it: the answer determines what `base.lock` contains, what the loader is told, what a version bump can break, and whether `install_pytorch.sh` is wrong or merely ugly. It is wrong either way, but the reason it is wrong depends on this decision.

The BLFS layer is a second book, and books drift. `base-system-and-init` D1 pinned the LFS edition by revision after discovering that "the OpenRC edition" without a revision is not a pin. The same rule applies here, and the answer to "which BLFS?" is currently unknown (OQ1).

## Decisions

### D1 — One lock file, entries that name their book

`base.lock` is the only package truth (convention in `openspec/project.md`). A BLFS-installed package is an entry like any other, with `book=` and `page=` fields naming where it came from — LFS 13.0-openrc chapter 8, or the BLFS edition and page, or `binary:` for a vendor artifact. A second list under `ai-stack/` or in a README table is the defect this decision exists to prevent, and it is the failure `build-orchestration` 2.1 was written to make impossible.

### D2 — Wheel-authoritative runtime, toolkit as a build dependency, source build as a gated deviation

Default: the training runtime is a **curated wheel set** — downloaded on the operator's machine, hash-verified, installed `--no-index` against a local directory (`gpu-stack` D8, adopted there). The system CUDA Toolkit stays installed because `nvcc`, profilers, and anything compiled against CUDA need it, which is `gpu-stack` D1's `binary-distribution` class, not a runtime dependency.

Building PyTorch from source is a **deviation with a gate**, not a default: it requires a GPU architecture list (no machine, no list), hours of build on a host that has not been declared (`build-orchestration` group 0), and a stated reason — custom kernels, a patch that must land, an architecture the wheels do not carry. Without all three, "from source" is a description of intent, and the repository already has one of those.

Why not the other way: a source build is the honest-scope choice only if it is actually performed and measured. Declaring it while the only working path is a wheel is exactly the "artifact that looks like an assertion but is inert" class this project has catalogued three times.

**Ownership:** `gpu-stack` decides which topology is BareCortex's default, because the question is where libraries live; this change consumes that decision and must cite `gpu-stack 1.3` rather than running its own probe. The first audit found both changes carrying the same probe and the same "declare the owner" task, which is two answers waiting to disagree.

### D3 — `uv` from source is the installer; `pip` never sees a network index

`uv` built from source with the system toolchain (`gpu-stack` D8) resolves and installs from the curated local wheel directory. `pip` remains available inside a venv because the ecosystem expects it, and is invoked with `--no-index --find-links=<curated dir>` by everything this repository ships. The check in the task list rejects a shipped script that names a remote index.

The cost of "from source" was not recorded when D8 adopted it and is recorded now: `uv` is a Rust program, so this pulls a Rust toolchain into the base — from the pinned BLFS rendering, `general/rust.html` — and with it the bootstrap that language needs. That is a real dependency added to avoid vendoring one binary. It is defensible and it is not free, and the task list carries both branches rather than only the cheap-sounding one.

### D4 — One place per concern, and the resolved values are recorded

| Concern | Owner | Not allowed |
|---|---|---|
| Driver library visibility | ldconfig, per `gpu-stack` D4 | `LD_LIBRARY_PATH` in `profile.d` |
| Toolkit libraries | opt-in per consumer | registering `/usr/local/cuda/lib64` globally |
| NCCL and service env | `/etc/conf.d/<service>` (`honest-scope` D8) | inline `export NCCL_*` in launchers and init scripts |
| Python package set | the venv, from the lock | unpinned `pip install` |
| What a run actually used | the job record | nothing — it must be written down |

The launcher stops setting `CUDA_VISIBLE_DEVICES` and `NCCL_DEBUG` inline (`deploy_training_job.sh:56-57`, `ai-training:43-44`) and reads them from the conf file, then writes the resolved values into the job record. That is the difference between "the environment is configured in three places" and "the environment is configured in one place and every run says which values it got".

### D5 — The venv isolates Python packages and nothing else

`libcuda`, the CUDA runtime, cuDNN and NCCL are system-wide or wheel-bundled; a venv does not sandbox a process from the driver, from another user's GPU memory, or from the host. `docs/PHILOSOPHY.md:21` and `README.md:37` present venvs as the container replacement, which is true for dependency isolation and misleading if read as an isolation boundary. The requirement is one sentence in each document, and it costs nothing.

### D6 — The BLFS layer is either pinned or explicitly upstream-plus-delta

If an OpenRC edition of BLFS exists at a revision, pin it and follow it. If it does not, the layer is upstream BLFS with a per-package OpenRC delta (the init script, the runlevel, the configure flag that differs), and **that delta is the deliverable** — recorded per package, reviewable, and the thing a future reader trusts. What is not acceptable is `README.md:57`'s bare arrow, which currently asserts an edition without naming one.

### D7 — Hardware-dependent facts enter from `target-hardware-bring-up`

`TORCH_CUDA_ARCH_LIST`, "all GPUs are visible", "NCCL all-reduce completes on two GPUs", and any throughput number are outputs of a machine this project does not have. This change writes the shape of those checks and the record they produce; `target-hardware-bring-up` runs them. A `(run)` task with no host is blocked, per the convention in `openspec/project.md`.

### D8 — `ai-stack/*` is absorbed, with its defects named

`setup_cuda.sh` either installs the toolkit or is renamed to what it is (an environment-file writer) and stops citing a book that has no CUDA in it. `create_venv.sh` loses its seven unpinned installs. `install_pytorch.sh` is replaced by the curated-wheel path or the gated source build. The install guide is updated in the same task, because a guide that leads the scripts by one release is how the repo got here.

## Open questions

- **OQ1 (answered against the live book, retrieved 2026-11-08):** an OpenRC edition of BLFS exists — **BLFS (OpenRC Edition) r13.0-194, dated 2026-03-23**, rendered from SVN at `linuxfromscratch.org/~zeckma/openrc/blfs/svn/`. Three things survive the answer. First, the rendering is **seven months old**, and the book's own version page says a development rendering older than a month is probably out of sync — so every coverage statement below is "as of this rendering" and must be re-checked against a fresher one before it becomes a decision. Second, its version page says it **targets the LFS development book**, while `base-system-and-init` pins LFS **13.0-openrc stable**: the two books are not pinned to the same target, and that has to be reconciled before anything is installed from the BLFS side. Third, the rendered index (726 package links) **does not list** several packages this layer assumed — `chrony`, `tmux`, `ninja`, `meson`, `zstd`, `ccache`, `pkgconf`, and any BLAS/LAPACK (no index match; direct path probes return 404) — while listing OpenSSH, Python3, CMake, Git, FFmpeg, rsync, wget, screen, pciutils, lm-sensors, sysstat and logrotate. So D6's branch is "pin it **and** carry a per-package gap list", not "follow it".
- **OQ2:** which NCCL does a training process actually load (`gpu-stack` OQ4)? Resolve with `ldd` on the loaded `libtorch_cuda.so` in the chosen environment. If a wheel bundles NCCL, building NCCL from source is not part of this stack and `honest-scope-and-config-generation`'s NCCL work is configuration only.
- **OQ3:** does this project need `nvcc`, custom kernels, or profiling at all? That is the gate in D2, and it is a product question, not a build question.
- **OQ4:** where do curated wheels come from, and who signs them? The air-gap default in `gpu-stack` D8 says the operator's workstation; the trust chain (vendor index → workstation → hash list → install) needs one named owner or the hashes protect against nothing.

## Risks

- **The wheel default can be read as abandoning the philosophy.** It is not: the philosophy is control and no hidden layers, and a hash-pinned wheel installed offline with the runtime topology written down is more of both than an unpinned `pip install` pretending to be a source build.
- **BLFS derivation is the largest task in the change and depends on OQ1.** If no OpenRC BLFS exists, expect the delta-per-package route, which is slower and is the honest cost of the choice.
- **Three environment sources will come back** unless the check in the task list lands with the fix. The check is the requirement; the prose is the explanation.
