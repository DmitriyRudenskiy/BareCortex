# Build log

Every phase of the system build, recorded as it ran: what was executed,
where in the pinned book it comes from, what it returned, and what was
frozen afterwards. Written by `scripts/build/build.py`; nothing here is
reconstructed afterwards from memory.

## host-prep — FAILED

2026-11-11T15:18:38 → 2026-11-11T15:18:42, context `host-root`, 5 page(s) of chapter 02.

Book pages: `chapter02/hostreqs.html`, `chapter02/creatingpartition.html`, `chapter02/creatingfilesystem.html`, `chapter02/aboutlfs.html`, `chapter02/mounting.html`

Exit criteria this phase claims to meet:

- The LFS partition is formatted ext4 and mounted at /mnt/lfs.
- Swap is on, and $LFS resolves to the mount point in every later context.

Failures: none

Checkpoint: `none`

## host-prep — FAILED

2026-11-11T15:22:19 → 2026-11-11T15:22:19, context `host-root`, 1 page(s) of chapter 02.

Book pages: `chapter02/mounting.html`

Exit criteria this phase claims to meet:

- The LFS partition is formatted ext4 and mounted at /mnt/lfs.
- Swap is on, and $LFS resolves to the mount point in every later context.

Failures: `block 1 rc 32`

Checkpoint: `none`

## host-prep — FAILED

2026-11-11T15:22:56 → 2026-11-11T15:22:59, context `host-root`, 5 page(s) of chapter 02.

Book pages: `chapter02/hostreqs.html`, `chapter02/creatingpartition.html`, `chapter02/creatingfilesystem.html`, `chapter02/aboutlfs.html`, `chapter02/mounting.html`

Exit criteria this phase claims to meet:

- The LFS partition is formatted ext4 and mounted at /mnt/lfs.
- Swap is on, and $LFS resolves to the mount point in every later context.

Failures: `block 1 rc 32`

Checkpoint: `none`

## host-prep — FAILED

2026-11-11T15:23:56 → 2026-11-11T15:23:59, context `host-root`, 5 page(s) of chapter 02.

Book pages: `chapter02/hostreqs.html`, `chapter02/creatingpartition.html`, `chapter02/creatingfilesystem.html`, `chapter02/aboutlfs.html`, `chapter02/mounting.html`

Exit criteria this phase claims to meet:

- The LFS partition is formatted ext4 and mounted at /mnt/lfs.
- Swap is on, and $LFS resolves to the mount point in every later context.

Failures: `block 1 rc 32`

Checkpoint: `none`

## host-prep — passed

2026-11-11T15:24:14 → 2026-11-11T15:24:17, context `host-root`, 5 page(s) of chapter 02.

Book pages: `chapter02/hostreqs.html`, `chapter02/creatingpartition.html`, `chapter02/creatingfilesystem.html`, `chapter02/aboutlfs.html`, `chapter02/mounting.html`

Exit criteria this phase claims to meet:

- The LFS partition is formatted ext4 and mounted at /mnt/lfs.
- Swap is on, and $LFS resolves to the mount point in every later context.

Failures: none

Checkpoint: `none`

## chroot-layout — FAILED

2026-11-11T15:30:01 → 2026-11-11T15:30:04, context `host-root`, 3 page(s) of chapter 04.

Book pages: `chapter04/creatingminlayout.html`, `chapter04/addinguser.html`, `chapter04/settingenvironment.html`

Exit criteria this phase claims to meet:

- The directory layout exists with $LFS/sources writable by lfs and $LFS/tools owned by lfs.
- User lfs exists and its environment sets LFS, LFS_TGT and MAKEFLAGS as the book's page writes them.

Failures: none

The phase's exit criteria, evaluated after the pages ran:

- `ok` `test "$(stat -c %U /mnt/lfs/tools)" = lfs`
- `FAILED 1` `test "$(stat -c %U /mnt/lfs/sources)" = lfs`
- `ok` `id lfs`
- `FAILED 1` `sudo -u lfs -H bash -c 'source $HOME/.bashrc; test "$LFS" = /mnt/lfs -a -n "$LFS_TGT" -a -n "$MAKEFLAGS"'`

Checkpoint: `after`

## chroot-layout — FAILED

