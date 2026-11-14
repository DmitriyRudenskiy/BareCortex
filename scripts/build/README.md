> derived by `scripts/build/build-rootfs.sh derive-readme` from the build files' own
> documentation regions, `phases.toml` and the checks' docstrings — do not edit
> this file: edit the build file it documents, then re-run.

# The build tree

One directory, one layout: the files here mirror the top level of
[gbionescu/build-rootfs](https://github.com/gbionescu/build-rootfs) at
`c9fa26adeb66e247afbac33510c9479cf4b89f4a` — the build script, the VM manager,
the tests, and this file. The alignment is checked, not assumed:
`tests/test-all.sh layout` fails on any file that wears a name the reference
does not use, and any file this README does not name.

The build runs the pinned LFS 13.0-openrc edition, page by page, against a
disposable VM, and keeps receipts. The per-page evidence of an actual run is in
`docs/BUILD-LOG.md` and `build-evidence/<phase>/`; this file is the map, not
the record.

## build-rootfs.sh

The driver. Python behind a shell header (`sh` execs it, python reads the exec
line as a string), which is why it can be both `sh build-rootfs.sh run temp-tools`
and an importable module — the trust checks import it as `build`.

```
sh scripts/build/build-rootfs.sh pages <phase>              # what would run, in book order
sh scripts/build/build-rootfs.sh run <phase> [--page P]     # run it, then its criteria
sh scripts/build/build-rootfs.sh fetch                      # download and verify base.lock's sources
sh scripts/build/build-rootfs.sh resume                     # the next phase that has not passed
sh scripts/build/build-rootfs.sh status                     # every phase's record
sh scripts/build/build-rootfs.sh guest <cmd>                # run a command in the build guest
sh scripts/build/build-rootfs.sh derive-base-lock <url>     # re-derive base.lock from an edition (stdout)
sh scripts/build/build-rootfs.sh derive-test-expectations   # re-derive test-expectations.toml in place
sh scripts/build/build-rootfs.sh derive-readme              # re-derive scripts/build/README.md from the build files
```

Every command executed comes from a page of the pinned edition — the book
reader is inlined into this file, which is why this file and not a package holds
it — except where `build-overrides.toml` says otherwise, and each such exception
is written into the log with the reason the override gives. The device
placeholders the book leaves as `<xxx>` are filled from one place, `devices` in
that file, so the machine this build runs on is a fact in one file and not in
eight pages of shell.

### The phases

| phase | chapter | pages | checkpoint | what passing means |
|---|---|---|---|---|
| `host-prep` | 02 | 5 | — | The LFS partition is formatted ext4 and mounted at /mnt/lfs.; Swap is on, and $LFS resolves to the mount point in every later context. |
| `sources` | 03 | 2 | — | Every URL in base.lock is present in $LFS/sources with the MD5 the book published — the book's checksum, not one computed here.; Nothing was fetched that base.lock does not name. |
| `chroot-layout` | 04 | 3 | after | The directory layout exists with $LFS/sources writable by lfs and $LFS/tools owned by lfs.; User lfs exists and its environment sets LFS, LFS_TGT and MAKEFLAGS as the book's page writes them. |
| `cross-toolchain` | 05 | 5 | after | $LFS/tools holds the cross compiler, assembler and linker, and the target's `as --version` answers.; The cross C++ compiler links a program — chapter 6's GCC pass 2 needs it to build libcody.; Every command block of every page exited 0. |
| `temp-tools` | 06 | 17 | after | The temporary utilities are installed under $LFS/usr: chapter 6's pages configure --prefix=/usr and install with DESTDIR=$LFS, so not under $LFS/tools.; Every command block of every page exited 0. |
| `enter-chroot` | 07 | 3 | after | Ownership of the $LFS directories is root:root, not lfs.; proc, sysfs and devpts are mounted inside $LFS and /dev is bind-mounted.; The chapter 7 `env -i` invocation runs a command inside $LFS seeing only HOME, TERM, PS1, PATH, MAKEFLAGS, TESTSUITEFLAGS. |
| `temp-system` | 07 | 8 | after | The chroot's own /etc exists: mtab, hosts, passwd, group, and the directory tree the pages create.; Gettext, Bison, Perl, Python, Texinfo and Util-linux are installed in the temporary system.; Every command block of every page exited 0. |
| `final-system` | 08 | 85 | after | Every package page's command blocks exited 0, and where a page states a test is known to fail, that is the only failure recorded.; The debugging-symbols, stripping and cleanup pages ran. |
| `configuration` | 09 | 7 | after | The device symlinks, network configuration, locale, inputrc and shells files exist with the contents the pages give them.; OpenRC's default runlevel is set. |
| `bootable` | 10 | 3 | after | fstab addresses the root filesystem by UUID, the kernel the book pins is built and installed, and GRUB is installed to the LFS disk.; The kernel on the disk is the version base.lock names. |
| `first-boot` | 11 | 2 | — | The LFS disk boots to a login prompt with the host's disk detached.; Login succeeds, `uname -r` answers the pinned kernel version, and PID 1 is the init the OpenRC edition installs, named by the command that identifies it. |

A page is a page of the pinned edition, run as its command blocks, in the
book's order, with nothing added. `pages <phase>` prints them; the list comes
from the edition's own contents page, so a page cannot be left out by editing a
list here (`check_no_page_is_dropped_from_the_index` is what keeps that true).

`run` takes the build root lock (`build-evidence/.build-lock`): a second
invocation is refused, not queued, because two builds would share one `$LFS`,
one qcow2 and one snapshot chain. `resume` reads the records, rolls the disk
back to the checkpoint the previous phase left, and runs the phase that has not
passed — a failed phase keeps its name, so a resume repeats it rather than
skipping past it.

### What the build refuses to do

It will not run a phase whose pages name a source `base.lock` does not carry, or
whose sources are not on the disk — the phase stops before its first command and
names the file and the command that puts it there. It will not skip a test suite
the book calls utmost important, even through `build-overrides.toml`. It will
not pretend a page ran: every block's exit code is in `build-evidence/<phase>/`,
and a phase whose record says `passed` has a record for every page it ran.

## run-qemu.sh

The disposable build host: a KVM guest whose entire disk is a qcow2 file,
managed entirely inside `$HOME/barecortex-vm` — deleting that directory is the
uninstall. `build-rootfs.sh` drives it as `~/barecortex-vm/vm.sh` (the installed
name; this file is the source), which is why a page's commands, the snapshots
and the rollbacks all route through it:

```
sh scripts/build/run-qemu.sh seed         # build the cloud-init seed (new instance-id each run)
sh scripts/build/run-qemu.sh start        # start the guest, forwarding 127.0.0.1:2222 -> :22
sh scripts/build/run-qemu.sh wait         # wait until ssh answers
sh scripts/build/run-qemu.sh sh <cmd>     # run a command in the guest
sh scripts/build/run-qemu.sh stop         # kill the guest, waiting for the disk lock to free
sh scripts/build/run-qemu.sh grow 64G     # stop, enlarge the qcow2, grow the guest filesystem
sh scripts/build/run-qemu.sh disk 60G     # create the LFS disk (/dev/vdb in the guest)
sh scripts/build/run-qemu.sh snapshot t   # freeze the LFS disk as checkpoint t
sh scripts/build/run-qemu.sh rollback t   # discard the current LFS disk, resume from checkpoint t
sh scripts/build/run-qemu.sh prune        # delete checkpoints no image is built on (--do to do it)
sh scripts/build/run-qemu.sh boot-lfs     # boot the LFS disk alone — the first-boot test
sh scripts/build/run-qemu.sh console      # tail the boot console
```

It expects QEMU extracted under `~/.local/qemu` (from Ubuntu's own .debs — the
host is not installed on); without one it says so and stops. The guest has no
GPU: passthrough needs root, so `(gpu)` work stays on the physical host and
`(build)` work happens here.

## tests/test-all.sh

The offline checks — no guest needed, one result line per check:

```
sh scripts/build/tests/test-all.sh                    # everything that runs offline
sh scripts/build/tests/test-all.sh trust               # the driver's trust checks
sh scripts/build/tests/test-all.sh layout              # the build-rootfs file-layout checks
sh scripts/build/tests/test-all.sh base-lock [--lock L --map M [--coverage]]
sh scripts/build/tests/test-all.sh services [--inventory I]
sh scripts/build/tests/test-all.sh no-systemd --root <image root>
sh scripts/build/tests/test-all.sh cycle-plan [--file F] [--explain]
```

### The checks

`trust` — the driver's own guarantees, run against the real `base.lock`,
`phases.toml` and the pinned pages:
- `check_mismatches` — A wrong, absent or undownloaded checksum names the file.
- `check_lock_files` — `base.lock` still carries 89 packages and 5 patches.
- `check_sources_named` — A page's commands are scanned for file names, not prose.
- `check_record_identity_and_atomicity` — A record says what it was built against, and a crash does not eat it.
- `check_chroot_environment_is_the_books` — The chroot gets the book's six variables and nothing else.
- `check_build_root_lock` — One build per build root, and a dead owner is named as stale.
- `check_test_expectations_are_current` — `test-expectations.toml` says what the pinned pages say today.
- `check_core_suite_cannot_be_skipped` — Skipping a core suite through an override is refused, not remembered.
- `check_no_page_is_dropped_from_the_index` — Every page the edition's own contents lists is a page the build knows about.
- `check_sources_needed_for_every_page` — Every page of every phase can be asked what it needs, including pages that need nothing.
- `check_resume_starts_at_the_failed_phase` — A resume runs the phase that failed, not the one after it.
- `check_guard_on_the_real_pages` — The guard runs against the pinned edition, and says what it finds.
- `check_a_predicted_test_failure_does_not_stop_the_page` — A predicted failure forgives the suite and nothing else.

`layout` — this tree keeps the reference's shape:
- `check_build_tree_file_set` — Every file in the build tree carries a name the reference has.
- `check_readme_is_the_scripts` — The README is what the build files' documentation blocks say today, byte for byte.

`base-lock` — Is `base.lock` an account for the base system: every entry carries name, version, URL, checksum, provenance class and its book pages, and every source-built package the edition lists is present. `services` — Every OpenRC service the image ships is in `openrc/services.toml` with its runlevel, its requirements and its failure semantics. `no-systemd` — Nothing in the image resolves a systemd library: the ELF walk reads `DT_NEEDED` with `readelf`, and a string-table mention is not a runtime resolution.

## tests/test-arch.sh

The 64-bit-only scan: every ELF file under an image root is classified from
`EI_CLASS` and `e_machine`, every directory is checked for the name `lib32` or
`libx32`, and nothing is excluded. It is named for the reference's
`tests/test-arch.sh` and reads like it:

```
sh scripts/build/tests/test-arch.sh --root <image root>   # fail on any non-x86-64 ELF
sh scripts/build/tests/test-arch.sh --explain             # what the scan does, in words
```

## README.md

This file. It is the build-rootfs slot for the tree's documentation: the five
`##` sections above are the five files the tree holds, and
`check_readme_is_the_scripts` keeps the file and the build files equal. The
whole-build procedure — the hardware, the VM, the GPU stack that follows this
rootfs — lives in `docs/` (`PREBUILD.md`, `HARDWARE_REQUIREMENTS.md`,
`HANDOFF-VM.md`, `GPU-STACK.md`), and the evidence of what has actually been
built so far is in `docs/BUILD-LOG.md`.
