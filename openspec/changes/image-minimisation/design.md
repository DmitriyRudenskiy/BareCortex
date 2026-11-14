# Design: image-minimisation

## Context

Motivation is in `proposal.md` — Why. What follows is the state of the three
things this change has to read: the package list, the book, and the binaries.

**The package list.** `base.lock` declares **89 packages**, all of class
`source-built`, of which **60 are built by chapter 8 pages only** — final-system
software, not toolchain. Five of them install documentation and nothing else:
`Man-pages`, `Python Documentation`, `Sqlite Documentation`, `Systemd Man Pages`,
`Tcl Documentation`. There is no editor in the list other than Vim — `ed`, `nano`
and `joe` are not entries — which is the fact that turns "delete the editor" from
a size decision into a support decision. Counts are read out of the file with
`python3 -c "import tomllib,pathlib;print(len(tomllib.loads(pathlib.Path('base.lock').read_text())['package']))"`,
not from a document that would then need keeping current. The array is `[[package]]`
and each entry's identifier field is `name` (`check-base-lock.py` reads the same
field), so that is the key every other file in this change joins on.

**The book publishes no dependency graph, and says why there is no package
manager.** Two probes of the pinned edition's cached pages:

- `grep -l RDEPENDS .book-cache/*.html` → **0 files**. The LFS pages carry no
  required/optional dependency lists; those are a BLFS convention. What an LFS
  page *does* carry is a "Contents" section — the programs and libraries the
  package installs. So the book gives *provides*, never *depends-on*, and the
  reverse direction, which is the only one a removal needs, exists nowhere in
  writing.
- `chapter08/pkgmgt.html` (§8.2) states outright that it "will not talk about nor
  recommend any particular package manager", and §8.2.2 lists the techniques with
  their drawbacks. That section is this change's specification for the ownership
  map, written before this repository existed.

**`ldd(1)`, as cited.** The Russian translation of the glibc page at
`opennet.ru/man.shtml?topic=ldd&category=1` (translated 2003, retrieved
2026-11-13) establishes two things that decide the shape of the scan. Its SYNOPSIS
is `ldd [ОПЦИЯ]... ФАЙЛ...` and its DESCRIPTION that it prints the libraries
required by **each program or library named on the command line** — the unit is a
file, so a package-level answer requires a file list per package first. Its ОШИБКИ
(BUGS) section says that pointing `ldd` at a binary it cannot interpret results in
**an attempt to execute that binary with `argc = 0`**, with unpredictable results.
The page also documents only `-V/--version`, `-v/--verbose`, `-d/--data-relocs`,
`-r/--function-relocs`, `--help`, and warns that long-option support differs
between libc versions — so the authority for what the built system's `ldd` accepts
is that system's own `ldd --version` (glibc 2.43 per `base.lock`), not a 2003
translation. Nothing in this change depends on an option the page does not name.

**The graph engine is already installed.** `graphify` 0.9.46 is present on the
build host (`uv tool`), and its extraction schema is small and checkable: nodes
need `id`, `label`, `file_type`, `source_file`; edges need `source`, `target`,
`relation`, `confidence` ∈ {EXTRACTED, INFERRED, AMBIGUOUS}, `source_file`. Its
`affected` subcommand is a reverse traversal whose default relation set already
includes `requires` — the exact question "what breaks if this goes away", already
implemented.

## Goals / Non-Goals

**Goals**

- A total, machine-checked verdict over the 89 declared packages, with the
  evidence for each kept beside it.
- A per-package file list captured at install time, so ownership is a record.
- A dependency graph whose edges are all read off the image, queryable in
  reverse, and committed.
- One phase that applies the trim, and a gate that is a rebuild plus a boot.

**Non-Goals**

- Stripping binaries and removing docs at compile time. The edition has pages for
  this (`chapter08/stripping.html`, `chapter08/cleanup.html`,
  `chapter08/aboutdebug.html`) and they are the authority; this change removes
  packages, not bytes from a package that stays.
- A package manager, a resolver, or an install database — `build-orchestration`
  D1 declined those and this change does not reopen it by the back door. The
  verdict file is a decision record, not a database: it installs nothing.
- Any figure for what minimisation saves. The first build that measures the image
  before and after the trim phase produces the number; until then the honest
  figure is "unmeasured".
- The GPU stack's own pruning (`gpu-stack` 6.7, CUDA components after install).
  Same verdict vocabulary, different mechanism, because a vendor installer writes
  files no `make install` of ours wrapped.

## Decisions