2026-11-11T15:30:55 → 2026-11-11T15:30:58, context `host-root`, 2 page(s) of chapter 04.

Book pages: `chapter04/creatingminlayout.html`, `chapter04/addinguser.html`

Exit criteria this phase claims to meet:

- The directory layout exists with $LFS/sources writable by lfs and $LFS/tools owned by lfs.
- User lfs exists and its environment sets LFS, LFS_TGT and MAKEFLAGS as the book's page writes them.

Failures: `block 1 rc 9`

The phase's exit criteria, evaluated after the pages ran:

- `ok` `test "$(stat -c %U /mnt/lfs/tools)" = lfs`
- `FAILED 1` `sudo test -w /mnt/lfs/sources && sudo -u lfs test -w /mnt/lfs/sources`
- `ok` `id lfs`
- `FAILED 1` `sudo -u lfs -H bash -c 'source $HOME/.bashrc; test "$LFS" = /mnt/lfs -a -n "$LFS_TGT" -a -n "$MAKEFLAGS"'`

Checkpoint: `after`

## chroot-layout — FAILED

2026-11-11T15:31:37 → 2026-11-11T15:31:39, context `host-root`, 2 page(s) of chapter 04.

Book pages: `chapter04/creatingminlayout.html`, `chapter04/addinguser.html`

Exit criteria this phase claims to meet:

- The directory layout exists with $LFS/sources writable by lfs and $LFS/tools owned by lfs.
- User lfs exists and its environment sets LFS, LFS_TGT and MAKEFLAGS as the book's page writes them.

Failures: `block 1 rc 9`

The phase's exit criteria, evaluated after the pages ran:

- `ok` `test "$(stat -c %U /mnt/lfs/tools)" = lfs`
- `ok` `sudo test -w /mnt/lfs/sources && sudo -u lfs test -w /mnt/lfs/sources`
- `ok` `id lfs`
- `FAILED 1` `sudo -u lfs -H bash -c 'source $HOME/.bashrc; test "$LFS" = /mnt/lfs -a -n "$LFS_TGT" -a -n "$MAKEFLAGS"'`

Checkpoint: `after`

## chroot-layout — passed

2026-11-11T15:31:55 → 2026-11-11T15:31:57, context `host-root`, 3 page(s) of chapter 04.

Book pages: `chapter04/creatingminlayout.html`, `chapter04/addinguser.html`, `chapter04/settingenvironment.html`

Exit criteria this phase claims to meet:

- The directory layout exists with $LFS/sources writable by lfs and $LFS/tools owned by lfs.
- User lfs exists and its environment sets LFS, LFS_TGT and MAKEFLAGS as the book's page writes them.

Failures: none

The phase's exit criteria, evaluated after the pages ran:

- `ok` `test "$(stat -c %U /mnt/lfs/tools)" = lfs`
- `ok` `sudo test -w /mnt/lfs/sources && sudo -u lfs test -w /mnt/lfs/sources`
- `ok` `id lfs`
- `ok` `sudo -u lfs -H bash -c 'source $HOME/.bashrc; test "$LFS" = /mnt/lfs -a -n "$LFS_TGT" -a -n "$MAKEFLAGS"'`

Checkpoint: `after`

## cross-toolchain — FAILED

2026-11-11T15:34:44 → 2026-11-11T15:34:45, context `host-lfs`, 1 page(s) of chapter 05.

Book pages: `chapter05/binutils-pass1.html`

Exit criteria this phase claims to meet:

- $LFS/tools holds the cross compiler, assembler and linker, and the target's `as --version` answers.
- Every command block of every page exited 0.

Failures: none

Checkpoint: `after`

## chroot-layout — passed

2026-11-11T15:38:05 → 2026-11-11T15:38:09, context `host-root`, 3 page(s) of chapter 04.

Book pages: `chapter04/creatingminlayout.html`, `chapter04/addinguser.html`, `chapter04/settingenvironment.html`

Exit criteria this phase claims to meet:

- The directory layout exists with $LFS/sources writable by lfs and $LFS/tools owned by lfs.
- User lfs exists and its environment sets LFS, LFS_TGT and MAKEFLAGS as the book's page writes them.

Failures: none

The phase's exit criteria, evaluated after the pages ran:

- `ok` `test "$(stat -c %U /mnt/lfs/tools)" = lfs`
- `ok` `sudo test -w /mnt/lfs/sources && sudo -u lfs test -w /mnt/lfs/sources`
- `ok` `id lfs`
- `ok` `sudo -u lfs -H bash -c 'source $HOME/.bashrc; test "$LFS" = /mnt/lfs -a -n "$LFS_TGT" -a -n "$MAKEFLAGS"'`

Checkpoint: `after`

## host-prep — passed

2026-11-11T15:39:28 → 2026-11-11T15:39:32, context `host-root`, 5 page(s) of chapter 02.

Book pages: `chapter02/hostreqs.html`, `chapter02/creatingpartition.html`, `chapter02/creatingfilesystem.html`, `chapter02/aboutlfs.html`, `chapter02/mounting.html`

Exit criteria this phase claims to meet:

- The LFS partition is formatted ext4 and mounted at /mnt/lfs.
- Swap is on, and $LFS resolves to the mount point in every later context.

Failures: none

The phase's exit criteria, evaluated after the pages ran:

- `ok` `findmnt -no FSTYPE /mnt/lfs | grep -qx ext4`
- `ok` `findmnt -no SOURCE /mnt/lfs | grep -qx /dev/vdc1`
- `ok` `sudo swapon --show=NAME --noheadings | grep -qx /dev/vdc2`

Checkpoint: `none`

## chroot-layout — passed

2026-11-11T15:39:32 → 2026-11-11T15:39:35, context `host-root`, 3 page(s) of chapter 04.

Book pages: `chapter04/creatingminlayout.html`, `chapter04/addinguser.html`, `chapter04/settingenvironment.html`

Exit criteria this phase claims to meet:

- The directory layout exists with $LFS/sources writable by lfs and $LFS/tools owned by lfs.
- User lfs exists and its environment sets LFS, LFS_TGT and MAKEFLAGS as the book's page writes them.

Failures: none

The phase's exit criteria, evaluated after the pages ran:

- `ok` `test "$(stat -c %U /mnt/lfs/tools)" = lfs`
- `ok` `sudo test -w /mnt/lfs/sources && sudo -u lfs test -w /mnt/lfs/sources`
- `ok` `id lfs`
- `ok` `sudo -u lfs -H bash -c 'source $HOME/.bashrc; test "$LFS" = /mnt/lfs -a -n "$LFS_TGT" -a -n "$MAKEFLAGS"'`

Checkpoint: `after`

## cross-toolchain — FAILED

2026-11-11T18:25:48 → 2026-11-11T18:25:50, context `host-lfs`, 0 page(s) of chapter 05.

Book pages: 

Exit criteria this phase claims to meet:

- $LFS/tools holds the cross compiler, assembler and linker, and the target's `as --version` answers.
- Every command block of every page exited 0.

Failures: none

The phase's exit criteria, evaluated after the pages ran:

- `FAILED 1` `test -x /mnt/lfs/tools/bin/x86_64-lfs-linux-gnu-gcc`
- `FAILED 127` `/mnt/lfs/tools/bin/x86_64-lfs-linux-gnu-as --version > /dev/null`
- `FAILED 1` `test -x /mnt/lfs/tools/bin/x86_64-lfs-linux-gnu-ld`

Checkpoint: `after`

## cross-toolchain — FAILED

2026-11-11T18:26:47 → 2026-11-11T18:27:47, context `host-lfs`, 1 page(s) of chapter 05.

Book pages: `chapter05/binutils-pass1.html`

Exit criteria this phase claims to meet:

- $LFS/tools holds the cross compiler, assembler and linker, and the target's `as --version` answers.
- Every command block of every page exited 0.

Failures: none

The phase's exit criteria, evaluated after the pages ran:

- `FAILED 1` `test -x /mnt/lfs/tools/bin/x86_64-lfs-linux-gnu-gcc`
- `ok` `/mnt/lfs/tools/bin/x86_64-lfs-linux-gnu-as --version > /dev/null`
- `ok` `test -x /mnt/lfs/tools/bin/x86_64-lfs-linux-gnu-ld`

Checkpoint: `after`

## cross-toolchain — FAILED

2026-11-11T18:27:59 → 2026-11-11T18:29:07, context `host-lfs`, 2 page(s) of chapter 05.

