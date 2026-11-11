# Tasks: gpu-stack

Scope note: nothing here runs on the **target**. Markers mean the classes in `target-hardware-bring-up` design D1 — `(build)` for toolchain work, `(gpu)` for anything that needs an NVIDIA GPU present, which a build VM does not have. Group 0 names those hosts or blocks the tasks that need them; group 8 lists what leaves this change because no host in it can answer.

## 0. Preconditions

- [ ] 0.1 Name the build host, or record that none exists — this gates 2.5, 2.6, 2.8, and 6.1 **(build)**
- [x] 0.2 **Named 2026-11-11: `user@192.168.0.86`** — 5× GeForce RTX 4060 Ti, driver **580.159.03**, `nvidia-smi` at `/usr/bin/nvidia-smi`, `topo -m` answers. It can answer 6.1's `ldconfig -p` run, 7.2's threshold source, 7.4's accepted-write rule, 8.2/8.6's `man nvidia-persistenced` — the binary is `/usr/bin/nvidia-persistenced` and its page is `man1/nvidia-persistenced.1.gz`, not man8 — and the field verification `maintenance-and-updates 6.3` asks for. It cannot answer 1.3 (no PyTorch installed) or 2.6 (no GPU binaries built here). Three of the five devices are held by another user's processes, which is the condition `maintenance-and-updates 7.6` needs rather than an obstacle to it. It is **not** a build host — see `build-orchestration 0.1`
- [ ] 0.3 Record how a binary artifact reaches the build: the named location an operator places a downloaded NVIDIA artifact in, the filename it must have, and what the build does when it is absent — stop with the artifact name and the URL, never skip the component silently. This is the missing half of the `binary-authenticated-fetch` class
- [ ] 0.4 Confirm 0.3's procedure is written where an operator will find it, not only in this file

## 1. Resolve the open questions first

- [ ] 1.1 Read the CUDA Toolkit and cuDNN licence texts and record what they permit for fetching, caching, and redistribution (OQ1). The answer decides whether `binary-authenticated-fetch` stays one class and whether an internal mirror is legal
- [ ] 1.2 Record what the driver installer emits about installed files and choose the recording mechanism (OQ3), from its own options rather than from assumption
- [ ] 1.3 **(gpu)** In the default environment, run `ldd` on the loaded `libtorch_cuda.so` and record which NCCL resolves (OQ4). **This change owns the answer** — `userspace-and-ml-stack 1.1` references this task instead of repeating the probe. If it is the framework's bundled copy, decide here whether source-built NCCL is part of this stack at all
- [ ] 1.4 Record OQ2 (`nvidia-open` vs proprietary module) as explicitly deferred to the target hardware, with the table that will decide it

## 2. Component inventory and `base.lock` entries

- [ ] 2.1 Write the component table: kernel module, userspace driver, CUDA Toolkit, cuDNN, NCCL, `nvidia-persistenced` — source URL, licence, provenance class, install document
- [x] 2.2 Added to `class-map.toml`: `source-built` requires `name`, `version`, `url`, `md5`, `pages`; `binary-distribution` requires those plus `licence`, `doc`, `installed_files` — the file set a vendor installer writes outside the build's view, without which an image build cannot tell installed files from unaccounted ones; `binary-authenticated-fetch` requires `name`, `version`, `md5`, `licence`, `doc`, `supplied_dir`, `supplied_name`, the last two being what an operator is told to place and name. Each row names `gpu-stack` as its owner. `scripts/build/check-base-lock.py` is the "whatever reads it": it reads the rows and their `requires` from the map, so the count is nowhere written down, and it prints `class: binary-distribution owner=gpu-stack entries=0` — the class exists, its fields are known, and nothing satisfies it yet, which is the red state and is visible in the line

