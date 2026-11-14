# Tasks: image-minimisation

Scope note: markers are the classes in `openspec/project.md`. Everything here is
`(build)` — a file check, a phase run in the guest, `objdump` and `ldd` against
built libraries — or `write`, satisfied by a file in this repository. Nothing is
`(gpu)` and nothing is `(target)`: what the trimmed image does on the target
machine is `target-hardware-bring-up`'s claim to make, and this change hands it the
phase and the record. **A task with no marker is `write`**; `(build)` appears on the
eleven tasks finished only by an execution whose output is recorded — 3.4, 3.6,
4.5, 4.6, 6.4, 6.6, 7.1, 7.2, 7.3, 7.4, 8.1. The count is grep-shaped and one off:
`grep -c '^- \[ \].*`(build)`' tasks.md` returns 12 because 9.5 names the marker
without carrying one.

Blocked inputs are recorded as blocked, not silently deferred. The verdicts group 2
writes are provisional until 7.1 and 7.2 run, and the rows say so, the way
`base-system-and-init` keeps OQ4 open.

## 0. Inputs this change does not have

- [ ] 0.1 Confirm and name the prerequisites: `build-orchestration` produces
      `base.lock` (2.1) and per-phase checkpoints (4.4), the orchestrator records
      per-package evidence (D11), and the guest from `build-orchestration` 0.1
      exists. Every `(build)` task below is blocked without all three
- [ ] 0.2 Record the image size before any removal, from the guest:
      `du -sx --exclude=/proc --exclude=/sys --exclude=/dev /` at the
      `final-system` checkpoint. **This is the baseline every later figure is
      subtracted from, and no saving may be stated anywhere in this change until it
      exists**
- [ ] 0.3 Ask `userspace-and-ml-stack` the question that decides the toolchain
      verdict (D7): does anything on the running system install a Python package
      from an sdist? Record the answer, or record it unanswered and leave GCC,
      Binutils, Make, Meson, Ninja and Pkgconf at `ships`
- [ ] 0.4 Ask `gpu-stack` which NVIDIA userspace objects are loaded by name rather
      than linked, for D3's unresolved-consumer class. Until it answers, no library
      under `/usr/lib` the driver or CUDA userspace could `dlopen` may be marked
      `absent` or `build-only`
- [ ] 0.5 Record that `phases.toml` has no phase for the BLFS layer or the GPU
      stack, so the trim phase's position is the rule (after the last consumer,
      before the freeze) and not an index — and name the change that will add the
      phases making the rule resolvable

## 1. Read the two sources this change cites, and record what they permit

- [ ] 1.1 Record from `ldd(1)` as cited (`opennet.ru/man.shtml?topic=ldd&category=1`,
      retrieved 2026-11-13) the three facts the scan depends on: the SYNOPSIS takes
      `ФАЙЛ...`, so the unit is a file; the BUGS section says an uninterpretable
      binary is **executed** with `argc = 0`; the documented options are `-V`, `-v`,
      `-d`, `-r`, `--help`, with long-option support differing by libc. Record that
      it is a translation and its date, so nobody treats it as authority on glibc 2.43
- [ ] 1.2 Record what the built `ldd` accepts, from inside the image: `ldd --version`
      and `ldd --help` in the chroot, naming the glibc `base.lock` pins. Any option
      the scan uses SHALL appear in both this output and 1.1; record the others as
      unusable rather than using them
- [ ] 1.3 Record the pinned edition's §8.2.2 techniques and the choice among them
      (D2), quoting the timestamp technique's two drawbacks — the checks exist to
      cover them — and the §8.2.2.5 condition that rejects LD_PRELOAD tracing
- [ ] 1.4 Extract per package page the "Contents" list of installed programs and
      libraries, and record how many pages yield one. `grep -l RDEPENDS
      .book-cache/*.html` returns zero files, so this is the only
      dependency-adjacent data the base book publishes, used as a cross-check and
      never as an edge
- [ ] 1.5 Record what the edition's `stripping`, `cleanup`, and `aboutdebug` pages
      delete and rewrite, so the trim runs before them (D5) and no later task claims
      credit for bytes those pages already remove

## 2. The verdict file

