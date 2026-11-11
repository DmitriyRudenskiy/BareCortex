# Tasks: userspace-and-ml-stack

Scope note: nothing here needs the **target** machine. Markers mean the classes in `openspec/project.md`: `(build)` is toolchain work on a VM, `(gpu)` needs an NVIDIA GPU present, `(run)` needs the target and is executed by `target-hardware-bring-up`. Install tasks in groups 3–5 are `(build)` phase work and are marked; a task whose class its group 0 does not name is blocked, not schedulable. A blocked task is recorded as blocked, not checked.

## 0. Inputs this change does not have

- [ ] 0.1 Re-verify the BLFS pin against a rendering newer than the one this change was written against: OQ1 is answered — **BLFS (OpenRC Edition) r13.0-194, dated 2026-03-23**, retrieved 2026-11-08 from `linuxfromscratch.org/~zeckma/openrc/blfs/svn/` — but the book's own version page warns that a development rendering older than a month is probably out of sync. Record the revision actually pinned, and re-run the coverage check in 0.7 against it
- [ ] 0.2 Name the wheel source of truth and its owner (OQ4): where artifacts come from, who verifies them, where the hash list lives, and what the chain defends against
- [ ] 0.3 Record the GPU architecture list as an input from `target-hardware-bring-up`, or record that it does not exist yet — this gates the source-build deviation and every `(run)` task in group 8
- [ ] 0.4 Answer OQ3: does this project need `nvcc`, custom kernels, or profiling? The answer selects the default in D2 and decides whether the source-build path is documented as likely or as remote
- [ ] 0.5 Confirm `build-orchestration 2.1` and `base-system-and-init 3.1` have produced a `base.lock` with a format this change can extend; if not, this change writes no package list
- [ ] 0.6 Reconcile the two books: BLFS r13.0-194 states it targets the **LFS development** book, while `base-system-and-init` pins **LFS 13.0-openrc stable**. Record which base the BLFS instructions are valid against, and what is done about the packages whose instructions assume the other one. **Groups 2–4 are blocked until this is recorded**
- [ ] 0.7 Carry the per-package gap list from the coverage check: `chrony`, `tmux`, `ninja`, `meson`, `zstd`, `ccache`, `pkgconf` and BLAS/LAPACK appear in no index entry of the pinned rendering, while OpenSSH, Python3, CMake, Git, FFmpeg, rsync, wget, screen, pciutils, lm-sensors, sysstat and logrotate do. For each gap, name the route: a different chapter of the same book, upstream BLFS as an explicit deviation, or a self-authored instruction set owned by this repository. **Groups 3 and 4 are blocked for the packages this names** — including the time source and the multiplexer `deploy_training_job.sh:41-44` already requires

## 1. Close the questions this change owns

