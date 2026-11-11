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

## temp-tools — FAILED

2026-11-11T19:10:22 → 2026-11-11T19:11:27, context `host-lfs`, 2 page(s) of chapter 06.

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

## temp-tools — FAILED

2026-11-11T19:14:16 → 2026-11-11T19:15:10, context `host-lfs`, 1 page(s) of chapter 06.

Book pages: `chapter06/ncurses.html`

Exit criteria this phase claims to meet:

- The temporary utilities are installed under $LFS/tools.
- Every command block of every page exited 0.

Failures: none

The phase's exit criteria, evaluated after the pages ran:

- `FAILED 127` `/mnt/lfs/tools/bin/gcc --version > /dev/null`
- `FAILED 127` `/mnt/lfs/tools/bin/ld --version > /dev/null`
- `FAILED 1` `test -x /mnt/lfs/tools/bin/bash`

Checkpoint: `after`

## temp-tools — FAILED

2026-11-11T19:15:19 → 2026-11-11T19:16:34, context `host-lfs`, 2 page(s) of chapter 06.

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

## temp-tools — FAILED

2026-11-11T19:19:49 → 2026-11-11T19:19:50, context `host-lfs`, 0 page(s) of chapter 06.

Book pages: 

Exit criteria this phase claims to meet:

- The temporary utilities are installed under $LFS/tools.
- Every command block of every page exited 0.

Failures: none

Checkpoint: `none`

## sources — passed

2026-11-11T19:19:55 → 2026-11-11T19:21:14, context `host-root`, 1 page(s) of chapter 03.

Book pages: `chapter03/packages.html`

Exit criteria this phase claims to meet:

- Every URL in base.lock is present in $LFS/sources with the MD5 the book published — the book's checksum, not one computed here.
- Nothing was fetched that base.lock does not name.

Failures: none

The phase's exit criteria, evaluated after the pages ran:

- `ok` `93 files, MD5 as published by the book`

Checkpoint: `none`

## temp-tools — FAILED

2026-11-11T19:21:14 → 2026-11-11T19:30:05, context `host-lfs`, 17 page(s) of chapter 06.

Book pages: `chapter06/m4.html`, `chapter06/ncurses.html`, `chapter06/bash.html`, `chapter06/coreutils.html`, `chapter06/diffutils.html`, `chapter06/file.html`, `chapter06/findutils.html`, `chapter06/gawk.html`, `chapter06/grep.html`, `chapter06/gzip.html`, `chapter06/make.html`, `chapter06/patch.html`, `chapter06/sed.html`, `chapter06/tar.html`, `chapter06/xz.html`, `chapter06/binutils-pass2.html`, `chapter06/gcc-pass2.html`

Exit criteria this phase claims to meet:

- The temporary utilities are installed under $LFS/tools.
- Every command block of every page exited 0.

Failures: none

The phase's exit criteria, evaluated after the pages ran:

- `FAILED 127` `/mnt/lfs/tools/bin/gcc --version > /dev/null`
- `FAILED 127` `/mnt/lfs/tools/bin/ld --version > /dev/null`
- `FAILED 1` `test -x /mnt/lfs/tools/bin/bash`

Checkpoint: `after`

## cross-toolchain — passed

2026-11-11T19:36:28 → 2026-11-11T19:49:52, context `host-lfs`, 5 page(s) of chapter 05.

Book pages: `chapter05/binutils-pass1.html`, `chapter05/gcc-pass1.html`, `chapter05/linux-headers.html`, `chapter05/glibc.html`, `chapter05/gcc-libstdc++.html`

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

2026-11-11T19:54:28 → 2026-11-11T19:54:29, context `host-lfs`, 0 page(s) of chapter 06.

Book pages: 

Exit criteria this phase claims to meet:

- The temporary utilities are installed under $LFS/tools.
- Every command block of every page exited 0.

Failures: none

Checkpoint: `none`

## sources — passed

2026-11-11T19:55:09 → 2026-11-11T19:56:19, context `host-root`, 1 page(s) of chapter 03.

Book pages: `chapter03/packages.html`

Exit criteria this phase claims to meet:

- Every URL in base.lock is present in $LFS/sources with the MD5 the book published — the book's checksum, not one computed here.
- Nothing was fetched that base.lock does not name.

Failures: none

The phase's exit criteria, evaluated after the pages ran:

- `ok` `93 files, MD5 as published by the book`

Checkpoint: `none`

## temp-tools — FAILED

2026-11-11T19:56:49 → 2026-11-11T20:12:12, context `host-lfs`, 17 page(s) of chapter 06.

