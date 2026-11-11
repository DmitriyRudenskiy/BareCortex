# userspace-and-ml-stack

## Why

The repository jumps from a base system nobody has built to a Python venv, and the gap is where the actual work is. Nothing in this tree installs Python, a BLAS, a compiler driver wrapper, SSH, a time source, or tmux — and `README.md:57` names the layer that would ("BLFS (OpenRC Edition) → Server tools, Python, compilers") without pinning it, so the layer has no revision, no package list, and no owner.

Meanwhile the three scripts that do exist contradict the project and each other:

- `ai-stack/frameworks/install_pytorch.sh:44` installs a **wheel from `download.pytorch.org`** while the docs describe a from-source bare-metal stack. Worse, its own comment (lines 41–43) admits the index only carries specific CUDA point releases, and line 45 then builds the index URL from whatever `nvcc` reported — so on a CUDA release with no matching index the install fails at run time, on the target machine, at the worst moment.
- `ai-stack/environments/create_venv.sh:43-47` runs seven **unpinned, unhashed, network** `pip install`s. There is no lockfile, so "the environment that was verified" and "the environment that is running" are unrelated facts.
- `ai-stack/cuda/setup_cuda.sh` installs nothing — it writes env files — and cites "GLFS OpenRC Edition" (line 17) for a CUDA toolkit that book does not contain. It then writes `LD_LIBRARY_PATH` into `/etc/profile.d/cuda.sh` (line 36) **and** registers `/usr/local/cuda/lib64` with ldconfig (line 54), the two mechanisms `gpu-stack` D4 says must not both be used, plus `NCCL_DEBUG` in the same profile file, which `honest-scope-and-config-generation` D8 puts in `/etc/conf.d/nccl`.
- `scripts/deploy/deploy_training_job.sh:56-57` and `openrc/init.d/ai-training:43-44` set `CUDA_VISIBLE_DEVICES` and `NCCL_DEBUG` inline. With profile.d that is **three** places deciding one process's environment, and no record of which one won.

This change installs the layer, decides the one question the scripts dodge (who owns the CUDA runtime), and reduces environment truth to one place per concern.

## What Changes

- **The BLFS layer gets pinned like the LFS layer was** — edition, revision, per-package page — and its packages go into `base.lock` with the book they came from recorded per entry. One lock file, two source books, no second list.
- **The pinned BLFS edition turns out not to cover this layer, and that is recorded as a gap list rather than absorbed silently.** BLFS (OpenRC Edition) **r13.0-194** (2026-03-23, retrieved 2026-11-08) has OpenSSH, Python3, CMake, Git, FFmpeg, rsync, wget, screen, pciutils, lm-sensors, sysstat and logrotate in its index — and **no** `chrony`, `tmux`, `ninja`, `meson`, `zstd`, `ccache`, `pkgconf`, or any BLAS/LAPACK. It also states it targets the **LFS development** book while this project pins **LFS 13.0-openrc stable**. Each gap gets a named route, and the book-target mismatch gets reconciled before anything is installed from it.
- **The runtime topology is decided, not implied**: the training runtime is wheel-authoritative by default (curated, hash-pinned, installed `--no-index`), the system toolkit is a build dependency, and building PyTorch from source is a **gated deviation** with a stated reason. `gpu-stack` D3 currently describes both topologies and picks neither; this closes it.
- **Python dependencies are locked and installed offline.** A `pip install` that reaches a network index is a defect, and there is a check for it.
- **One place sets each piece of the runtime environment.** `/etc/conf.d/*` for services, the venv for Python packages, the job record for what a given run actually used. `profile.d` keeps interactive shells and never carries `LD_LIBRARY_PATH`.
- **A training job leaves a record** — command, resolved environment, GPU set, start, stop, exit status, log path — because "the job ran and then it didn't" is not an account.
- **GPU selection refuses instead of guessing**: requested devices must exist and be free, or the operator overrides explicitly.
- **`ai-stack/*` is absorbed**, each script's defect named in the task that fixes or deletes it, and `docs/INSTALL_GUIDE.md` follows the scripts rather than leading them.
- **Hardware-dependent validation is declared, not performed.** `torch.cuda.is_available()`, a two-GPU NCCL all-reduce, and `TORCH_CUDA_ARCH_LIST` need the machine; their shape is written here and their execution belongs to `target-hardware-bring-up`.

## Impact

- New capability: `userspace-and-ml-stack`. Affected: `ai-stack/**` (absorbed), `scripts/deploy/deploy_training_job.sh`, `openrc/init.d/ai-training`, `docs/INSTALL_GUIDE.md` phases 5 and 6 — which order the reader to run the scripts this change replaces — `README.md:30` ("maximum speed" is a measurement or a claim) and `README.md:57` (the bare BLFS arrow).
- Task-level dependencies, not change-level: `build-orchestration 2.1` (derives `base.lock`) blocks this change's BLFS derivation task; `base-system-and-init 3.1` (lock format) blocks it too; **`gpu-stack 1.3` owns the "which NCCL does a process loads" decision** and this change cites its answer instead of probing again; `target-hardware-bring-up 6.1` produces the GPU architecture list. This change blocks `maintenance-and-updates`'s update path for Python packages.
- Blocked on inputs that do not exist yet: a GPU architecture list, a wheel source of truth (OQ4), and the per-package route for the eight packages the pinned BLFS rendering does not cover. OQ1 is answered — BLFS (OpenRC Edition) r13.0-194, 2026-03-23 — and its answer created the coverage gap instead of closing the question. Tasks that need a missing input are marked, and a blocked task is not a done task.
- `openspec/project.md` conventions apply: no unmeasured performance numbers; `(build)`/`(gpu)`/`(run)` name the machine a task needs; hardware claims live in `target-hardware-bring-up`.
