# Pre-build notes

Working file for the work that happens **before** a build exists: the shipped
utilities, rewritten in Rust, and the record of every open item that a machine
— not a conversation — has to answer. Written instead of asked, so the build
starts with a list of unknowns rather than a pile of assumptions.

Nothing below is a question for the reader. Each item names the host class that
answers it (`(build)` / `(gpu)` / `(target)`, defined in
[`../openspec/project.md`](../openspec/project.md)) and the change that owns it.

---

## 1. Scope: what is Rust here, and what deliberately is not

The boundary is not "small code" and not "shell is bad". It is **whether the
code runs inside the distribution or inside the build of the distribution**.

| Path | Class | Language | Why |
|---|---|---|---|
| `utils/` | shipped runtime utilities | **Rust** | Executed by an operator or a service on a running host. Each one replaces a shell script whose failure mode is the thing being fixed (an exit code that never fires, a prompt that cannot be scripted, a write whose rejection is discarded). |
| `scripts/build/` | build tooling | shell | Runs before the toolchain it drives exists. A Rust orchestrator would have to be compiled by the toolchain it orchestrates — the language policy applied backwards. Owned by `build-orchestration`. |
| `ai-stack/` | install-time setup | shell | Runs once against a host that is being built. `userspace-and-ml-stack` absorbs these as lock-driven steps. |
| `openrc/init.d/` | service wrappers | shell | `openrc-run` is a shell dialect executed by the init system. Where a service needs real behaviour it calls a binary: `gpu-fancontrol` now calls `bmc-fan`. |
| kernel, graphics, conf.d, patches | configuration | — | No language to choose. |

The policy this sits under is `base-system-and-init` D4: selection order
(maintained port → upstream as-is → do not adopt), and a replacement needs
**usage evidence, parity evidence from the incumbent, and an executed
rollback**. The ledger that records those verdicts is
[`../utils/rust-coverage.toml`](../utils/rust-coverage.toml), and it is the
truth, not this file. All four binaries are `candidate` there — built, tested,
installed nowhere.

## 2. The binaries and their contracts

| Binary | Replaces | Exit codes |
|---|---|---|
| `bmc-health` | `scripts/maintenance/check_gpu_health.sh` | 0 every check ran and nothing was wrong · 1 a named condition is present · 2 a check could not be performed · 3 `nvidia-smi` absent |
| `bmc-vram` | `scripts/maintenance/clean_vram.sh` | 0 nothing to do / acted as asked · 1 refused or an action failed · 2 state undeterminable · 3 driver absent |
| `bmc-run` | `scripts/deploy/deploy_training_job.sh` | 0 started or planned · 1 refused · 2 undeterminable · 3 tmux or driver absent |
| `bmc-fan` | the `nvidia-smi -f` writes in `openrc/init.d/gpu-fancontrol` | 0 every write accepted · 1 a write refused · 2 unreadable · 3 driver absent |

The four codes are the same contract in all four binaries, and the point of
them is the difference between 1 and 2: **a check that could not be made is not
a pass**, which is the defect the shell versions had (`check_gpu_health.sh:50-58`
prints a temperature warning and exits 0; `:33` masks a failed field read with
`|| echo "N/A"`).

## 3. Build and verify

```bash
cargo build  --offline --release --manifest-path utils/Cargo.toml
cargo test   --offline           --manifest-path utils/Cargo.toml
bash utils/tests/parity.sh
```

`--offline` is not a convenience. The phase runs have the network denied
(`build-orchestration` D5), so a shipped component cannot carry a crate it did
not bring: the dependency graph is the workspace directory and nothing else.
`parity.sh` checks that claim rather than asserting it — `cargo tree` output
containing a crate without a local path fails the run.

`utils/tests/parity.sh` drives every binary against
`utils/tests/fake-nvidia-smi` (canned driver answers, scenario-selected) and
asserts exit codes: driver absent → 3, hot GPU → 1, unreadable persistence
field → 2, no devices → 1, dry run → 0, refused fan write → 1, out-of-range
device index → 1. It also runs the **incumbent** against the same canned output
and records that it exits 0 on a 91 °C GPU — the evidence half of D4 that a
machine without an NVIDIA card can produce.