- [ ] 2.3 Add `base.lock` entries for each binary component, with artifact name and checksum
- [ ] 2.4 Record the installer's installed-file set as the `base.lock` file set for the driver (from 1.2)
- [ ] 2.5 **(build)** Demonstrate the failure: an image containing an unaccounted GPU binary fails the build. The rule itself belongs to `base-system-and-init`'s unaccounted-package requirement — reference it, do not restate it, and add only the GPU-binary case **(build)**
- [ ] 2.6 **(build)** Confirm the no-systemd link scan from `base-system-and-init` runs over these binaries too, and record what it finds — a nonzero count is the expected first outcome
- [ ] 2.7 Add the `nvidia.kmod = open|proprietary` `base.lock` flag, selected from the GPU architecture rather than assumed, with the GSP firmware load check as its gate and the fallback documented
- [ ] 2.8 Choose the mechanism that captures an installer's written file set (an `install -D` wrapper recording each path is the candidate) and record it as the answer to OQ3
- [ ] 2.9 Write the required fields of the `binary-distribution` row: source URL, artifact name, checksum, licence, installation document. Until this lands the completeness check is **red naming this change**, and that red is the expected state of the map, not a regression — the row is unwritten because no session that does not own the class wrote it
- [x] 2.10 Recorded as the fourth row of `class-map.toml`, owned here and required to carry `name`, `version`, `scope`, `licence` — `scope` being the build scope ADR 0004 makes its version (the crate for a compiled binary, the file for a copied one), which is why the class cannot be folded into `source-built`: that would weaken "a book page" to "a page or a path" for all 88 source-built packages. The row exists before any entry does, so `class: first-party owner=userspace-and-ml-stack entries=0` is the honest current state — the off-book phase that populates it is `build-orchestration` group 3

- [ ] 2.11 Record which change owns each class row, so that a failing completeness check names an owner and not a file

## 3. Replace the GLFS citation

- [ ] 3.1 Rewrite `docs/INSTALL_GUIDE.md` phase 3: drop "GLFS OpenRC Edition" and cite the NVIDIA datacenter driver installation guide, the CUDA Installation Guide for Linux, and the CUDA Compatibility guide
- [ ] 3.2 State in the same section that compute work needs no X server, and that the headless default in `docs/PHILOSOPHY.md:45` is not a limitation to work around
- [ ] 3.3 Check every URL added resolves, and record the check

## 4. The from-scratch claim

- [ ] 4.1 Rewrite `README.md:41` ("From scratch — Built with LFS + BLFS + GLFS"): name the real base book and state the GPU-stack exception on the same line or the one below
- [ ] 4.2 Rewrite `README.md:40` and `README.md:112` ("Zero abstraction", "Zero overhead") so the proprietary driver and CUDA userspace are named as the layer they are
- [ ] 4.3 Check that no other file repeats the from-scratch or zero-abstraction claim without the exception

## 5. Version pairing

- [ ] 5.1 Record the driver-branch selection procedure: GPU architecture in, branch and module flavour out, with the supported-architecture table cited by URL
- [ ] 5.2 Record the driver/CUDA pairing rule with the compatibility table cited, and the two commands that show each half on a running host
- [ ] 5.3 Remove any branch or version number that has no hardware fact behind it, including "r550/r580 or newer" if it entered the tree
- [ ] 5.4 State what breaks when one half of the pair is updated, and what has to be re-checked first

## 6. Loader and service environment

- [ ] 6.1 **(build)** Add loader configuration entries owned by the installation, and the `ldconfig -p` check that shows the libraries resolve. Running the check is `(gpu)` work; writing it is not, and the two are recorded separately
- [ ] 6.2 Implement the check that fails when a service's environment reaches a library path through a login profile, and run it over the whole tree — an audit that runs once does not keep the rule true. Shared with `userspace-and-ml-stack 6.4`; one mechanism, one owner, named there
- [ ] 6.3 Make `openrc/init.d/nvidia-persistenced` and any training service resolve the stack without a login profile, and state which file gives them their environment
- [ ] 6.4 Reconcile this with `honest-scope-and-config-generation` D8 (single source in `/etc/conf.d`) so there is one mechanism for service environment, not two
- [ ] 6.5 Declare the default CUDA-runtime owner (design D3, answered by 1.3) in the install guide, with the other topology documented as a supported deviation
- [ ] 6.6 Split the loader rule and write it down: driver libraries in `/usr/lib` are registered and exist once system-wide; `/opt` toolkit libraries are opt-in per consumer and are not registered; neither reaches a service through a login profile
- [ ] 6.7 Record the CUDA toolkit component list that is pruned after install, keeping the CLI profilers, and state what breaks if a pruned component is later needed

