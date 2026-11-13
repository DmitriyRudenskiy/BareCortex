# Tasks: amd64-only-userspace

Scope note: nothing here needs the **target** machine and nothing needs the GPU.
Group 1 is `(build)` and already ran; groups 2–4 are `(build)` — a Python 3 the
build host has, plus `readelf` for nothing at all, since the check reads ELF
headers itself. The one task that would need a finished image says so and is left
open, because chapters 8–10 are still building.

## 0. Inputs and host classes

- [x] 0.1 Name the host for every task below: `(build)`, the x86_64 physical host
      at `192.168.0.86` (`docs/HANDOFF-VM.md`). The LFS guest is not used — it
      reboots between phases and its sshd is not always listening. Both the probe
      and the future check run unprivileged, so neither competes with a build in
      flight
- [x] 0.2 Pin the input the numbers are about: `stage3-amd64-nomultilib-openrc-20261108T170107Z.tar.xz`,
      sha256 `433f0f43db2f3db9515339bca0f2b340650ad87d85ab9d7ec34155b14bcc0228`,
      published == computed at measurement time, from
      `distfiles.gentoo.org/releases/amd64/autobuilds/20261108T170107Z/`

## 1. Measure it before asserting anything about it

- [x] 1.1 Write `artifacts/handoff-vm/gentoo-probe/probe.sh` and `owns.py`: verify
      the tarball against its published sha256, unpack without `/dev` (device
      nodes need root, and the init system makes them), census every ELF header,
      resolve every loadable file with the stage3's own loader, and stat the paths
      `/var/db/pkg/*/CONTENTS` names. Run: `./probe.sh`
- [x] 1.2 Fix the library path, which was the whole measurement: a binary's
      absolute `RUNPATH` resolves against the host root when a foreign loader
      runs, so `--library-path lib64:usr/lib64` reported **127 false
      `cannot open shared object` failures**. Passing every directory holding a
      `.so` is what makes the graph honest. A probe that reports failures it did
      not investigate is worse than no probe, because its numbers get quoted
- [x] 1.3 Run it and keep the output. Result: 1 038 605 377 B unpacked, 61 480
      paths, 292 packages; **1 487 ELF files, all `64-bit EM_X86_64`, zero 32-bit,
      no `lib32`/`libx32` directory**; **1 461 loadable files, 1 461 at exit 0,
      zero unresolved**. Evidence: `evidence/probe.log`, `evidence/elf-census.json`,
      `evidence/ldd.json`, `evidence/vdb.txt`
- [x] 1.4 Price what a distro's OpenRC brings along, from the stage3's own package
      record: gcc 337 408 853 B (32.5% of the image), glibc 52 849 340 B against
      musl's 670 312 B, portage 27 406 462 B + python 59 471 011 B, `/usr/share`
      354 996 KiB of locale/sgml/doc/man. OpenRC itself is 2 072 578 B — the same
      size as Alpine's
- [x] 1.5 Find the two rows that are already this project's decisions, and record
      them as conflicts rather than curiosities: **65 binaries resolve
      `libpam.so.0`**, including `usr/libexec/rc/bin/openrc-user` (the binary
      Alpine splits out as a no-PAM package) and `usr/bin/supervise-daemon`, while
      the pinned edition builds OpenRC `-D pam=false`; and **12 binaries resolve
      `libsystemd-shared-260.so`** (`udevadm`, `systemd-hwdb`, `systemd-tmpfiles`,
      the `usr/lib/udev/*.id` helpers), which `scripts/build/check-no-systemd.py`
      would fail the build on today
- [x] 1.6 Establish the current state of the rule instead of assuming it: grep of
      `README.md`, `docs/PHILOSOPHY.md`, `docs/INIT-LAYER.md`, `openspec/project.md`
      and `base.lock` finds no `x86_64`/`amd64`/`64-bit`/`multilib`, and no `-m32`,
      `--enable-multi-arch` or `lib32` anywhere in the build scripts. The rule is
      true by construction, unstated, and unenforced

## 2. Make the rule a command

- [x] 2.1 Write `scripts/build/check-amd64-only.py --root <image>`: walk the root,
      read `EI_CLASS` (byte 4) and `e_machine` (bytes 18–19) from every ELF
      header, fail naming the path on `ELFCLASS32`, on a machine other than
      x86_64, or on a `lib32`/`libx32` directory; fail without `--root`; print the
      ELF count it read. `--explain` prints the rules, per
      `scripts/build/check-no-systemd.py`, whose CLI and refusal-to-scan-nothing
      this copies (design D1, D2). Written; 20 bytes per file, no `readelf`, the
      whole 61 480-path stage3 in 3 s