### D1 — The verdict file is hand-authored, outside `base.lock`, keyed by name

`base.lock` is generated: `scripts/build/derive-base-lock.py` prints "derived, do
not edit by hand" into the file it writes, and a hand-authored field inside it is
that script's next overwrite victim. `class-map.toml` exists for precisely this
reason and is the precedent to copy — hand-authored, repository root, keyed by
identifiers that must resolve against the derived file. So `image-trim.toml` holds
one row per package: `name` (the identifier `base.lock` uses, so the two files can
be joined and neither can be silent about a package), `verdict`, `evidence` (the
scan run and file list), `reason`, `last_consumer` for `build-only`, `recovery` for
anything not `ships`. It records
no version, URL, checksum, or page — the check rejects a row that does, which is
what stops "the verdict file" from decaying into the second package list
`base-system` forbids. The alternative considered was a field in `base.lock`
maintained by teaching the derivation to preserve it; rejected because the
derivation would then own a decision it cannot derive, and a package the book
stops building would silently lose its verdict along with its row.

**Totality is the requirement, not the contents.** A file listing only the
packages someone objects to is how an undecided image survives a review. The
check fails on a missing row and on a stale one, in both directions.

### D2 — Ownership by the book's timestamp technique, cross-checked against the page's own Contents

§8.2.2.4 "Timestamp Based": stamp a file before the install, `find` for anything
newer afterwards, keep the list as the package's log. The book names its two
drawbacks and both are handled rather than ignored:

- *"files installed with any timestamp other than the current time will not be
  tracked"* — cross-checked against the page's Contents section, which names the
  programs and libraries the package is supposed to install. A Contents entry
  missing from the capture is reported as under-tracked and the package becomes
  unremovable until it is explained. This is the only place the book's Contents
  prose earns its keep as data.
- *"logs are not reliable if two packages are installed simultaneously"* — the
  build root is already exclusively locked (`build-orchestration` D11), so one
  package at a time is a property of the build, not a hope.

Alternatives from the same section, rejected on the book's own grounds: LD_PRELOAD
tracing (§8.2.2.5) requires every executable involved to be dynamic and non-setuid
and can perturb the install it is watching; `strace` is not in `base.lock`;
DESTDIR archives (§8.2.2.3, §8.2.2.6) would require editing the book's recipes for
the packages that ignore `DESTDIR`, which is the delta this project refuses to
carry for convenience.

Capture lives in the evidence directory beside the command output D11 already
keeps, and is one file list per **page install** — including the chapter 5–7
passes, whose files mostly disappear with the temp system but whose names are
needed to explain why a chapter 8 binary links something in `/tools`. The unit is
the page and not the package because the timestamp technique cannot see a
boundary the book does not draw: eight pages install more than one package (D8),
and where a page does, its list is split by the packages that page's own Contents
sections name. A page whose two products the Contents sections cannot separate
yields one list and inseparable verdicts, which is recorded rather than averaged
over.

**Installed is not present.** The edition's own cleanup and stripping steps delete
and rewrite files after the packages that installed them. The file lists are
therefore an install record, not a mirror of the disk, and every post-trim check
says which of the two it means: "no file of a trimmed package is present" is a
disk statement; "the package owns no file any other package requires" is a record
statement.

### D3 — Edges from ELF headers for the whole image; `ldd` where only `ldd` answers

Two tools, each used for what the cited page says it is for.

- **Whole-image pass:** classify every file in the image; for each ELF object
  record its required library names from its own headers (`objdump -p` or
  `readelf -d`), and attribute each name to the package whose file list contains
  the file that satisfies it. Nothing is executed. This is the direct consequence
  of the page's BUGS section: a scan that walks `/usr/bin` and `/usr/sbin` will
  meet setuid binaries (`mount`, `su`, `chsh`, `newgrp` — Shadow and Util-linux
  install them) and an ancient or malformed object, and `ldd` is documented to
  *run* what it cannot interpret. A build-time scan of an image is also a scan of
  files whose loader path and `ld.so.cache` belong to the image, not the host.
- **`ldd` where the loader's answer is the question:** confirming the runtime
  closure of a named program — that `man` really resolves only libraries still in
  the image after the trim — run **inside** the image (the chroot or the VM),
  never on the build host, where it would resolve the host's glibc and invent
  edges. `ldd --version` is recorded from inside the image first, so the options
  used are the ones that exist there.

