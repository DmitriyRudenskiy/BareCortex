# Design: base-system-and-init

## Context

The project's build story is three documentation links. Two return 404 (`lfs/view/stable-openrc/`, `blfs/view/stable-openrc/`); the third resolves to *Gaming* Linux From Scratch and is cited as the source of the NVIDIA driver and CUDA Toolkit. Verified directly, not inferred.

**Correction, verified 2026-11-08 against linuxfromscratch.org.** An OpenRC edition of LFS does exist; it is not in the main view index, which is why probing `/lfs/view/stable-openrc/` returned 404 and was read as absence. It is published at `linuxfromscratch.org/~zeckma/openrc/lfs/` (`13.0/`, `stable/`, `development/`), titled **Version 13.0-openrc**, with OpenRC built in `chapter08/openrc.html` and an OpenRC usage chapter. Two facts from the same probe change the shape of this decision:

- **LFS 13.x publishes no SysVinit edition.** `lfs/view/13.0/` and `13.1/` return 404; only `-systemd` exists. The SysVinit edition stops at 12.4, which is what `/lfs/view/stable/` still serves. "Base = SysVinit edition" is therefore a decision to freeze two majors behind, not a neutral choice.
- **BLFS has no released OpenRC edition.** `~zeckma/openrc/blfs/` contains `svn/` only — an SVN rendering, pinned by this project as **r13.0-194, dated 2026-03-23**. The server-stack half of the story therefore has a development snapshot and no release, and that snapshot **targets the LFS development book**, not the 13.0-openrc stable base pinned above. The mismatch is this change's to carry, since this change owns the base pin: any BLFS instruction that assumes a newer base, or a package the pinned base does not provide, is checked against the pinned edition before it is followed. Coverage is also incomplete — the rendering's index has no page for `chrony`, `tmux`, `ninja`, `meson`, `zstd`, `ccache`, `pkgconf`, or BLAS/LAPACK — so "install it from BLFS" is not available for those and each needs its own upstream source.

So the OpenRC layer is not BareCortex-owned software with no upstream, as this design first said. It is someone's personal directory on the project's own domain, carrying a book, with no stated release cadence. That is a different risk, and a smaller one, but it is still a risk worth naming in the documentation.

**What the pinned edition contains, read from the book itself rather than assumed** (`~zeckma/openrc/lfs/stable/`, 11 chapters, 85 packages in `chapter03/packages.html`):

| Pinned | Version | |
|---|---|---|
| Linux | **6.18.10** | the base book's kernel, not this project's `v6.12` audit pin and not 13.1-systemd's 7.1.8 |
| glibc / GCC / binutils | 2.43 / 15.2.0 / 2.46.0 | |
| OpenRC | **0.63** | built `-D pam=false` |
| systemd | 259.1 | **present**, as the source of `chapter08/udev.html`: "Udev from Systemd-259.1", built `-D logind=false -D vconsole=false -D link-udev-shared=false` |
| grub / kmod / util-linux / openssl / sysklogd | 2.14 / 34.2 / 2.41.3 / 3.6.1 / 2.7.2 | |
| PAM | **absent** | the string does not occur in the package list; there is no `pam` page in chapter 8 |

Three consequences, each of which contradicts something this design or a sibling change asserted: PID 1 is OpenRC's own init, not sysvinit's (OQ1 below); "no systemd" cannot be stated without naming the udev exception (D2); and `pam_limits` is not available as a mechanism anywhere in this system, which is corrected in `honest-scope-and-config-generation` D14.

## Goals / Non-Goals

**Goals**

- A build story that resolves: named editions, named versions, checksums, patch series.
- The OpenRC substitution written down as owned software, with its maintenance cost visible.
- "No systemd" backed by a command that fails the build.
- A package policy that stops at selection, because rewriting a toolchain is a different project.

**Non-Goals**

- Specifying LFS again. The book is upstream; duplicating it here drifts and decides nothing.
- Choosing a distribution or replacing LFS with a binary base. That is a different project's thesis.
- Any rewrite of any package.

## Decisions

### D1 — The base is the published OpenRC edition, pinned by revision

The systemd edition builds systemd into the base system, which contradicts the init choice and the README's own feature list. The OpenRC edition of LFS is published and is the base; the SysVinit edition is not chosen merely because it exists, since it stops at 12.4 while 13.1-systemd is current.

What is owned is not the init layer but the **pinning of it**. The edition lives in a personal directory with no release cadence, so the build records the exact revision of the delta, the book version it applies to, and every patch applied on top. Where the delta and the book diverge, the divergence is a patch series under `patches/`, not an edit to a copy of the book.