## 7. GPU fan control

- [ ] 7.1 Specify what `openrc/init.d/gpu-fancontrol` decides and from what input — it ships at 61 lines with a `conf.d` file and is specified in no change, which makes it the only shipped service whose behaviour nobody owns
- [ ] 7.2 State the threshold source: which `nvidia-smi` or NVML field, at what interval, and what the service does when the read fails. A cooling service that fails silently is worse than no cooling service
- [ ] 7.3 Define the safe failure state explicitly — a fan policy that parks fans because a temperature read returned empty is the destructive case — and name which stop level that leaves
- [ ] 7.4 Confirm the service reports success only when a write was accepted, matching `base-system-and-init`'s rule that success requires having done the work
- [ ] 7.5 Record whether this service should exist at all against the driver's own fan management, and record the decision either way

## 8. Verification reference

- [ ] 8.1 Write the verification command table from design D6, each row naming what a pass establishes and what it leaves unproven
- [ ] 8.2 **(gpu)** Describe `nvidia-persistenced` by what it does — driver state across process exits and driver unloads — and add the task to verify against `man nvidia-persistenced` on the installed driver
- [ ] 8.3 State that `nvidia-smi topo -p2p r` reports availability (`OK`/`ERR`/`UNS`) and not bandwidth, so the distinction is not lost in the next consultation
- [ ] 8.4 Confirm no measured number from hardware appears anywhere in this change
- [ ] 8.5 Document device-node creation without the setuid helper: `NVreg_CreateDeviceFiles=1` as primary, explicit creation as fallback, chosen by a probe at install time, and state why the setuid path was rejected
- [ ] 8.6 Describe persistence at the granularity that is true — the driver stays initialised across **job start and stop** — and keep the task that verifies it against `man nvidia-persistenced`

## 9. Handed to `target-hardware-bring-up`

Each row names the task there that receives it, so the delegation is checkable in both directions.

- [ ] 9.1 Running any verification command in 8.1's table → that change's 4.1–4.5
- [ ] 9.2 `nvidia-smi topo -m` / `-p2p r` output, `nccl-tests` results, any bandwidth or scaling figure → its 5.1–5.5
- [ ] 9.3 Choosing the driver branch for the actual GPUs, which 1.4 defers → its 0.1 and 5.1
- [ ] 9.4 The `ldconfig -p` run 6.1 writes → its 4.6
- [ ] 9.5 The `man nvidia-persistenced` reading in 8.2 and 8.6, which needs an installed driver → its 4.3
- [ ] 9.6 Multi-node fabric: InfiniBand, RDMA, fabric managers — not deferred, out of scope with the single-node decision

## Self-Review

Generated from `tasks.md` against the two spec files rather than maintained by hand: re-run after any edit, since a hand-kept mapping is exactly the artifact that goes quietly stale.

- **Spec coverage** — `gpu-stack`: 6 requirements → groups 2 (provenance), 4 (claim), 5 (pairing), 6 (runtime owner, loader), 8 (verification). Fan control (group 7) is a new requirement, added because the shipped service had no owner.
- **Dependency order** — task-level edges, not change-level: `base-system-and-init 3.1` owns the `base.lock` format this change extends in 2.2–2.4, and its link scan is used in 2.6. `honest-scope-and-config-generation` owns the kernel config this driver compiles against.
- **Why two changes and not one** — the paper work is answerable now; the hardware work is blocked on a machine. Keeping them separate stops the first from inheriting the second's blockage, which is what group 0 makes visible instead of hiding.
- **What stays unknown** — OQ1 (licence terms), OQ2 (module flavour), OQ3 (installer output), OQ4 (which NCCL loads). None is guessed at in a task; 1.1–1.4 close them from primary sources, and 1.3 needs the host 0.2 names.
