# 🗣️ Language and dependency policy

What language new code here is written in, what gets adopted instead of written,
and what never gets rewritten. Per-component verdicts — what is Rust on this
machine and what deliberately is not — live in
[`utils/rust-coverage.toml`](../utils/rust-coverage.toml), which is the policy
truth: a decision absent from that file has not been made. This document is the
reasoning that file cites.

## The selection order

1. **A maintained port.** Somebody upstream already builds and maintains it.
2. **Upstream as-is.** The project itself, unmodified or carried as a patch
   series.
3. **Do not adopt.**

"Rewrite it in our language" is not a step in this order. Two reasons, and the
second is arithmetic:

- **A rewrite has no upstream.** Every future CVE, every behaviour change, every
  edge case a user would have reported to a project with a thousand users
  arrives here, where the team is one operator and a repository.
- **The standing proposal to rewrite the toolchain cannot bootstrap itself.** A
  Rust compiler needs a linker, a libc, and LLVM — which are the C and C++
  artifacts the rewrite was meant to remove. The dependency is not incidental;
  it is the shape of a compiler.

## The accelerator path is excluded

`kernel`, the NVIDIA driver, the CUDA runtime, CPython, PyTorch. These are C and
CUDA **by construction**: the driver is a kernel module with a proprietary
userspace half, CUDA is a C ABI, and the two frameworks ship C++ and CUDA
kernels that CPython loads. A language policy that reaches them is a rewrite
program for the software the machine exists to run, and the thing it would
destroy is the reason the machine exists.

## Modified upstreams live as patch series, never as directories

A deviation from upstream is a `.patch` file named in the artifact's `base.lock`
entry — `coreutils-9.10-i18n-1.patch`, for example — so the diff against the
next release still exists. A copied directory does not record what was changed
or against what it was changed, and the first rebase turns it into a fork
nobody can update. This is also why `utils/` holds first-party code and no
directory in this repository holds a copy of someone else's project.

## Reach for these before proposing a rewrite

`uv`, `ruff`, `tokenizers`, `polars`. Each is a maintained upstream that ships
binaries or wheels, which is the whole content of step 1. The first is adopted
now; the rest are listed so the next consultation does not propose writing the
thing they already are. `tokenizers` and `polars` arrive as Python dependencies
inside a training environment, not as system packages — they are libraries the
frameworks use, and installing them machine-wide would put a second copy of a
library under the thing that pins its own.

## uv is the default environment manager

Adopted in place of `python -m venv` plus `pip`, and it is the mechanism that
carries what containers were being proposed for: **a locked environment**. One
file resolves once and installs byte-for-byte, with no daemon, no image layer,
and no second kernel between the training script and the GPUs.

The honest boundary: uv does not replace the venv — `uv venv` creates one. The
isolation is still the venv. What the lock adds is that the same file rebuilds
the same environment on a machine nobody has logged into, which is the property
anyone asking for Docker on this host actually wanted.

What adoption costs, stated so it does not arrive as a surprise:
`ai-stack/environments/create_venv.sh` and the install guide still say `venv`
and `pip`, and uv itself is a Rust binary that has to be built by the toolchain
(`docs/adr/0001`) and pinned like every other artifact — not fetched with
`curl … | sh`, which is how an unpinned dependency and an unreviewed script
both enter a machine that is supposed to be auditable.

## A replacement must meet three conditions

1. **Usage evidence.** The capability is used, observed on this machine.
2. **Parity evidence**, captured from the incumbent on this machine — not from
   its documentation, which describes the version someone else tested.
3. **A rollback that has been executed once.** A rollback never run is a
   hypothesis about the state the machine will be in at 3am.

And the incumbent is **named as moved, not deleted**. "We replaced X" is only
true while the way back exists; the ledger's `rollback` field is where it is
recorded, and every candidate in it currently says *not executed*, because
nothing has been installed anywhere yet.

## The standing proposals, judged against those conditions

| Proposal | Verdict | Why |
|---|---|---|
| uutils coreutils | **passes**, as a maintained port | It is step 1 of the order: an upstream that exists and ships. Passing is not a reason to move — GNU coreutils 9.10 is in `base.lock` with the book's i18n patch, and swapping a working core of the base system for a compatibility layer needs a stated requirement, not an available one. |
| Hand-written `syslogd` | **fails** | `sysklogd` 2.7.2 (troglobit) is already in the image — a maintained upstream, so this is step 1 already taken. A hand-written daemon fails usage evidence (the capability is provided) and gains no upstream. |
| Hand-written `cron` | **fails** | No cron is in `base.lock`, so the capability is genuinely missing — and the fix is a maintained cron, which exists. Parity evidence is unobtainable for an incumbent that does not exist, which is condition 2 failing before the others. |
| Hand-written OOM watchdog | **fails** | `earlyoom` is the maintained upstream. The mechanism decided here for OOM during training is `vm.overcommit_memory` plus a cgroup v2 memory limit (`docs/PHILOSOPHY.md`), which needs no daemon; if a responder is ever wanted it is `earlyoom`, and it has to state what it kills and why before it is installed. |