The documentation says this in those words. An operator following the main view index will not find the OpenRC book at all, and the honest text is "the OpenRC edition is published here, at this revision, and here is what to do when it lags the book" — not a link to `/lfs/view/stable-openrc/`, and not a claim that no upstream exists.

**The pin, as published by the two editions themselves** (read off their own pages, not off a memory of them):

| | path | version | revision / date | what it patches |
|---|---|---|---|---|
| LFS | `~zeckma/openrc/lfs/13.0/` | `13.0-openrc` | published **March 5th, 2026** — no SVN revision is published for it | LFS 13.0, whose changelog entry `[bdubbs] - LFS-13.0 released` carries the same date |
| BLFS | `~zeckma/openrc/blfs/svn/` | `r13.0-194` | dated **March 23rd, 2026** | nothing versioned — it is the **svn branch**, and its own version page says it is "currently targeting the LFS development book" |

Three things follow and are recorded here rather than in a change that cannot see them.

The LFS edition's directory also offers `stable/` and `development/`. Neither is cited anywhere in this repository: an alias is a path that moves under a pin, which is the failure mode the pin exists to prevent.

BLFS has **no released OpenRC edition at all** — `~zeckma/openrc/blfs/` contains one entry, `svn/`. So the BLFS instructions this project reads assume a base *newer* than the 13.0-openrc pinned above, and every BLFS-derived `base.lock` entry is checked against the pinned base before it is installed. That is not a hypothetical: `userspace-and-ml-stack` 3.1, 3.3 and 3.4 already record Ninja, pkgconf, ccache, chrony and tmux as having **no page in the pinned BLFS rendering** — the gap is the mismatch showing up, in advance.

The SysVinit alternative was checked rather than assumed: `lfs/view/13.0/` and `lfs/view/13.1/` return 404 while `lfs/view/12.4/` returns 200. LFS 13.x publishes no SysVinit edition, so choosing the OpenRC edition is a **version** decision and not only an init decision — the alternative to 13.0-openrc is 12.4-sysvinit, four minor versions and a year of package updates behind, or 13.1 with systemd built into the base.

**When the two books disagree about a base package version, the base book wins**, because `base.lock` is derived from the base book's package page and the build installs what `base.lock` names; a BLFS page describing a different version is not a competing instruction, it is evidence that the BLFS instruction was written against a newer base. The re-verification is owned by the change that derives the BLFS-side entries — `userspace-and-ml-stack` group 3, whose 0.7 route already exists for packages the rendering does not cover — and it is triggered by the only event that can move the pin: a re-run of `derive-base-lock.py` against a different edition directory. What is *not* in place is a check that notices the drift automatically; the trigger is procedural, and saying so is cheaper than a check that cannot be written before a build exists.

### D2 — "No systemd" is a link scan with a named exception, not a badge

The failure mode is silent and common: build systems probe, find `libsystemd`, and enable integration — meson options default to `auto`, autotools grow `--with-systemd`. The result is an OpenRC host whose binaries link `libsystemd.so.1`, with a README badge that remains technically true.

The check is a scan over the finished image for `libsystemd`, `libelogind`, `pam_systemd`, `libnss_systemd`, failing with the binary named. The fix is the package's own build option; an exclusion list would restore exactly the silence the check exists to break.