- [x] 2.2 Write `test_check_amd64_only.py` in the style of
      `test_check_no_systemd.py`: craft headers rather than needing an image —
      `ELFCLASS32`/`EM_386` fails, `ELFCLASS64`/`EM_X86_64` passes, a `lib32`
      directory fails, a non-ELF file is ignored, a file with ELF magic that
      cannot be read is a failure and not a skip, and `ET_REL` is classified like
      anything else. Run: `python3 test_check_amd64_only.py` — passes. The count
      is files read, not files cleared, so a dirty root prints its offenders in
      `elf=` and still exits 1
- [x] 2.3 Prove the check on the one artifact already on disk: run it against the
      unpacked stage3 and expect exit 0 with `elf=1487`, the same count the probe's
      census printed. Two independent programs agreeing on 1 487 is the check's
      real test; the crafted headers only prove the classification. **The task
      caught the bug it was written for:** the first run printed `elf=1908`,
      because `Path.is_file()` follows symlinks and 421 of them point at ELF
      files. A link is not a second object — the file at the other end is scanned
      where it lives, and an absolute link inside an image root resolves inside
      that root once booted — so links are not counted, and the two programs now
      agree. Run on `~/.cache/bc-gentoo/root` (0.1's host):
      `python3 check-amd64-only.py --root ~/.cache/bc-gentoo/root` →
      `amd64-only: root=/home/user/.cache/bc-gentoo/root elf=1487 findings=0`,
      exit 0
- [ ] 2.4 Run it against the assembled image once chapters 8–10 finish `(build)`,
      and record the count in `build-evidence/`. Until then the claim is that the
      check works, not that the image passes

## 3. Say it where it can be acted on

- [x] 3.1 `docs/GENTOO-AS-BASE.md`: the input by checksum, the census, the loader's
      graph, the Alpine/Gentoo table, the three conflicts with decisions already
      made, and a closing section on what the measurement does not say. Every
      figure comes from group 1
- [x] 3.2 `artifacts/handoff-vm/gentoo-probe/README.md`: how to rerun, why the
      library path is the trick, what each evidence file settles, and that a moved
      autobuild checksum means rerun rather than edit
- [x] 3.3 State the rule once in `README.md` next to the systemd row, naming the
      command the way that row names `check-no-systemd.py`, and add the same
      sentence to `docs/PHILOSOPHY.md`. Blocked on 2.1: a claim whose command does
      not exist yet is the thing this repository exists to make impossible — done:
      a `64-bit only, no 32-bit code` row in the README philosophy table, and a
      section of the same name in `docs/PHILOSOPHY.md` between the systemd and
      Wayland sections. Neither says "fails the build", because the check is not
      yet wired into a phase; it is a command you run, and the text says so
- [ ] 3.4 When the Gentoo autobuild moves, rerun `probe.sh` and let it rewrite the
      numbers. **Do not adjust `docs/GENTOO-AS-BASE.md` by hand**

## 4. Hand off what other changes own

- [x] 4.1 Tell `gpu-stack` that the NVIDIA driver is this rule's first real test:
      whether the driver installs any 32-bit code is **not measured here**, and
      `check-amd64-only.py` will name it if it does, so the remedy is the
      installer's own option — not an exception in the check. Recorded as
      `openspec/changes/gpu-stack/tasks.md` 1.5, next to the task that reads the
      installer's options anyway. If a capability turns out to exist only as
      32-bit code, that reopens this change's requirement in `openspec/`
- [ ] 4.2 Tell `image-minimisation` the two numbers it is arguing against if a
      distro seed is ever on the table: 337 408 853 B of compiler and 354 996 KiB
      of `/usr/share` in a 1 038 605 377 B base, and that `/var/db/pkg/*/CONTENTS`
      is a second ownership record to compare against Alpine's
      `/lib/apk/db/installed` (both are already read by a committed probe).
      **Not done, on purpose:** that change is untracked and being written by
      another agent in this worktree right now, and editing under it would be a
      merge nobody asked for. The numbers are in `docs/GENTOO-AS-BASE.md` §4 and
      above; the hand-off is one edit to that change's proposal once it is
      committed
- [x] 4.3 Confirm with `base-system-and-init` that this change touches nothing it
      owns: base stays LFS 13.0-openrc, `base.lock` gains no entries,
      `docs/INIT-LAYER.md` is not edited, and the new check is a second scan
      beside `check-no-systemd.py`, not a change to it. Verified, not asserted:
      `git status --porcelain base.lock docs/INIT-LAYER.md
      scripts/build/check-no-systemd.py test_check_no_systemd.py` is empty. The
      working tree's `phases.toml` modification is the concurrent chapters 8–10
      build work, not this change

## 5. What would make this change worth archiving

- [ ] 5.1 `check-amd64-only.py` and its test committed, passing on the stage3
      (2.3) and on the first assembled image (2.4), with the count recorded
- [x] 5.2 `README.md` and `docs/PHILOSOPHY.md` carrying the claim with its command,
      so that "64-bit only" is a checkable property of the image rather than an
      accident of the toolchain's name
