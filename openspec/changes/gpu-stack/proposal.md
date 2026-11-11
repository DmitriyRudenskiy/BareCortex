# Proposal: gpu-stack

## Why

Everything between the kernel and a training run — the NVIDIA kernel driver, the userspace driver, CUDA Toolkit, cuDNN, NCCL, `nvidia-persistenced` — is the subject of this project and is specified nowhere in it. `docs/INSTALL_GUIDE.md:55` sends the reader to GLFS for "NVIDIA Drivers & CUDA", and GLFS is *Gaming* Linux From Scratch. The repository ships an `openrc/init.d/nvidia-persistenced` service for a daemon whose installation is not described in any file in the tree.

The uncomfortable part is not the missing chapter. It is that this stack breaks the project's own claims. `README.md:41` says **"From scratch — Built with LFS + BLFS + GLFS"** and `README.md:40` says **"Zero abstraction between your code and the GPU"**, with `README.md:112` repeating *"Zero overhead. Zero abstraction."* The GPU driver is a proprietary binary distribution with a closed userspace component, and CUDA Toolkit and cuDNN are published as binaries under NVIDIA's own licence. The single most important component group in the system is the one that cannot be built from source and is the thickest layer between the code and the hardware, and no document says so. A reader who discovers this on their own concludes the rest of the claim needs auditing too.

There is a second consequence that is easy to miss and expensive to learn late. A CUDA Toolkit installed "globally to `/opt/cuda`, with paths in `/etc/profile`" does not reach an OpenRC service, because services do not read login profiles — this is the same defect already found in the NCCL environment file, where the runtime settings lived in `profile.d` and no service sourced it. And a PyTorch installed from wheels carries its own CUDA runtime, cuBLAS and cuDNN libraries, so a system-wide Toolkit is not what makes it run. The advice to install CUDA globally and the advice to use `venv` are both correct and, together, half-redundant. Which of the two layouts is BareCortex's is an unmade decision, and it decides what `base.lock`, the linker configuration and the reproducibility story have to contain.

## What Changes

- New capability `gpu-stack`: every component between kernel and training run is named, with its source, its provenance class, and the document it is installed from.
- **Provenance becomes a recorded field, not an implication.** Each component is `source-built`, `binary-distribution`, or `binary-authenticated-fetch` (download requires an NVIDIA developer login, so a human performs it and the build verifies a checksum). The requirement from `base-system-and-init` that nothing enters the image unaccounted extends to blobs: a binary with no `base.lock` entry is a build failure, same as a source package. Where the artifact has to be carried by hand, the drop location and the missing-artifact behaviour are written down, because "the build accepts a local artifact" is not an instruction an operator can follow.
- **The README's "from scratch" and "zero abstraction" claims state their exception.** The GPU stack is binary, named as the exception, with NCCL named as the exception to the exception — NCCL is open source and is built here.
- **The driver branch is derived, not suggested.** Which driver branch and which kernel-module flavour (`nvidia-open` or the proprietary module) apply is determined by the GPU architecture against NVIDIA's supported-architecture table, recorded per target. A branch number taken from a conversation is not an input.
- **Driver and CUDA versions are recorded as a pair** with the compatibility table row that permits the pairing, and the command that shows the pair on a running host.
- **Libraries are registered, not exported.** Loader paths go through `ld.so.conf.d` entries owned by the installation; `LD_LIBRARY_PATH` is not the mechanism. What remains in the environment is documented per consumer, and services get their environment from a source they actually read.
- **Verification commands are documented with what each proves and what it does not.** `nvidia-smi topo -m` reports affinity, `nvidia-smi topo -p2p r` reports P2P availability, `nccl-tests` measures the collective. None of them run in this change.
- **Hardware validation is explicitly out.** Running any of it, and every claim that depends on having run it, belongs to `target-hardware-bring-up`. This change is the second of the two changes the user asked for; the split is the point.
- **`gpu-fancontrol` gets a specification or gets deleted.** It ships as 61 lines of init script plus a `conf.d` file, controls cooling, and is described in no change — the only shipped service in the tree whose behaviour nobody owns. Its thresholds, its input, and the state it leaves the hardware in when the temperature read fails are decisions, and one of them is destructive.

## Impact

Affected specs: `gpu-stack` (new). No existing capability is modified — `base-system` and `init-system` exist only as a delta inside `base-system-and-init` and are not yet in `openspec/specs/`, so this change depends on that one landing first and says so in prose rather than editing a spec that does not exist yet.

Affected code: `docs/INSTALL_GUIDE.md` (phase 3 rewritten), `README.md:40,41,112` (the from-scratch and zero-abstraction claims), `base.lock` entries, `openrc/init.d/nvidia-persistenced`, `openrc/init.d/gpu-fancontrol` and its `conf.d`, new loader and environment files. `docs/PHILOSOPHY.md` keeps its headless default — this change adds no X server.

**Task-level edges** (change-level "depends on" is what let two changes both think they owned a decision):
- `base-system-and-init 3.1` owns the `base.lock` format this change extends in 2.2–2.4.
- `base-system-and-init`'s unaccounted-package rule is the one 2.5 demonstrates for GPU binaries; the rule is not restated here.
- `base-system-and-init 5.x`'s link scan is run over these binaries in 2.6.
- `honest-scope-and-config-generation` owns the kernel config this driver compiles against, and D8 owns the single service-environment source reconciled in 6.4.
- `userspace-and-ml-stack 1.1` consumes the CUDA-runtime-owner answer this change produces in 1.3 and 6.5; this change owns that decision.
- `target-hardware-bring-up` 4.1–4.6, 5.1–5.5 receive what group 9 hands over.

Risk: the cuDNN fetch is not automatable, so a fully unattended build is not achievable for the whole stack and the documentation must stop implying one. Open questions below name what has to be read before tasks can be estimated.