- [ ] 2.1 Write `image-trim.toml` at the repository root: one row per package with
      `package`, `verdict`, `reason`, `evidence`, `recovery`, and `last_consumer`
      when the verdict is `build-only`. Hand-authored like `class-map.toml`, and
      outside `base.lock` for the reason D1 gives
- [ ] 2.2 Populate it with **89 rows**, one per `base.lock` entry, all `ships`, each
      reading "provisional — no scan has run". The count is the check's input, not a
      claim: `python3 -c "import tomllib,pathlib;print(len(tomllib.loads(pathlib.Path('base.lock').read_text())['package']))"`.
      The array is `[[package]]` and each entry's identifier field is `name` — so a
      row reads `[[package]] name = "Acl"`, and the check keys on `p["name"]`, the
      same field `check-base-lock.py` reads
- [ ] 2.3 Write the totality check: a `base.lock` entry with no row fails naming the
      package; a row with no `base.lock` entry fails naming the row; a row carrying
      `version`, `url`, `md5`, or `pages` fails as package data in the wrong file.
      **It must fail loudly on an empty verdict file** — the defect class
      `class-map.toml` names for its own coverage check
- [ ] 2.4 Prove 2.3 against fixtures, not against the real file: a three-entry lock
      against a two-row verdict file, one row naming an absent package, and assert
      both failures and their text. A check proven only on passing input has not
      been checked
- [ ] 2.5 Assert the derivation never touches it: run `derive-base-lock.py` against
      the cache with a verdict file present, and assert the verdict file is
      byte-identical afterwards and `base.lock` gained no verdict field
- [ ] 2.6 Add the check to the same gate `check-base-lock.py` runs in, offline, so a
      verdict cannot be added by a commit that skips the gate

## 3. Ownership: the file list per package

- [ ] 3.1 Implement the capture around every package **page's** install step (D2, D8):
      stamp a file, run the install block, `find` the image root for anything newer,
      record the paths. Stdlib or shell, no package `base.lock` does not declare.
      Where the page installs two declared packages, split the list by those pages’
      Contents sections and record the pair as inseparable when it cannot
- [ ] 3.2 Store one list per package install in the evidence directory beside the
      command output D11 keeps, naming phase, page, and version, so a list is
      attributable to a build run
- [ ] 3.3 Capture the chapter 5–7 passes as well as chapter 8, recording that a
      temp-system file is expected to disappear — an install record, not a disk mirror
- [ ] 3.4 `(build)` Run the capture for one chapter and diff it against that
      chapter's Contents lists, recording every path missed. This measures the
      book's caveat instead of quoting it