Book pages: `chapter05/binutils-pass1.html`, `chapter05/gcc-pass1.html`

Exit criteria this phase claims to meet:

- $LFS/tools holds the cross compiler, assembler and linker, and the target's `as --version` answers.
- Every command block of every page exited 0.

Failures: none

The phase's exit criteria, evaluated after the pages ran:

- `FAILED 1` `test -x /mnt/lfs/tools/bin/x86_64-lfs-linux-gnu-gcc`
- `ok` `/mnt/lfs/tools/bin/x86_64-lfs-linux-gnu-as --version > /dev/null`
- `ok` `test -x /mnt/lfs/tools/bin/x86_64-lfs-linux-gnu-ld`

Checkpoint: `after`

## cross-toolchain — passed

2026-11-11T18:41:42 → 2026-11-11T18:50:38, context `host-lfs`, 4 page(s) of chapter 05.

Book pages: `chapter05/binutils-pass1.html`, `chapter05/gcc-pass1.html`, `chapter05/linux-headers.html`, `chapter05/glibc.html`

Exit criteria this phase claims to meet:

- $LFS/tools holds the cross compiler, assembler and linker, and the target's `as --version` answers.
- Every command block of every page exited 0.

Failures: none

The phase's exit criteria, evaluated after the pages ran:

- `ok` `test -x /mnt/lfs/tools/bin/x86_64-lfs-linux-gnu-gcc`
- `ok` `/mnt/lfs/tools/bin/x86_64-lfs-linux-gnu-as --version > /dev/null`
- `ok` `test -x /mnt/lfs/tools/bin/x86_64-lfs-linux-gnu-ld`

Checkpoint: `after`

## temp-tools — FAILED

2026-11-11T18:51:29 → 2026-11-11T18:51:55, context `host-lfs`, 2 page(s) of chapter 06.

Book pages: `chapter06/m4.html`, `chapter06/ncurses.html`

Exit criteria this phase claims to meet:

- The temporary utilities are installed under $LFS/tools.
- Every command block of every page exited 0.

Failures: none

The phase's exit criteria, evaluated after the pages ran:

- `FAILED 127` `/mnt/lfs/tools/bin/gcc --version > /dev/null`
- `FAILED 127` `/mnt/lfs/tools/bin/ld --version > /dev/null`
- `FAILED 1` `test -x /mnt/lfs/tools/bin/bash`

Checkpoint: `after`

## sources — FAILED

2026-11-11T18:54:23 → 2026-11-11T18:54:59, context `host-root`, 1 page(s) of chapter 03.

Book pages: `chapter03/packages.html`

Exit criteria this phase claims to meet:

- Every URL in base.lock is present in $LFS/sources with the MD5 the book published — the book's checksum, not one computed here.
- Nothing was fetched that base.lock does not name.

Failures: `checksum mismatch ncurses-6.6.tar.gz`

The phase's exit criteria, evaluated after the pages ran:

- `FAILED 1` `93 files, MD5 as published by the book`

Checkpoint: `none`

## sources — passed

2026-11-11T18:57:17 → 2026-11-11T18:59:15, context `host-root`, 1 page(s) of chapter 03.

Book pages: `chapter03/packages.html`

Exit criteria this phase claims to meet:

- Every URL in base.lock is present in $LFS/sources with the MD5 the book published — the book's checksum, not one computed here.
- Nothing was fetched that base.lock does not name.

Failures: none

The phase's exit criteria, evaluated after the pages ran:

- `ok` `93 files, MD5 as published by the book`

Checkpoint: `none`

## temp-tools — FAILED

2026-11-11T18:59:26 → 2026-11-11T19:00:30, context `host-lfs`, 2 page(s) of chapter 06.

Book pages: `chapter06/m4.html`, `chapter06/ncurses.html`

Exit criteria this phase claims to meet:

- The temporary utilities are installed under $LFS/tools.
- Every command block of every page exited 0.

Failures: none

The phase's exit criteria, evaluated after the pages ran:

- `FAILED 127` `/mnt/lfs/tools/bin/gcc --version > /dev/null`
- `FAILED 127` `/mnt/lfs/tools/bin/ld --version > /dev/null`
- `FAILED 1` `test -x /mnt/lfs/tools/bin/bash`

Checkpoint: `after`