- [ ] 1.1 Consume the CUDA-runtime answer from `gpu-stack 1.3`, which owns that decision and runs the `ldd` probe on a `(gpu)` host. This task records which NCCL a training process loads here and whether source-built NCCL is part of this stack at all — it does not run a second probe and record a second answer
- [ ] 1.2 Record the D2 decision in the install guide: default topology, the deviation and its gate, and which library set a training process loads
- [ ] 1.3 Record what the venv does not isolate (driver, CUDA runtime, cuDNN, NCCL, other users' GPU memory) and put the sentence in `docs/PHILOSOPHY.md` and `README.md` where the container replacement is claimed
- [ ] 1.4 Record the trust chain from D3 in one paragraph: what a hash verifies and what it cannot, given keys are not verified

## 2. Derive the layer into the one package list

- [ ] 2.1 Extend the `base.lock` format with `book=` and `page=` per entry, keeping LFS entries valid, and record one worked entry from each book
- [ ] 2.2 Derive the BLFS package set from the pinned edition (or upstream plus the per-package OpenRC delta, per 0.1) into `base.lock`, with no package typed by hand
- [ ] 2.3 For every package where the route is upstream-plus-delta, record the delta: the init script, the runlevel, the configure flag, and the page it deviates from
- [ ] 2.4 Extend the citation check from `build-orchestration 2.5` to cover entries from both books, and demonstrate it rejecting a BLFS entry with no page
- [ ] 2.5 Record which packages this layer deliberately does **not** install, with the reason, so absence is a decision rather than an oversight

## 3. Server core: the door in, the clock, the session

- [ ] 3.1 `(build)` Install the build tooling the layer needs (CMake, Ninja, pkgconf, ccache, Git) as `base.lock` entries, and record which build in this repository requires each. Ninja, pkgconf and ccache have no page in the pinned BLFS rendering — route them through 0.7 before this task runs
- [ ] 3.2 `(build)` Install and enable OpenSSH with key-only authentication, root login restricted, and an explicit user list; record the config file as the source and the restart that applies it
- [ ] 3.3 `(build)` Install and enable chrony client-only, and record why the clock matters here (log ordering, TLS validity, collective-communication timeouts) without claiming a drift figure nobody measured. Chrony has no page in the pinned rendering; 0.7 names its route
- [ ] 3.4 `(build)` Install tmux for multi-hour sessions, and record it as the mechanism `deploy_training_job.sh` already assumes (`:41-44` fails without it). Same gap as 3.3
- [ ] 3.5 `(build)` Verify from a fresh install that the services come up enabled after reboot and that SSH accepts a key and refuses a password — recorded as command output, not as an intention
- [ ] 3.6 Record the SSH hardening baseline's failure mode: what a locked-out operator does, given this is the only door in

## 4. Python and the numerical base

- [ ] 4.1 `(build)` Install Python per the pinned route, with `venv` and `ensurepip` functional, and record the version check that proves the pinned version is the one installed
- [ ] 4.2 `(build)` Install the BLAS/LAPACK the numerical stack will link against, and record which library a NumPy import actually resolves — the difference between "OpenBLAS is installed" and "NumPy uses it". No page in the pinned rendering; 0.7 names the route
- [ ] 4.3 `(build)` Install the image and codec libraries dataset loading needs, and record which loader requires which library rather than installing a media stack on a headless host
- [ ] 4.4 `(build)` Record a baseline for the numerical base (the benchmark the library itself ships, its own units) so a later "faster" claim has something to be faster than. No target machine needed; no claim made from it here
- [ ] 4.5 Record what is deliberately not installed (X server, desktop stack, GUI profilers) and how an occasional graphical window is served instead

## 5. The runtime and its wheel set

- [ ] 5.1 `(build)` Build `uv` from source with the system toolchain and record it as a `base.lock` entry, per `gpu-stack` D8. **Its prerequisite is a Rust toolchain**, which nothing in this project installs: record the `rust` entry from the pinned BLFS rendering (`general/rust.html`), what it pulls in, and the build cost it adds — adopting uv from source is not a one-package decision. If the cost is unacceptable, the alternative is a vendored binary `uv` recorded with `gpu-stack`'s `binary-authenticated-fetch` class, and that is a decision to record, not to discover halfway through. **Measured 2026-11-11 on the disposable guest, and out of order:** a minimal rustup toolchain (rustc 1.99.1 + cargo + std) is **0.60 GB** installed; the five `utils/` crates build `--offline --release` in **4.1 s** on 8 vCPU into a 52 MB target directory, yielding four binaries of 352–404 KB. That is not the figure ADR 0001 asks for, twice over: it is Debian's rustup rather than the BLFS `rust` package with its own pulls, and no threshold was fixed before measuring, so it is evidence and not a verdict. The threshold still waits on the rootfs budget (`target-hardware-bring-up` 1.7)
- [ ] 5.2 Define the curated wheel directory layout, the hash list format, and the populate procedure run on the operator's machine
- [ ] 5.3 Produce the dependency lock for the training runtime with a hash per artifact, and record the resolution as data rather than as a `pip install` command
- [ ] 5.4 `(build)` Implement the offline install path (`--no-index`, local directory, hash verification) and verify it fails naming the artifact when the directory lacks it
- [ ] 5.5 Implement the check that rejects a shipped script naming a remote package index, and demonstrate it failing on `install_pytorch.sh:44` and `create_venv.sh:43-47` before either is fixed
- [ ] 5.6 Write the source-build deviation as a gated procedure: required inputs (architecture list, toolkit, host, reason), expected build cost, and the verification that a build for the wrong architecture is detected rather than shipped
- [ ] 5.7 Record which framework extras (vision, audio) are not installed until a workload needs them, and where that need is tracked

## 6. One owner per environment concern

- [ ] 6.1 Remove `LD_LIBRARY_PATH` from the generated `profile.d` file and remove the global ldconfig registration of the toolkit library directory, leaving driver visibility where `gpu-stack` D4 puts it
- [ ] 6.2 Move every `NCCL_*` value out of `profile.d`, the launcher and the init script into `/etc/conf.d/`, per `honest-scope-and-config-generation` D8, and record which file owns which variable
- [ ] 6.3 Make `deploy_training_job.sh` and `openrc/init.d/ai-training` read the conf file instead of setting variables inline, and delete the duplicated defaults
- [ ] 6.4 Implement the check that fails when one variable is set in two places, and run it over the whole tree — including `graphics/` and `openrc/`
- [ ] 6.5 Rename or rescope `setup_cuda.sh` to what it does, and replace the "GLFS OpenRC Edition" citation (line 17) with the change and task that actually install the toolkit
- [ ] 6.6 Reconcile the venv path convention: `create_venv.sh` creates `/opt/ai/<project>/venv` while `ai-training:10` defaults to `/opt/ai/venv/bin/python`. Pick one, fix the other, and make the default fail loudly rather than silently pointing at a nonexistent interpreter

## 7. Job launch and the record

- [ ] 7.1 Define the job record: command, resolved environment, devices, start, log path, exit status, and where it lives
- [ ] 7.2 Write the record on start and update it on completion, and make "status never observed" a distinct value from "succeeded"
- [ ] 7.3 Implement device validation: requested devices exist, and are refused when held by another process, with the owner named and an explicit recorded override
- [ ] 7.4 Refuse a second start for a project with an unresolved record on the same devices, and list unresolved records in the status output
- [ ] 7.5 Fix the log path so the file exists before the process starts and the record points at it, and `(build)` verify the current `tmux ... | tee` construction actually captures output when the session is detached
- [ ] 7.6 `(build)` Start a dummy job that exits non-zero and verify the record shows the failure — a record that can only say "started" is the inert assertion this project has already been bitten by
- [ ] 7.7 Document the operator's view: list running jobs, attach, tail the log, and what each command reads

## 8. Validation shape, executed on the target

- [ ] 8.1 Write the device-visibility check (count, names, driver version) as a script producing a record, and mark it `(run)` for `target-hardware-bring-up`
- [ ] 8.2 Write the collective-communication smoke check for the declared device set, mark it `(run)`, and state what a pass proves about NCCL and what it does not prove about performance
- [ ] 8.3 Write the mixed-precision step check, mark it `(run)`, and record it as a correctness check rather than a speed claim
- [ ] 8.4 `(gpu)` Write the "which libraries did the process load" check (`ldd` plus the loaded map) and make it part of the environment record, so OQ2 has a repeatable answer instead of a one-time observation. `ldd` against a built `libtorch_cuda.so` needs a GPU host, not a build VM

## 8a. The operator-facing docs this change invalidates

The guide currently orders the reader to run exactly the scripts this change deletes; leaving it in place means the change ships a lie next to the fix.

- [ ] 8a.1 Rewrite `docs/INSTALL_GUIDE.md` phase 5 (`:94-101`): it instructs `setup_cuda.sh`, `create_venv.sh` and `install_pytorch.sh`, the three scripts groups 5–6 replace. Point it at the curated wheel install and the venv convention chosen in 6.6
- [ ] 8a.2 Rewrite phase 6 (`:105-118`): `nvcc --version` and `import torch` are not verification of an installed system, and the line labelled "Run NCCL test (if installed)" runs `deviceQuery`, which tests neither NCCL nor anything multi-device. Each line names what it proves and which change runs it
- [x] 8a.3 Fix `README.md:57`'s bare "BLFS (OpenRC Edition) →" arrow — cited in this change's proposal as a defect and left unowned until now. Name the pinned revision and what the arrow was meant to say
  - Closed by the README stack-diagram rewrite done under `base-system-and-init` 2.3. The row reads `BLFS r13.0-194 (svn) → Server tools: OpenSSH, Git, CMake, curl` — revision and contents. The two arrows beside it were corrected in the same block: `LFS 13.0-openrc` now notes that Python, Ninja and Meson are LFS packages in this edition, and the GLFS arrow became the NVIDIA and CUDA guides.
- [ ] 8a.4 Grep the docs for any remaining instruction to run a script this change changes, and confirm each is rewritten or explicitly marked superseded with a task reference
- [ ] 8.5 Record every group-8 task as not yet run, and remove or amend any document that presents framework-on-GPU operation as established

## 9. Honesty checks on this change

- [ ] 9.1 Confirm no document claims the framework runs on the GPUs, and no throughput or speed number appears without a measurement, host, date and revision
- [ ] 9.2 Confirm `README.md:30` ("Run Python-based ML frameworks at maximum speed") is either backed by a recorded measurement or rewritten as a goal
- [ ] 9.3 Confirm `base.lock` is the only package list, that every entry cites a book or a vendor, and that the citation check runs in CI or as a pre-merge step
- [ ] 9.4 Confirm no shipped script reaches a network package index, and that the check enforcing it has been demonstrated to fail on the pre-fix scripts
- [ ] 9.5 Confirm every `(build)`, `(gpu)` and `(run)` task's evidence is recorded output, and that blocked tasks are labelled blocked rather than complete
- [ ] 9.6 Archive with `--skip-specs` while tasks remain unexecuted, per `openspec/project.md`