Three classes of file get recorded rather than skipped, because "no edges" and
"not examined" must never share a value: statically linked objects, scripts (no
ELF edges; their dependencies are interpreters and `$PATH` commands, which the
rebuild gate is responsible for), and objects that reference the dynamic loading
entry point — a `dlopen` user has an edge the header scan cannot see, so the
providing package is marked as having an unresolved consumer and cannot be removed
on scan evidence alone.

**The graph over-approximates, deliberately.** A binary that links `libfoo` but
never calls it still blocks removing `foo`. `ldd`'s unused-dependency reporting is
the tool that would refine this; it is not among the options the cited page
documents, so it is not used, and the direction of the error is the safe one — an
over-approximation blocks removals, it never authorises one.

### D4 — graphify as the graph engine, never as the author of edges

The scan emits graphify's extraction JSON directly: one node per package and per
shared library, one `requires` edge per attributed requirement, every edge
`confidence: "EXTRACTED"` with `source_file` naming the scan artifact and the
scanned file. Where two packages' file lists both satisfy one soname the edge is
`AMBIGUOUS`, which is what that vocabulary is for. The LLM extractor is not run:
an `INFERRED` edge in a removal decision is a guess about why the node will not
boot, and graphify's own honesty rule is that edges are never invented.

Then the existing tooling does the rest — `build_from_json(directed=True)` and
`to_json` for `graphify-out/graph.json`, and the queries are the CLI's:
`graphify affected <pkg> --relation requires` for the blast radius, `graphify
god-nodes` for the packages everything requires, `graphify query` for the human
question, `graphify export html` for the picture. Committed: the generator, the
report, and the shared-page table — text, reviewable in a diff. Not committed:
`graph.json`, the extraction JSON, and the HTML. `.gitignore` ignores `*.json`
repo-wide under the heading "Datasets (too large for git)", and a 110 KB generated
graph is not the exception that rewrites that rule; one command regenerates it, and
what belongs in the repository is the report naming the scan run the graph came
from — which is also what 5.5's staleness check reads.

The alternative was a purpose-built script and a Graphviz file. Rejected: the
reverse traversal, the clustering, the report, and the interactive view already
exist, are pinned by version, and one fewer bespoke artifact is the whole point.
The dependency this adds is a **build-host** tool — `graphify` and its Python
dependencies must never appear in `base.lock`, and a task says so, because a
change about removing packages that adds one to the image would be a farce worth
avoiding by check rather than by care.

### D5 — Two mechanisms, one record: `absent` skips the page, `build-only` removes it in the trim phase

`absent` means the orchestrator never runs that package's page. It is the cheaper
and safer mechanism — nothing is installed, so nothing has to be deleted, and no
file list is needed to prove the deletion complete. It is legal only when no phase
consumes the package, no image file requires it, **and the package is the sole
product of every page that builds it** (D8 — a skip is page-granular and cannot
pick out one of two packages a page installs). `build-only` is for the packages the
build genuinely uses — Tcl, Expect, DejaGNU and Perl exist largely to run test
suites — and its removal is the trim phase's job.

The phase's position is a rule, not an index: **after the last phase whose
recorded commands consume anything the phase removes, before the disk is frozen.**
Today `phases.toml` ends at chapter 11 and has no phase for the BLFS layer or the
GPU stack, so the rule is the durable form and the index is provisional. It also
runs **before** the edition's stripping and cleanup steps, so those do not spend
time on files about to be deleted, and so the trim's post-conditions are evaluated
against a file set nothing has rewritten since.

Skipping a page must not remove its `base.lock` entry — the entry records what the
book builds and what the sources are, and `check-base-lock.py --coverage` asks the
edition which pages build packages. Skipping keeps the entry, so coverage stays
green and the image is smaller. That is the difference between a delta and a fork
of the book.

### D6 — The scan is evidence; the rebuild is the verdict

A header scan sees ELF and nothing else. It cannot see a `.pc` file a later
`configure` asks `pkg-config` for, a CMake package file, a Python module imported
by name, an interpreter named in a shebang, or a service that calls a binary by
name. So acceptance is: build from the recorded checkpoint with the trim applied,
reach the multi-user runlevel, and re-run the trim idempotently. A failure names
the package, the file, and the step; the fix is the verdict, not the build.

Rollback is `build-orchestration`'s checkpoint (D3), already implemented as a qcow2
snapshot per phase. The one obligation this change adds is positional: the phase
before the trim must carry a checkpoint, so "undo the trim" is a restore and not a
rebuild.

### D7 — Verdicts this change must not take alone