Book pages: `chapter06/m4.html`, `chapter06/ncurses.html`, `chapter06/bash.html`, `chapter06/coreutils.html`, `chapter06/diffutils.html`, `chapter06/file.html`, `chapter06/findutils.html`, `chapter06/gawk.html`, `chapter06/grep.html`, `chapter06/gzip.html`, `chapter06/make.html`, `chapter06/patch.html`, `chapter06/sed.html`, `chapter06/tar.html`, `chapter06/xz.html`, `chapter06/binutils-pass2.html`, `chapter06/gcc-pass2.html`

Exit criteria this phase claims to meet:

- The temporary utilities are installed under $LFS/tools.
- Every command block of every page exited 0.

Failures: none

The phase's exit criteria, evaluated after the pages ran:

- `FAILED 127` `/mnt/lfs/tools/bin/gcc --version > /dev/null`
- `FAILED 127` `/mnt/lfs/tools/bin/ld --version > /dev/null`
- `FAILED 1` `test -x /mnt/lfs/tools/bin/bash`

Checkpoint: `after`

## temp-tools — FAILED

2026-11-11T20:16:00 → 2026-11-11T20:17:14, context `host-lfs`, 2 page(s) of chapter 06.

Book pages: `chapter06/m4.html`, `chapter06/ncurses.html`

Exit criteria this phase claims to meet:

- The temporary utilities are installed under $LFS/usr: chapter 6's pages configure --prefix=/usr and install with DESTDIR=$LFS, so not under $LFS/tools.
- Every command block of every page exited 0.

Failures: none

The phase's exit criteria, evaluated after the pages ran:

- `ok` `/mnt/lfs/usr/bin/gcc --version > /dev/null`
- `FAILED 139` `LD_LIBRARY_PATH=/mnt/lfs/usr/lib /mnt/lfs/usr/bin/ld --version > /dev/null`
- `ok` `test -x /mnt/lfs/usr/bin/bash`

Checkpoint: `after`

## sources — passed

2026-11-11T20:20:16 → 2026-11-11T20:21:09, context `host-root`, 1 page(s) of chapter 03.

Book pages: `chapter03/packages.html`

Exit criteria this phase claims to meet:

- Every URL in base.lock is present in $LFS/sources with the MD5 the book published — the book's checksum, not one computed here.
- Nothing was fetched that base.lock does not name.

Failures: none

The phase's exit criteria, evaluated after the pages ran:

- `ok` `93 files, MD5 as published by the book`

Checkpoint: `none`

## temp-tools — passed

2026-11-11T20:21:09 → 2026-11-11T20:36:17, context `host-lfs`, 17 page(s) of chapter 06.

Book pages: `chapter06/m4.html`, `chapter06/ncurses.html`, `chapter06/bash.html`, `chapter06/coreutils.html`, `chapter06/diffutils.html`, `chapter06/file.html`, `chapter06/findutils.html`, `chapter06/gawk.html`, `chapter06/grep.html`, `chapter06/gzip.html`, `chapter06/make.html`, `chapter06/patch.html`, `chapter06/sed.html`, `chapter06/tar.html`, `chapter06/xz.html`, `chapter06/binutils-pass2.html`, `chapter06/gcc-pass2.html`

Exit criteria this phase claims to meet:

- The temporary utilities are installed under $LFS/usr: chapter 6's pages configure --prefix=/usr and install with DESTDIR=$LFS, so not under $LFS/tools.
- Every command block of every page exited 0.

Failures: none

The phase's exit criteria, evaluated after the pages ran:

- `ok` `/mnt/lfs/usr/bin/gcc --version > /dev/null`
- `ok` `/mnt/lfs/usr/lib/ld-linux-x86-64.so.2 --library-path /mnt/lfs/usr/lib /mnt/lfs/usr/bin/ld --version > /dev/null`
- `ok` `test -x /mnt/lfs/usr/bin/bash`

Checkpoint: `after`

## enter-chroot — FAILED

2026-11-11T20:43:35 → 2026-11-11T20:43:39, context `host-root`, 5 page(s) of chapter 07.

Book pages: `chapter07/changingowner.html`, `chapter07/kernfs.html`, `chapter07/chroot.html`, `chapter07/creatingdirs.html`, `chapter07/createfiles.html`

Exit criteria this phase claims to meet:

- Ownership of the $LFS directories is root:root, not lfs.
- proc, sysfs and devpts are mounted inside $LFS and /dev is bind-mounted.
- The chapter 7 `env -i` invocation runs a command inside $LFS seeing only HOME, TERM, PS1, PATH, MAKEFLAGS, TESTSUITEFLAGS.

Failures: `block 1 rc 1`

The phase's exit criteria, evaluated after the pages ran:

- `ok` `findmnt -t proc -o SOURCE /mnt/lfs/proc`
- `ok` `findmnt -t sysfs -o SOURCE /mnt/lfs/sys`
- `ok` `findmnt -t devpts -o SOURCE /mnt/lfs/dev/pts`
- `ok` `test "$(stat -c %U /mnt/lfs/usr)" = root`

