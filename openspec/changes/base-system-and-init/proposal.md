# Proposal: base-system-and-init

## Why

`docs/INSTALL_GUIDE.md` tells the reader to build the base system from the "LFS OpenRC Edition" and the server environment from the "BLFS OpenRC Edition". Neither exists: `lfs/view/stable-openrc/` and `blfs/view/stable-openrc/` return 404. Phase 3 cites GLFS for "NVIDIA Drivers & CUDA", and GLFS is *Gaming* Linux From Scratch — Steam, Wine, and their dependencies. So the project's build story is three links, two of which lead nowhere and one of which leads to the wrong subject.

The consequence is not a broken hyperlink. `linuxfromscratch.org` publishes an OpenRC edition of LFS — at `~zeckma/openrc/lfs/`, titled `13.0-openrc`, outside the main view index, which is why probing the index for an OpenRC edition reads as absence. Two facts verified there change what this project has to decide. LFS 13.x publishes **no SysVinit edition** (`13.0/` and `13.1/` return 404; only `-systemd` exists), so choosing SysVinit means freezing at 12.4, two majors behind. And BLFS has **no released OpenRC edition** — `~zeckma/openrc/blfs/` holds an SVN snapshot and nothing else. The init layer is therefore not software this project owns with no upstream; it is a book published in a personal directory with no release cadence, and the server-stack half of the story has no release at all. That is a smaller liability than first recorded, and it still has to be pinned by revision rather than followed by link.

Meanwhile the README's "No systemd" badge is an assertion with no check behind it. BLFS packages probe for `libsystemd` and enable integration by default, so an OpenRC system can ship binaries linked against `libsystemd.so.0` while the badge stays true.

## What Changes

- The base book becomes **named and pinned**: the LFS **OpenRC edition** (`~zeckma/openrc/lfs/`), recorded with book version, the revision the delta was taken at, package versions, source URLs, checksums, and applied patch series in `base.lock`, the file the build reads. BLFS is cited per package, never as "follow BLFS", and its snapshot status is recorded where it applies.
- Documentation stops restating LFS. Specs carry **only the delta** — what is excluded, what is added, what is guaranteed on top — because a copy of the book drifts on the day the book is updated and contains no decision of ours.
- The **OpenRC substitution layer is documented against its upstream**: which init artifacts are not shipped, what replaces each, which upstream documents the replacement, and where none does. PID 1 is named — `openrc-init`, symlinked to `/sbin/init` by the pinned edition itself; the edition ships no SysVinit package and no `/etc/inittab`.
- **"No systemd" becomes a build-time check**, not a badge: a link scan over the finished image for `libsystemd`, `libelogind`, `pam_systemd`, `libnss_systemd`, failing the build with the offending package named.
- Package deltas take the form of a **patch series plus a `base.lock` entry**. No upstream project is vendored into this repository.
- A **language and dependency policy** replaces the "rewrite anything not in C or Rust" idea: a selection order (maintained port, then upstream as-is, then do not adopt), with the accelerator path — kernel, GPU driver, CUDA runtime, CPython, PyTorch — explicitly outside the policy, because those are C and CUDA by construction and are what the project exists to run.
- GPU stack installation points at NVIDIA's own documentation instead of GLFS.

## Capabilities

### New Capabilities

- `base-system`: what is inherited from LFS, how the delta is recorded, and how every package in the image is traceable to a `base.lock` entry.
- `init-system`: OpenRC as the init system, what replaces each LFS init artifact, and how the absence of systemd is verified.

## Impact

- `docs/INSTALL_GUIDE.md` — Phases 1–3 rewritten around the editions that exist and NVIDIA's documentation.
- `README.md` — the "No systemd" claim gains a verification command; the "LFS + OpenRC" claim gains the citation and the pin for the OpenRC edition.
- `base.lock` at the repository root — the one package truth, **derived from the pinned book by `build-orchestration` task 2.1** and extended by other changes' entries. This change defines its format and consumes it; it does not maintain a second list.
- New build step: link scan for systemd symbols over the image.
- Task-level dependencies, deliberately not change-level (a change-level "depends on `build-orchestration`" beside that change's "depends on this one" is a cycle, and cycles hide which task unblocks which):
  - `3.1` (`base.lock` format) blocks `build-orchestration 5.1` (fetch and verify against it)
  - `build-orchestration 3.1` (phase table exists) blocks `3.3` and `3.4` ("fail the build" needs a build to fail)
  - `build-orchestration` group 3 blocks `8.3` and `8.4` ("demonstrated on a build tree" needs a build tree)
  - This change says what is true of a built system; it does not sequence the build.
- Depends on nothing in `honest-scope-and-config-generation`, but shares its verification discipline: an assertion with no command behind it is not a requirement.