- **The compiler.** Whether GCC, Binutils, Make, Meson, Ninja and Pkgconf are
  `ships` or `build-only` depends on whether anything installs a Python package
  from an sdist on the running system — a wheel with no matching wheel builds, and
  building needs a compiler. That is `userspace-and-ml-stack`'s decision (its D3
  on wheel-authoritative runtime), and this change records the answer it gets.
- **The editor.** Vim is the only file editor among the 89. Removing it leaves an
  image with `sed` and `awk` and no way to edit `/etc/conf.d/ai-training` at a
  console. Shipping a smaller editor is an *addition* — a new `base.lock` entry of
  some class — not a removal, and it is a worse trade than it sounds. The default
  verdict is `ships` and the row says why.
- **The GPU stack's view of `/usr/lib`.** `gpu-stack` D4 owns the loader-path
  decision; a trim that removes a library the NVIDIA userspace `dlopen`s is that
  change's call, and the unresolved-consumer class in D3 is what routes it there.

### D8 — The page is the unit of skipping; the package is the unit of the verdict

Found by the declared-build graph in `graph/`, not by reading: eight of the 111
pages `base.lock` cites build more than one package, and 15 of the 89 packages
share a page with another. `chapter08/tcl.html` builds Tcl and Tcl Documentation;
`chapter08/udev.html` builds Systemd, Systemd Man Pages and the Udev-lfs Tarball;
`chapter05/gcc-pass1.html` and `chapter06/gcc-pass2.html` each build GCC, GMP, MPC
and MPFR; `Python Documentation` is built by two pages, in chapter 7 and chapter 8.
The full table is in `graph/README.md`.

The consequence is mechanical. A skip is a page, so `absent` can only ever name a
package that is the sole product of every page building it — otherwise the skip
removes its neighbour too, and a verdict that silently removes a second package is
the definition of an unaccounted removal. Four of the five documentation packages
are in that class, so they are `build-only` by construction rather than by
judgement, and only `Man-pages` can be skipped. The check that enforces it reads
the same edges the graph holds: a row whose verdict is `absent` and whose page
builds another package fails, naming both.

The other half of the consequence is that **a package can be built by several
pages**, so "the page that builds it" is a set. `absent` means every page in the
set is skipped, which for `Python Documentation` would mean skipping chapter 7's
Python page — absurd on its face, and exactly the kind of error a check is better
than a paragraph for.

## Risks / Trade-offs

- **A skip removes more than the verdict names** — the mechanism is page-granular
  and eight pages build two or three packages → D8's sole-product check refuses the
  verdict at review time, and the shared-page table is committed next to the graph
  that exposed it.
- **The timestamp capture under-records** (the book's own caveat) → the Contents
  cross-check turns silent under-recording into a named package that cannot be
  removed; the trim refuses un-tracked packages rather than guessing.
- **A removal breaks something the scan cannot see** → the rebuild gate is the
  check, and it is the acceptance criterion, not a nice-to-have after it.
- **The image becomes unrepairable** — no compiler, no editor, no man pages on a
  machine reached over SSH at 3am → every non-`ships` row carries `recovery`, and
  the recovery is a phase and a command, not a package manager.
- **Verdicts written before the first full build are guesses** → they are recorded
  as provisional, the gate is what makes them true, and the tasks that need a
  built image are `(build)`-marked and blocked until one exists. This is the same
  honesty `base-system-and-init` OQ4 keeps about the systemd link count.
- **`absent` changes what a later package configures against** — removing a
  library before its neighbours build can silently disable a feature rather than
  fail (the mechanism `base-system-and-init` OQ2 documents for OpenRC's optional
  `audit`/`libselinux`) → the rebuild gate is again the check, and it is why a
  package is never marked `absent` while anything builds after it.
- **The graph is only as fresh as the scan** → the graph carries the scan run's
  identity and a check fails a graph older than the file lists it describes.

## Migration Plan

Land in the order that keeps every step green: the capture (inert — it records and
changes nothing), the verdict file with a total set of provisional rows all
defaulting to `ships`, the scan, the graph, then the trim phase with one
package as its first subject, then the rest. The first subject is `Man-pages`: it
installs no ELF and no libraries, it is the sole product of `chapter08/man-pages.html`,
and so it is the only one of the five documentation packages that the skip mechanism
can legally remove (D8). Its removal exercises the whole mechanism — capture, scan,
graph query, phase, rebuild, boot — with nothing that can link it, and the four
documentation packages that share a page with their subject follow as `build-only`,
which exercises the file-list half instead.