Checkpoint: `after`

## enter-chroot — passed

2026-11-11T20:47:08 → 2026-11-11T20:47:13, context `host-root`, 3 page(s) of chapter 07.

Book pages: `chapter07/changingowner.html`, `chapter07/kernfs.html`, `chapter07/chroot.html`

Exit criteria this phase claims to meet:

- Ownership of the $LFS directories is root:root, not lfs.
- proc, sysfs and devpts are mounted inside $LFS and /dev is bind-mounted.
- The chapter 7 `env -i` invocation runs a command inside $LFS seeing only HOME, TERM, PS1, PATH, MAKEFLAGS, TESTSUITEFLAGS.

Failures: none

The phase's exit criteria, evaluated after the pages ran:

- `ok` `findmnt -t proc -o SOURCE /mnt/lfs/proc`
- `ok` `findmnt -t sysfs -o SOURCE /mnt/lfs/sys`
- `ok` `findmnt -t devpts -o SOURCE /mnt/lfs/dev/pts`
- `ok` `test "$(stat -c %U /mnt/lfs/usr)" = root`

Checkpoint: `after`

## temp-system — FAILED

2026-11-11T20:48:53 → 2026-11-11T20:48:58, context `chroot-root`, 3 page(s) of chapter 07.

Book pages: `chapter07/creatingdirs.html`, `chapter07/createfiles.html`, `chapter07/gettext.html`

Exit criteria this phase claims to meet:

- The chroot's own /etc exists: mtab, hosts, passwd, group, and the directory tree the pages create.
- Gettext, Bison, Perl, Python, Texinfo and Util-linux are installed in the temporary system.
- Every command block of every page exited 0.

Failures: none

The phase's exit criteria, evaluated after the pages ran:

- `FAILED 1` `sudo chroot /mnt/lfs /bin/bash -c "test -x /usr/bin/perl -a -x /usr/bin/make -a -x /usr/bin/python3"`

Checkpoint: `after`

## sources — passed

2026-11-11T20:51:43 → 2026-11-11T20:51:46, context `host-root`, 1 page(s) of chapter 03.

Book pages: `chapter03/packages.html`

Exit criteria this phase claims to meet:

- Every URL in base.lock is present in $LFS/sources with the MD5 the book published — the book's checksum, not one computed here.
- Nothing was fetched that base.lock does not name.

Failures: none

The phase's exit criteria, evaluated after the pages ran:

- `ok` `93 files, MD5 as published by the book`

Checkpoint: `none`

## temp-system — FAILED

2026-11-11T20:51:46 → 2026-11-11T20:51:50, context `chroot-root`, 3 page(s) of chapter 07.

Book pages: `chapter07/creatingdirs.html`, `chapter07/createfiles.html`, `chapter07/gettext.html`

Exit criteria this phase claims to meet:

- The chroot's own /etc exists: mtab, hosts, passwd, group, and the directory tree the pages create.
- Gettext, Bison, Perl, Python, Texinfo and Util-linux are installed in the temporary system.
- Every command block of every page exited 0.

Failures: none

The phase's exit criteria, evaluated after the pages ran:

- `FAILED 1` `sudo chroot /mnt/lfs /bin/bash -c "test -x /usr/bin/perl -a -x /usr/bin/make -a -x /usr/bin/python3"`

Checkpoint: `after`

## sources — passed

2026-11-11T20:52:16 → 2026-11-11T20:52:19, context `host-root`, 1 page(s) of chapter 03.

Book pages: `chapter03/packages.html`

Exit criteria this phase claims to meet:

- Every URL in base.lock is present in $LFS/sources with the MD5 the book published — the book's checksum, not one computed here.
- Nothing was fetched that base.lock does not name.

Failures: none

The phase's exit criteria, evaluated after the pages ran:

- `ok` `93 files, MD5 as published by the book`

Checkpoint: `none`

## temp-system — passed

2026-11-11T20:52:19 → 2026-11-11T21:03:12, context `chroot-root`, 8 page(s) of chapter 07.

Book pages: `chapter07/creatingdirs.html`, `chapter07/createfiles.html`, `chapter07/gettext.html`, `chapter07/bison.html`, `chapter07/perl.html`, `chapter07/Python.html`, `chapter07/texinfo.html`, `chapter07/util-linux.html`

Exit criteria this phase claims to meet:

- The chroot's own /etc exists: mtab, hosts, passwd, group, and the directory tree the pages create.
- Gettext, Bison, Perl, Python, Texinfo and Util-linux are installed in the temporary system.
- Every command block of every page exited 0.

Failures: none

The phase's exit criteria, evaluated after the pages ran:

- `ok` `sudo chroot /mnt/lfs /bin/bash -c "test -x /usr/bin/perl -a -x /usr/bin/make -a -x /usr/bin/python3"`