## 4. What the build still has to do with these

Nothing below is decided by this file. Each line names the owner and stops.

- **The `base.lock` entry for `rust`** — `userspace-and-ml-stack` 5.1. `rustc`
  ships in the installed image: `docs/adr/0001-rust-toolchain-ships-in-the-image.md`.
- **The phase that compiles and installs `utils/`**, its position, its entry
  requirements and its exit criteria — `build-orchestration`, as the off-book
  phase of the phase table.
- **The installed paths**, including this ledger's — the same phase's `installs[]`.
- **The incumbents are not installed**, so the rollback D4 requires has never
  been executed. No spec carries this; the ledger states it and nothing here
  claims otherwise. This line is on lease: when the rehearsal's not-evaluable
  status gets a spec home — wave 7, or a maintenance rehearsal carrying the same
  greppable "expected until hosts exist" form as the cost threshold — the fact
  moves there and this bullet becomes a fifth pointer.

## 5. Decisions taken while writing, and what would reopen them

1. **Severity is not the exit code.** `Status::worst` ranks a confirmed problem
   above "we could not tell", so a run that finds a hot GPU and an unreadable
   field exits 1, not 2. The codes still distinguish them. Reopen if an
   unattended consumer needs "did everything run" separately from "is anything
   wrong" — this project has no such consumer by `maintenance-and-updates` D6.
2. **`--query-gpu=<fields>` and `-i <n>`** are the argument forms used, because
   they are the forms every `nvidia-smi` example and manual page uses. The
   space-separated and packed forms are undocumented and were the previous
   code's silent gamble.
3. **`bmc-run` has no compiled-in device default.** The shell version defaulted
   to `0,1` (`:8`), which is the duplicated default
   `userspace-and-ml-stack` 6.3 exists to delete. Device list comes from
   `--gpus`, then from `CUDA_VISIBLE_DEVICES` in `/etc/conf.d/nccl`, then the
   run is refused with both options named.
4. **The environment is written into the tmux pane's command**, not exported by
   the launcher. A detached tmux **server** that is already running supplies the
   new pane's environment, not the client, so on any host with a live session
   the shell version's exports never reached the training process. If the
   session mechanism changes (tmux has no page in the pinned BLFS rendering —
   `userspace-and-ml-stack` 0.7), this is the reason the fix is not a regression
   to plain exports.
5. **The job record is `/var/log/ai-training/job-record.tsv`**, one TSV line per
   launched job naming script, interpreter, devices, log, and where the NCCL
   values came from. The path is one `const`; `userspace-and-ml-stack` owns the
   question of where it should live.
6. **`bmc-fan` is the mechanism only.** No threshold, no interval, no fan curve,
   no safe-failure state — `gpu-stack` 7.1–7.3 own those, and a cooling policy
   invented inside a rewrite is the destructive case with no owner. Consequently
   `CHECK_INTERVAL` was deleted from `openrc/conf.d/gpu-fancontrol`: nothing in
   the service read it. `nvidia-smi -pm 1` was also dropped from the service,
   which already declares `need nvidia-persistenced`.
7. **`--percent 0` means "return control to the driver"** because that is the
   write the incumbent's `stop()` made. Whether the driver treats 0 that way on
   the cards this project targets is `(gpu)` and unverified.
8. **`/etc/conf.d/nccl` is read, never sourced.** Parsing `KEY=value` is not
   running root-owned shell as part of starting a job. A value needing
   expansion is a value this project has not decided to put in that file.
9. **Timestamps spawn `date`.** coreutils is in the base image by construction;
   a hand-rolled civil-time routine is forty lines of bug surface for one
   string.
10. **Notes on the incumbents go at the bottom of the file.** The specs cite
    their line numbers (`check_gpu_health.sh:33`, `clean_vram.sh:61`,
    `deploy_training_job.sh:56-57`); a header comment would silently invalidate
    every one of them.

## 6. Not ready, with owners