- [ ] 3.5 Write the under-tracked check: a Contents entry absent from the captured
      list marks the package unremovable and names the path (spec: *The recorded
      list is smaller than the book's own contents*)
- [ ] 3.6 `(build)` Record which packages' installs write files with preserved
      timestamps — `cp -p`, `tar -xp`, a `make install` copying mtime — found by
      running 3.4 across the phases, not guessed from package names
- [ ] 3.7 Write the disputed-file rule: a path in two packages' lists is recorded as
      disputed, attributed to neither, and blocks any verdict that would delete it
- [ ] 3.8 Test 3.5 and 3.7 against synthetic evidence — fixture file lists and
      Contents lists — so both failures are reachable without a built image, and add
      the inseparable-page case (one page, two packages, one list)
- [ ] 3.9 Write the sole-product check (D8): a row whose verdict is `absent` fails
      when any page building that package builds another declared package, or when
      skipping the package's whole page set would remove a package another row keeps.
      Prove it on the real data — `chapter08/tcl.html`, `chapter08/udev.html`, and
      `Python Documentation`'s two pages must all fail, and `chapter08/man-pages.html`
      must pass

## 4. The scan: edges from the image, never by executing it

- [ ] 4.1 Classify every file in the image: ELF executable, shared object, archive,
      script, other. Record the counts as the scan's header, so "no edges" stays
      distinguishable from "nothing scanned"
- [ ] 4.2 For each ELF object record its required library names from its own headers
      (`objdump -p`, `readelf -d` as fallback), with the scanned file as the edge's
      source. **No file is executed here**, and the code says so where the setuid
      binaries are — `su`, `mount`, `chsh`, `newgrp` are what a walk of `/usr/bin`
      and `/usr/sbin` meets, and 1.1 is why running them is a bug
- [ ] 4.3 Attribute each required name to the package whose file list contains the
      file satisfying it, recording three outcomes separately: one provider
      (EXTRACTED), several (AMBIGUOUS, soname named), none (a dangling requirement —
      the build fails naming file and library, because the image is already broken)
- [ ] 4.4 Record the three classes the header scan cannot see, as named classes:
      statically linked objects, scripts, and objects referencing the dynamic
      loading entry point. A `dlopen` user makes the providing package unremovable on
      scan evidence alone (spec: *A library is opened by name at run time*)
- [ ] 4.5 `(build)` Run `ldd` inside the image, against the image's own loader, for
      the named programs the trim touches, and record it as the runtime-closure
      confirmation the header scan is not (D3), with 1.2's version line beside it
- [ ] 4.6 `(build)` Prove the no-execution property: put a marker-writing fixture
      object in the scan's path and assert the marker is never written and the
      fixture is reported as unscanned
- [ ] 4.7 Write the scan's output as one artifact per build run, with the run
      identified, so a verdict or a graph can name the evidence it rests on

## 5. The graph

- [ ] 5.1 Emit graphify's extraction JSON from the scan: package and library nodes,
      `requires` edges, `confidence` EXTRACTED or AMBIGUOUS, `source_file` naming the
      scan artifact and the scanned file. Validate against graphify's schema before
      building — nodes need `id`, `label`, `file_type`, `source_file`; edges need
      `source`, `target`, `relation`, `confidence`, `source_file`
- [ ] 5.2 **Do not run the LLM extractor.** Every edge in a removal graph is read off
      a file; an INFERRED edge here is a guess about why the node will not boot. Put
      the rule in the script that emits the JSON, where the next person tempted to
      enable it will read it
- [ ] 5.3 Build `graphify-out/graph.json` with graphify's own
      `build_from_json(directed=True)` and `to_json`, and generate the report.
      Directed is not decorative: reverse reachability is the question, and an
      undirected graph answers "what is nearby" instead
- [ ] 5.4 Wire the queries the spec asks for: `graphify affected <pkg> --relation
      requires` for what a removal breaks, `graphify god-nodes` for what everything
      requires, `graphify query` for the prose question
- [ ] 5.5 Write the staleness check: the graph records its scan run and fails when
      that run is older than the newest file list or package build it describes
      (spec: *The graph is older than the evidence*)
- [ ] 5.6 Commit the generator, the report, and the shared-page table; leave
      `graph.json`, the extraction JSON, and the HTML regenerable and uncommitted —
      `.gitignore` ignores `*.json` repo-wide, and the answer to a stale graph is 5.5's
      check, not a force-add. Record the graphify version in the report
- [ ] 5.7 Add a check that `graphify` and none of its Python dependencies appears in
      `base.lock`. A change about removing packages must not add one to the image to
      make its point
- [ ] 5.8 Test 5.5 and a reverse-reachability answer against a fixture graph — three
      packages, two edges, one AMBIGUOUS — so query behaviour is pinned without an image
- [ ] 5.9 Keep `graph/` honest: the declared-build graph and its generator are
      committed, and the check regenerates the shared-page table from `base.lock` and
      fails when the committed table drifts. Eight pages and fifteen packages today; a
      pin bump that changes either number has to say so here

## 6. The trim phase

- [ ] 6.1 Add the phase row to `phases.toml` with D5's position rule in its `entry`
      text, `checkpoint` on, and the reason it runs before the edition's stripping
      and cleanup steps in the row's comment
- [ ] 6.2 Implement `absent` as a skip: the orchestrator does not run the page, the
      `base.lock` entry stays, and `check-base-lock.py --coverage` still passes
      because the entry still cites the page. The skip is gated by 3.9 — a page that
      builds two packages cannot be skipped for one of them. Prove the coverage half
      with that check, not by asserting it
- [ ] 6.3 Implement `build-only` as removal from the recorded file list — never by
      name, glob, or pattern — refusing any package whose list is missing, empty,
      disputed, or under-tracked
- [ ] 6.4 `(build)` Prove idempotency: run the phase twice, assert the second run
      removes nothing and reports zero, and that a second run over an already-trimmed
      image is not an error
- [ ] 6.5 Write the post-trim checks: no file of any trimmed package is present; no
      file in the image requires a library no present package provides; every service
      in the default runlevels still starts
- [ ] 6.6 `(build)` Prove the reinstatement guard: in a scratch guest, install a
      trimmed package's file from a later phase and assert that phase fails naming
      the package and itself (spec: *A later phase reinstalls a trimmed package*)
- [ ] 6.7 Record the rollback as the checkpoint restore it is, and confirm the phase
      before the trim carries a checkpoint — if it does not, the trim may not run
- [ ] 6.8 Take `Man-pages` as the first subject: no ELF, no libraries, and the sole
      product of `chapter08/man-pages.html`, so it is the only documentation package
      the skip mechanism can legally remove (D8). It exercises capture, scan, graph,
      phase, rebuild, and boot with nothing that can link it. `Python Documentation`,
      `Sqlite Documentation`, `Tcl Documentation` and `Systemd Man Pages` follow as
      `build-only`, which exercises the file-list half instead — they are built by the
      pages that build their subjects, so they were never skippable

## 7. The gate that makes a verdict true

- [ ] 7.1 `(build)` With one verdict applied, rebuild from the recorded checkpoint
      and record the phase results — every later package still configures, builds,
      and installs
- [ ] 7.2 `(build)` Boot the trimmed image and record it reaching the multi-user
      runlevel, with `runlevel` and each default-runlevel service's status as evidence
- [ ] 7.3 `(build)` Record before/after image size with 0.2's command, and only then
      allow a figure into any document in this repository
- [ ] 7.4 `(build)` Extend to the next tranche — Tcl, Expect, DejaGNU, Perl, the
      packages the build itself consumes — gating each removal by 7.1 and 7.2
      separately, because their `last_consumer` is the claim under test
- [ ] 7.5 Record every reversal: package, failing step, missing file, verdict before
      and after. A reversal with no failure recorded is a decision never made
- [ ] 7.6 Re-run the graph after every accepted or reversed verdict, and let 5.5
      refuse anything stale

## 8. Boundaries and the claims this change makes

- [ ] 8.1 `(build)` Give the README's "No bloat" claim a command: point it at the
      verdict file's counts and the trim phase's recorded output, so the badge
      resolves to a check instead of to an adjective
- [ ] 8.2 Tell `maintenance-and-updates` that a package which does not ship still has
      a security route: its ledger row SHALL say the package is not in the image, or
      SHALL stop watching it. A ledger watching an absent package reports peace
- [ ] 8.3 Tell `gpu-stack` that its 6.7 and this change share a verdict vocabulary
      and not a mechanism, and record which libraries the driver loads by name so
      0.4 stops being a question
- [ ] 8.4 Record in `docs/` what the image ships and why, as a pointer to
      `image-trim.toml` plus the reasons — not a copy of it, which would be the
      second list this project has already forbidden once
- [ ] 8.5 Record the editor decision where a reader will find it: Vim is the only
      file editor among the 89, so removing it is a support decision, and a smaller
      editor is an addition with its own `base.lock` entry
- [ ] 8.6 Record what this change does not do: no stripping (the edition's own
      pages), no package manager (`build-orchestration` D1), no accelerator-path
      removal, no figure before 7.3

## 9. Audit before this change is called done

- [ ] 9.1 Check every scenario in the three new requirements has a task and, where it
      is a behaviour, a test or a recorded run. Name each scenario and where it lands
- [ ] 9.2 Check the two failure modes this change exists to prevent are each covered
      by a failing test or a recorded build failure, not by prose: a removal that
      breaks the image, and a verdict file that quietly omits packages
- [ ] 9.3 Check that no task adds a package to `base.lock`, and that the only new
      host-side dependency this change introduces is `graphify`, already installed
      and recorded as a build-host tool by 5.7
- [ ] 9.4 Check that no document in this repository states a size saving, and that
      7.3 is the only task that could make one true
- [ ] 9.5 Check every `(build)` task's evidence is recorded output rather than a
      claim, and that the eleven markers match what 0.1 makes possible — a mislabelled
      task is worse than a blocked one because it looks schedulable