Checkpoint: `after`

## final-system — FAILED

2026-11-11T21:05:18 → 2026-11-11T21:05:21, context `chroot-root`, 1 page(s) of chapter 08.

Book pages: `chapter08/pkgmgt.html`

Exit criteria this phase claims to meet:

- Every package page's command blocks exited 0, and where a page states a test is known to fail, that is the only failure recorded.
- The debugging-symbols, stripping and cleanup pages ran.

Failures: `block 2 rc 2`

The phase's exit criteria, evaluated after the pages ran:

- `ok` `sudo chroot /mnt/lfs /bin/bash -c "command -v gcc > /dev/null && command -v make > /dev/null && command -v perl > /dev/null && command -v python3 > /dev/null"`

Checkpoint: `after`

## sources — passed

2026-11-11T21:16:04 → 2026-11-11T21:16:06, context `host-root`, 1 page(s) of chapter 03.

Book pages: `chapter03/packages.html`

Exit criteria this phase claims to meet:

- Every URL in base.lock is present in $LFS/sources with the MD5 the book published — the book's checksum, not one computed here.
- Nothing was fetched that base.lock does not name.

Failures: none

The phase's exit criteria, evaluated after the pages ran:

- `ok` `93 files, MD5 as published by the book`

Checkpoint: `none`

## temp-system — FAILED

2026-11-11T21:16:06 → 2026-11-11T21:16:08, context `chroot-root`, 2 page(s) of chapter 07.

Book pages: `chapter07/creatingdirs.html`, `chapter07/createfiles.html`

Exit criteria this phase claims to meet:

- The chroot's own /etc exists: mtab, hosts, passwd, group, and the directory tree the pages create.
- Gettext, Bison, Perl, Python, Texinfo and Util-linux are installed in the temporary system.
- Every command block of every page exited 0.

Failures: `block 6 never reported (the page stopped early)`

The phase's exit criteria, evaluated after the pages ran:

- `FAILED 1` `sudo chroot /mnt/lfs /bin/bash -c "test -x /usr/bin/perl -a -x /usr/bin/make -a -x /usr/bin/python3"`

Checkpoint: `after`

## sources — passed

2026-11-11T21:19:01 → 2026-11-11T21:19:04, context `host-root`, 1 page(s) of chapter 03.

Book pages: `chapter03/packages.html`

Exit criteria this phase claims to meet:

- Every URL in base.lock is present in $LFS/sources with the MD5 the book published — the book's checksum, not one computed here.
- Nothing was fetched that base.lock does not name.

Failures: none

The phase's exit criteria, evaluated after the pages ran:

- `ok` `93 files, MD5 as published by the book`

Checkpoint: `none`

## temp-system — passed

2026-11-11T21:19:04 → 2026-11-11T21:28:16, context `chroot-root`, 8 page(s) of chapter 07.

Book pages: `chapter07/creatingdirs.html`, `chapter07/createfiles.html`, `chapter07/gettext.html`, `chapter07/bison.html`, `chapter07/perl.html`, `chapter07/Python.html`, `chapter07/texinfo.html`, `chapter07/util-linux.html`

Exit criteria this phase claims to meet:

- The chroot's own /etc exists: mtab, hosts, passwd, group, and the directory tree the pages create.
- Gettext, Bison, Perl, Python, Texinfo and Util-linux are installed in the temporary system.
- Every command block of every page exited 0.

Failures: none

The phase's exit criteria, evaluated after the pages ran:

- `ok` `sudo chroot /mnt/lfs /bin/bash -c "test -x /usr/bin/perl -a -x /usr/bin/make -a -x /usr/bin/python3"`

Checkpoint: `after`

## sources — passed

2026-11-11T22:56:46 → 2026-11-11T22:56:54, context `host-root`, 1 page(s) of chapter 03.

Book pages: `chapter03/packages.html`

Exit criteria this phase claims to meet:

- Every URL in base.lock is present in $LFS/sources with the MD5 the book published — the book's checksum, not one computed here.
- Nothing was fetched that base.lock does not name.

Failures: none

The phase's exit criteria, evaluated after the pages ran:

- `ok` `93 files, MD5 as published by the book`

Checkpoint: `none`

## sources — passed

2026-11-12T00:44:18 → 2026-11-12T00:44:27, context `host-root`, 1 page(s) of chapter 03.

Book pages: `chapter03/packages.html`

Exit criteria this phase claims to meet:

- Every URL in base.lock is present in $LFS/sources with the MD5 the book published — the book's checksum, not one computed here.
- Nothing was fetched that base.lock does not name.

Failures: none

The phase's exit criteria, evaluated after the pages ran:

- `ok` `93 files, MD5 as published by the book`

Checkpoint: `none`