| Blocked | Needs | Owner |
|---|---|---|
| All four binaries | ~~A build host to install onto~~ — a disposable one now exists (`scripts/build/vm-host.sh`), so what remains is the phase that installs them, not the machine | `build-orchestration` 0.1, 0.3 |
| `bmc-health`, `bmc-fan`, `bmc-vram` | Fields verified 2026-11-11 (below). Remaining: a **rejected** fan write observed being reported, which needs a write, which needs root | `maintenance-and-updates` 6.3, 7.6; `gpu-stack` 7.4 |
| `bmc-run` | Whether tmux is the session mechanism at all, and where the job record lives | `userspace-and-ml-stack` 0.7, 6.3 |
| `bmc-fan` as a policy | Threshold source, interval, and the state the fans park in when the temperature read fails. Measurement bears on this: `fan.speed` reads **0 at idle** on real cards, so "0 %" is a normal reading and not a fire | `gpu-stack` 7.1–7.3 |
| Installation | A phase that compiles `utils/` after `rust` exists, offline, and installs the four binaries | `build-orchestration` |

### Measured on the first GPU host, 2026-11-11

`user@192.168.0.86`, 5× RTX 4060 Ti, driver 580.159.03. Read-only queries; nothing on
that machine was written to or restarted.

- **`pcie.replay.counter` does not exist on this driver** — exit 2, `Field … is not a
  valid field to query`. `check_pcie` reported `INDETERMINATE`, which is the design
  claim holding; the incumbent printed `N/A` and exited 0. That pair is the D4 parity
  evidence, produced by hardware rather than asserted.
- **A query matching no device exits 6** with `No devices were found` — not 0, not 1.
  `Smi::run` tested only `success()`, so the real driver routed "no GPUs" through the
  same error as "that field does not exist", and `fake-nvidia-smi` had been answering
  exit 0 with empty output, which is why the suite never saw it. `run` now maps exit 6
  to no rows; the existing `no devices visible exits 1` assertion passes against a fake
  that fails the way the real one does.
- **`fan.speed` is 0 at P8 idle on all five devices**, so a policy that reads 0 as
  "fans stopped" fires on a healthy idle card. Owner: `gpu-stack` 7.3.
- **`persistence_mode` is `Disabled` on all five** while three processes hold device
  memory — the condition `nvidia-persistenced` exists to remove, observed rather than
  reasoned about. Owner: `gpu-stack` 8.2.
- The incumbent, run against real hardware, **exits 0 while printing that all five
  GPUs exceed the threshold** (`maintenance-and-updates 6.2`), and its hot-GPU line
  breaks across five lines because `HOT_GPUS` carries newlines.

## 7. Readiness

Passing now, on a machine with no GPU and no network:

- [x] `cargo build --offline --workspace` — five crates, no external dependency
- [x] `cargo test --offline --workspace` — unit tests for the shared plumbing,
      device validation, and the fan range check
- [x] `bash utils/tests/parity.sh` — 20 assertions against canned driver
      output on Linux, 19 on a non-Linux dev host: the module/kernel record is a
      Linux file, so the case that needs one exists only there. First run on the
      target platform is what found that the healthy-driver assertion demanded
      an installed system from a fresh host, and that the record path had no
      test override while the driver path had one (`BMC_DRIVER_RECORD`, added
      with the mismatch case `maintenance-and-updates` D3 exists for)
- [x] every shell script still parses (`bash -n`), incumbents included

Not passing, because they need a machine:

- [ ] `base.lock` carries a `rust` entry with a book citation
- [ ] a phase compiles `utils/` offline and installs the binaries
- [ ] the ledger's four `candidate` entries become `installed`, with the
      rollback executed once
- [x] the `(gpu)` field verification in section 6 — performed 2026-11-11 against
      driver 580.159.03, where it corrected two assumptions the code had made

The machine for the first three now exists: `scripts/build/vm-host.sh` starts a
disposable KVM guest — Debian 13.7, 8 vCPU, 59 G free, passwordless root, its
whole disk a qcow2 file — which is what `build-orchestration 0.3` and design D10
turned on. Those three lines are blocked on the phases being written, not on
hardware. The guest has no GPU, so everything `(gpu)` stays on the substrate.
