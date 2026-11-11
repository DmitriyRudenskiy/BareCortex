# Build steps

The eleven phases `scripts/build/build.py` runs, in the order the book's chapters
put them, and the checks that decide whether a phase may run at all. The
per-page evidence of an actual run is in `docs/BUILD-LOG.md` and
`build-evidence/<phase>/`; this file is the map, not the record.

```
python3 scripts/build/build.py pages <phase>   # what would run, in book order
python3 scripts/build/build.py run <phase>     # run it, then its criteria
python3 scripts/build/build.py resume          # the next phase that has not passed
python3 scripts/build/build.py status          # every phase's record
```

`run` takes the build root lock (`build-evidence/.build-lock`): a second
invocation is refused, not queued, because two builds would share one `$LFS`,
one qcow2 and one snapshot chain. `resume` reads the records, rolls the disk back
to the checkpoint the previous phase left, and runs the phase that has not
passed — a failed phase keeps its name, so a resume repeats it rather than
skipping past it.

## The phases

| phase | chapter | pages | checkpoint | what passing means |
|---|---|---|---|---|
| `host-prep` | 02 | 5 | — | the host has the tools, `lfs` exists, `$LFS` is a real device |
| `sources` | 03 | 2 | — | every file in `base.lock` is in `$LFS/sources` with its checksum |
| `chroot-layout` | 04 | 3 | after | `$LFS/{dev,proc,sys,run}` work, `$LFS/tools` belongs to `lfs` |
| `cross-toolchain` | 05 | 5 | after | `$LFS/tools` holds the cross gcc, as, ld, and libstdc++ links |
| `temp-tools` | 06 | 17 | after | the temporary tools are installed, including a C++ compiler |
| `enter-chroot` | 07 | 5 | after | the `lfs` user is inside `$LFS` with the book's six variables |
| `temp-system` | 07 | 6 | after | the second pass binutils/gcc are in `/tools` |
| `final-system` | 08 | 85 | after | the system is built in `/usr`, its test suites read against the book |
| `configuration` | 09 | 7 | after | `/etc` is configured: fstab, timezone, locale, user, network |
| `bootable` | 10 | 3 | after | a bootloader and kernel are installed on the disk |
| `first-boot` | 11 | 2 | — | the disk boots on its own and answers over ssh |

A page is a page of the pinned edition, run as its command blocks, in the book's
order, with nothing added. `pages <phase>` prints them; the list comes from the
edition's own contents page, so a page cannot be left out by editing a list here
(`check_no_page_is_dropped_from_the_index` is what keeps that true).

## The checks

`python3 scripts/build/test_build.py` — no guest needed.

- [x] `check_mismatches` — a wrong, absent or undownloaded checksum names the file
- [x] `check_lock_files` — `base.lock` still carries 88 packages and 5 patches
- [x] `check_sources_named` — a page's commands are scanned for file names, not prose
- [x] `check_record_identity_and_atomicity` — a record names its `base.lock` bytes and edition, and a crash leaves the previous one readable
- [x] `check_chroot_environment_is_the_books` — the chroot gets the book's six variables and nothing else
- [x] `check_build_root_lock` — one build per build root, and a dead owner is named as stale
- [x] `check_test_expectations_are_current` — `test-expectations.toml` says what the pinned pages say today
- [x] `check_core_suite_cannot_be_skipped` — an override cannot skip glibc's, GCC's or Binutils' suite
- [x] `check_no_page_is_dropped_from_the_index` — every page the contents page lists is a page the build knows
- [x] `check_resume_starts_at_the_failed_phase` — the phase that failed is the phase a resume runs
- [x] `check_sources_needed_for_every_page` — any page can be asked what it needs, including the chapter 7 pages that need nothing
- [x] `check_guard_on_the_real_pages` — an unsourced tarball, in a page or in an override, stops the phase

## What the build refuses to do

It will not run a phase whose pages name a source `base.lock` does not carry, or
whose sources are not on the disk — the phase stops before its first command and
names the file and the command that puts it there. It will not skip a test suite
the book calls utmost important, even through `build-overrides.toml`. It will not
pretend a page ran: every block's exit code is in `build-evidence/<phase>/`, and
a phase whose record says `passed` has a record for every page it ran.