The claim the check backs has to be worded to what is actually true, because the pinned edition ships systemd source: `chapter08/udev.html` is titled "Udev from Systemd-259.1" and builds udev out of the systemd tarball. What BareCortex does without is systemd **as init and as a service manager**, plus `logind` and `vconsole` (both disabled by the edition's own meson options) — not every line of systemd code. The scan is what keeps that honest rather than pedantic: udev is built `-D link-udev-shared=false`, so if any binary in the finished image resolves `libsystemd.so.1` at runtime, the static-linking assumption has silently broken, and that is precisely the failure the badge would hide.

### D3 — `base.lock` is the source of truth; specs hold only the delta

`base.lock` at the repository root records name, version, URL, checksum, provenance class, and patch series per package, and the build refuses an image containing a package it cannot account for. Specifications then carry only exclusions, additions, and guarantees — which is the only content that survives an upstream release.

The file is named once, in this change, and every other change adds entries to it rather than starting a parallel list; "manifest" in older prose here means this file and nothing else. `build-orchestration` derives its contents; this change owns its format.

Deltas are patch series under the existing `patches/` tree. Vendoring an upstream project into this repository would trade a checksum line for a permanent copy with no provenance, and the LFS recipe would still not see it.

The class list is data, not prose. `class-map.toml` holds one row per provenance class — the fields that class requires, and the change that owns the row — because a check cannot read an enumeration out of a sentence, and a check iterating zero rows passes without earning it. The map is hand-authored and lives outside `base.lock`, whose contents are derived: a hand-authored section inside a derived file is the next regeneration's collateral damage. `gpu-stack` owns the rows of the vocabulary; this change owns the file's path and schema.

### D4 — Language policy is a selection order

The alternative considered was a rewrite cycle for any package not in C or Rust. Two facts settle it:

- **Bootstrap.** `rustc` needs a linker, a libc, and LLVM. LFS's temporary-system chapter exists because of this dependency. A toolchain cannot be rewritten into Rust before a Rust compiler exists, and cannot be rewritten after, because the compiler now depends on what is being rewritten.
- **The accelerator path is C and CUDA and is not rewritable.** The GPU driver and the CUDA userspace blobs are built and tested against glibc. Disturbing libc risks the one component the project cannot replace — while touching none of the code a language policy would reach: kernel, driver, CUDA runtime, CPython, PyTorch.

The policy is therefore: maintained port, then upstream as-is, then do not adopt. A rewrite proposal must name the port it rejected and the maintenance cost it accepts, because the property being spent is upstream.

**The narrowed criterion selects nothing.** "Rewrite only what the system actually uses" was run against the real package set rather than in the abstract: the LFS base (gcc, binutils, glibc, coreutils, bash, util-linux, kmod, e2fsprogs, iproute2, openssl, openssh, pciutils, kbd) is C, and so is the ML stack (CUDA — C++/PTX, cuDNN — C, NCCL — C++, PyTorch — C++/CUDA, OpenBLAS and FFmpeg — C plus asm, OpenMPI — C, CMake — C++, Git, tmux, htop, nvtop, chrony). One non-C package ships in the image: CPython. It cannot be rewritten, because `torch`'s bindings are C extensions against the CPython C-API — rewriting the interpreter cancels the stack the rewrite was for. Tcl, Expect and Perl appear in LFS only to run test suites and do not reach the image. The policy is therefore either vacuous or impossible, which is why it is recorded as a selection order.

The constructive half of the same goal is to **adopt existing ports rather than write them**: `uv` over `pip`/`venv`, `ruff` over `flake8`/`pylint`, `tokenizers`, `polars`. The language changes in the running system, upstream is kept, and no line is rewritten.

**A rewrite is admissible only with evidence, and the decision lives in a file.** An external plan proposed an audit-driven version of the rewrite policy: record which binaries actually execute, rewrite only the proven-used subset, and keep a machine-readable ledger of what was deliberately left alone. The evidence gate is adopted; it is strictly better than choosing by source language, which is the criterion this decision rejected. Three conditions come with it:

- **A ledger, not a memory.** `/srv/manifest/rust-coverage.toml` (or equivalent) carries `[keep]` entries with a one-line rationale and `[rewritten]` entries with the evidence citation. The file is the policy truth; a decision absent from it has not been made. The installed copy is for a human at a console with no network — nothing running on the installed system reads it, the repository copy is authoritative, and an edit made on the target is overwritten by the next build. When the ledger ships it accounts for itself as a `first-party` entry.
- **A maintained port counts as the port.** For coreutils the maintained Rust port is uutils, so the selection order reaches it without a rewrite. Writing an applet by hand is not on the table.
- **Replacement needs parity evidence and a rollback that was tested.** Golden outputs captured from the incumbent on this machine, a gate on the audited invocations, the incumbent moved rather than deleted, and the rollback executed once before the swap. A replacement whose rollback has not run is not reversible.

The gate is also what rejects things. A daemon written to replace syslogd, cron, or an OOM watchdog is a rewrite of a maintained upstream (sysklogd, cron, earlyoom) at the project's own cost, in the components relied on when everything else has failed. "Only the used subset" argues for a smaller surface, not for fewer correctness guarantees, and the selection order answers it before the design does.

### D5 — GPU stack comes from NVIDIA

The datacenter driver installation guide, the CUDA Installation Guide for Linux, and the CUDA Compatibility guide all resolve and are the actual sources. GLFS is a gaming book.

### D6 — The kernel version is the base book's; the config audit names what it was done against

Three kernel numbers are in circulation and the build uses exactly one of them. `base.lock` pins **Linux 6.18.10** (`chapter05/linux-headers.html`, `chapter10/kernel.html`), derived from the pinned edition, and that is what the build compiles. `honest-scope-and-config-generation` audited every symbol in `kernel/.config` against **`torvalds/linux` tag `v6.12`**, on 2026-11-09 — a version nobody builds here. Mainline is ahead of both.

The audit is a record of what was checked, not a version the project uses, and the difference is not cosmetic: a symbol that exists in 6.12 can be renamed, moved under a new parent, or select another symbol by 6.18, and `merge_config.sh` exits 0 whether or not the requested symbol survived — the string match that change added *is* the enforcement. So the honest state, written down where the next reader will find it rather than where it flatters: **as of today `kernel/.config` has no symbol verified against 6.18.10**, and the first build that compiles the kernel is the first time the two meet. When the pin moves, the audit is redone against the tag the pin names, and the audit document says which tag it used in its first line, the way `honest-scope-and-config-generation` already does.

## Risks / Trade-offs

- **The OpenRC layer has no upstream and one maintainer.** Mitigation is the inventory requirement: every service names its runlevel, requirements, and failure mode, so the cost of not maintaining it is visible before it is paid at 3am.
- **The link scan will fail on the first run.** That is the check working, not a regression; the current number of offending packages is unknown and unknowable until a system is built.
- **`base.lock` drifts if the build can succeed without it.** Mitigated by making an unaccounted package a build failure rather than a warning.

## Open Questions

- **OQ1 — CLOSED, and closed wrongly the first time (2026-11-08).** This question was previously recorded as "PID 1 is sysvinit's `init`, OpenRC is called per runlevel". That was an inference from how other OpenRC systems are packaged, and the pinned edition contradicts it: `chapter08/openrc.html` installs OpenRC, states that "OpenRC provides init and other programs of its own", and creates the shorthands itself — `for i in {init,shutdown}; do ln -sv openrc-$i /sbin/$i; done`, followed by a hand-written `/sbin/poweroff`. **PID 1 is `openrc-init`.** There is no sysvinit package in the edition's 85-package list, and no `/etc/inittab` to keep. Everything this design or a spec said about sysvinit is superseded. The consequence for the runlevel map, `sulogin`/`utmp` handling and shutdown path is the edition's business, cited by revision, not ours.
- **OQ2 — What does OpenRC itself need to build? ANSWERED 2026-11-11, from `openrc/openrc` tag `0.63` — the version the pinned edition builds — `meson.build` and `meson_options.txt`.** Every external dependency OpenRC 0.63 declares lives in the root `meson.build`; the 54 subdirectory files only `declare_dependency` over its own sources.
  - **Required, on Linux: `libcap >= 2.33`** (`dependency('libcap', version: '>=2.33')`, unconditional inside `if os == 'linux'`). Satisfied by the base's own order: `base.lock` pins Libcap 2.77, and the edition builds it at §8.27 while OpenRC is §8.84.
  - **Optional with `type: 'feature', value: 'auto'`: `audit` and `libselinux`.** `auto` means meson probes pkg-config and enables silently when present. Neither exists in the pinned base — `base.lock` has no audit or SELinux entry — so both are off, which is why the edition passes no flag for them. The consequence is the interesting part: install audit or SELinux before §8.84 and OpenRC starts linking them with nobody deciding, and the only thing that would notice is the group 5 link scan.
  - **`pam` is a boolean with no default (false), and the edition passes `-D pam=false` anyway** — "disables needing Linux-PAM for the build, since it is not installed yet". With it false, `pam_misc` and `libcrypt` are never looked up, which is the mechanism behind 2.7: there is no PAM anywhere in this system to write a `pam.d` file for.
  - **`dl` and `util` are `find_library(..., required: false)`** — glibc, present. `kvm` is BSD-only.
  - **Correction to the question as asked:** cracklib and ncurses are not dependencies of OpenRC 0.63 and appear nowhere in its build definition, and neither is popt, which older OpenRC needed and which a dependency list copied from a distro spec would still name. The question's own list was the thing that needed checking.
- **OQ3 — Is BLFS used at all, or only a named subset?** "Follow BLFS" is the same class of instruction as "follow the LFS OpenRC edition": a book is not a build. The image should list packages, and BLFS should be cited per package. Compounded by BLFS having no released OpenRC edition — only an SVN snapshot (see the correction above for its pin and its coverage gaps). **Answered 2026-11-08:** a named subset only, derived into `base.lock` by `userspace-and-ml-stack` group 3, one citation per entry, with the uncovered packages listed there rather than assumed available.

**Recorded 2026-11-11, with one exception still standing:** the list exists — `base.lock`, 88 package entries, each naming the book pages that build it — and it is derived by `scripts/build/derive-base-lock.py` from the pinned edition's package page, so nothing hand-maintained sits beside it. What is *not* yet true is "no second list is kept": `docs/INSTALL_GUIDE.md` Phase 2 still carries a hand-written "Required packages" block (Python, OpenSSH, Git, GCC, CMake, Make, Ninja, curl, wget) that no check reads and no build consumes. Task 2.2 is what removes it; until then the second list exists and this line is the record that it is a defect, not a design.
- **OQ4 — How many packages link systemd today?** Unanswerable without a built system; the answer is the first run of the scan in **group 5**, and it is expected to be nonzero. The count is unknown by construction, not by omission, and no document here states a number.
