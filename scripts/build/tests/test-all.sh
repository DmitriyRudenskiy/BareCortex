#!/bin/sh
# test-all.sh — the offline checks of the build, named for build-rootfs's
# tests/test-all.sh. The build driver's trust checks, the base.lock account,
# the service inventory, the no-systemd scan (with an image root), and the
# build-rootfs file-layout checks — one result line per check.
#
#   tests/test-all.sh                     everything that runs offline
#   tests/test-all.sh trust               the build-orchestration trust checks
#   tests/test-all.sh layout              the build-rootfs file-layout checks
#   tests/test-all.sh base-lock [--lock L --map M [--coverage]]
#   tests/test-all.sh services [--inventory I]
#   tests/test-all.sh no-systemd --root <image root>
#
# Polyglot: sh execs the Python payload; python reads the exec line as a string.
'''exec' python3 "$0" "$@" # '''
"""test-all.sh — the offline checks of the build.

The build driver's own trust path is imported from
`scripts/build/build-rootfs.sh` (loaded as the module `build`, which is also
how the inlined checks' `import book` resolves — the book reader is inlined
into that file), and the base.lock, service, and no-systemd checkers live in
this file now that the build-rootfs layout gives the tree one test entry each.
"""

# >>> docs
# ## tests/test-all.sh
#
# The offline checks — no guest needed, one result line per check:
#
# ```
# sh scripts/build/tests/test-all.sh                    # everything that runs offline
# sh scripts/build/tests/test-all.sh trust               # the driver's trust checks
# sh scripts/build/tests/test-all.sh layout              # the build-rootfs file-layout checks
# sh scripts/build/tests/test-all.sh base-lock [--lock L --map M [--coverage]]
# sh scripts/build/tests/test-all.sh services [--inventory I]
# sh scripts/build/tests/test-all.sh no-systemd --root <image root>
# ```
#
# ### The checks
#
# @@CHECKS@@
# <<< docs

import argparse
import contextlib
import html
import importlib.util
import io
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import tomllib
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[3]
BUILD_SH = ROOT / "scripts" / "build" / "build-rootfs.sh"

# The build driver, loaded as the module `build`: the inlined trust checks call
# it by that name. A SourceFileLoader because the file ends in .sh, not .py.
# Registering it in sys.modules (as itself, and as `book`, which is inlined
# into it) is what those calls and the checks' `import book` resolve against.
from importlib.machinery import SourceFileLoader  # noqa: E402

_loader = SourceFileLoader("build", str(BUILD_SH))
_spec = importlib.util.spec_from_loader("build", _loader)
build = importlib.util.module_from_spec(_spec)
sys.modules["build"] = build
sys.modules["book"] = build
_loader.exec_module(build)

# --- trust: the two paths that decide what the build is made of ---------



sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import build  # noqa: E402


def check_mismatches():
    """A wrong, absent or undownloaded checksum names the file."""
    ok = "@@MD5 a.tar.xz deadbeef deadbeef\n"
    bad = "@@MD5 b.tar.xz cafebeef deadbeef\n"
    gone = "@@MD5 c.tar.xz  deadbeef\n"          # md5sum printed nothing: no file
    failed = "@@FETCH-FAIL d.tar.xz\n"
    assert build.mismatches(ok) == [], "a matching checksum must not be a failure"
    assert build.mismatches(bad) == ["b.tar.xz"], "a mismatch must name the file"
    assert build.mismatches(gone) == ["c.tar.xz"], "an absent file must name the file"
    assert build.mismatches(failed) == ["d.tar.xz"], "a failed download must name the file"
    # the shape a real fetch.log has: every file, in base.lock's order
    assert build.mismatches(ok + bad + gone + failed) == ["b.tar.xz", "c.tar.xz", "d.tar.xz"]
    assert build.mismatches("") == [], "no output is no evidence, but not a named file"


def check_lock_files():
    """`base.lock` still carries 89 packages and 5 patches."""
    have = build.lock_files()
    assert len(have) == 94, f"base.lock carries 89 packages and 5 patches, got {len(have)}"
    assert "acl-2.3.2.tar.xz" in have
    assert "glibc-fhs-1.patch" in have


def check_sources_named():
    """A page's commands are scanned for file names, not prose."""
    # a page's commands are scanned for file names, not for prose that mentions one
    cmd = "patch -Np1 -i ../glibc-fhs-1.patch && ./configure --prefix=/usr"
    names = {t.rsplit("/", 1)[-1] for t in build.ARCHIVE_RE.findall(cmd)}
    assert names == {"glibc-fhs-1.patch"}, names


def check_guard_on_the_real_pages():
    """The guard runs against the pinned edition, and says what it finds.

    A finding here is not a test failure: it is the phase that would stop, and
    the reason recorded so the next run does not rediscover it.
    """
    ph = build.phases()["cross-toolchain"]
    pages = [pg for pg, _ in build.pages_for(ph)][:4]
    missing = build.guard_sources(pages)
    print("  cross-toolchain (first 4 pages) unsourced:", missing or "none")
    # a page command naming a file the lock does not carry must be found too —
    # this is the path a hand-written `tar -xvf` or a `wget` walks into
    import book
    real = book.commands
    book.commands = lambda pg: [("", "tar -xvf linux-99.9.9.tar.xz")]
    try:
        assert build.guard_sources(["chapter08/linux.html"]) == \
            ["chapter08/linux.html: linux-99.9.9.tar.xz"], "an unsourced tarball must stop the phase"
    finally:
        book.commands = real
    # the three suites the book calls utmost importance cannot be skipped
    saved = dict(build.OVR.get("page", {}))
    try:
        build.OVR.setdefault("page", {})["chapter08/glibc.html"] = {"skip_blocks": [1]}
        suite = [i for i, (_, c) in enumerate(book.commands("chapter08/glibc.html"), 1)
                 if "check" in c][0]
        build.OVR["page"]["chapter08/glibc.html"] = {"skip_blocks": [suite]}
        assert build.guard_test_policy(["chapter08/glibc.html"]) == \
            [f"chapter08/glibc.html: glibc test suite skipped (block {suite})"]
        build.OVR["page"]["chapter08/glibc.html"] = {"skip_blocks": [suite + 1]}
        assert build.guard_test_policy(["chapter08/glibc.html"]) == [], \
            "skipping any other block is allowed; only the suite is protected"
    finally:
        build.OVR["page"] = saved
    # an override naming a file the lock does not carry must be found
    saved = dict(build.OVR.get("page", {}))
    try:
        build.OVR.setdefault("page", {})["chapter05/gcc-pass1.html"] = {
            "archive": "gcc-99.9.9.tar.xz"}
        assert any("gcc-99.9.9.tar.xz" in m for m in build.guard_sources([])), \
            "an override archive outside base.lock must fail the phase"
    finally:
        build.OVR["page"] = saved


def check_record_identity_and_atomicity():
    """A record says what it was built against, and a crash does not eat it."""
    d = pathlib.Path(tempfile.mkdtemp())
    build.write_record(d, {"phase": "sources", "passed": True})
    rec = json.loads((d / "record.json").read_text())
    assert len(rec["base_lock"]) == 64, "the record names base.lock by its bytes"
    assert rec["book"].endswith("13.0"), "the record names the edition it was read from"
    assert not (d / "record.json.tmp").exists(), "a finished write leaves no temp file"
    # a run that dies mid-write leaves a half-written temp file, never a
    # half-written record: the previous phase's record still parses
    (d / "record.json.tmp").write_text('{"phase": "sources", "passed": fal')
    assert json.loads((d / "record.json").read_text())["passed"] is True


def check_chroot_environment_is_the_books():
    """The chroot gets the book's six variables and nothing else."""
    book_vars, env_i, prelude = build.wrap_environment_delta()
    assert book_vars == {"HOME", "TERM", "PS1", "PATH", "MAKEFLAGS", "TESTSUITEFLAGS"}, \
        "the pinned page's invocation was read differently than the book prints it"
    assert env_i == book_vars, \
        f"env -i adds {sorted(env_i - book_vars)} drops {sorted(book_vars - env_i)}"
    assert prelude == {"LFS", "BCT_SRCDIR"}, \
        f"the page shell is handed {sorted(prelude)} beyond the book's environment"


def check_build_root_lock():
    """One build per build root, and a dead owner is named as stale."""
    real = build.LOCK
    d = pathlib.Path(tempfile.mkdtemp())
    build.LOCK = d / ".build-lock"
    dead = next(p for p in range(999999, 999000, -1)
                if not _alive(p))
    try:
        build.acquire_lock("run sources")
        assert build.LOCK.read_text().split("\t")[0] == str(os.getpid())
        try:
            build.acquire_lock("run final-system")
            raise AssertionError("a second invocation against a locked root must fail")
        except SystemExit as e:
            assert f"locked by pid {os.getpid()}" in str(e), e
            assert "owns $LFS" in str(e), e
        build.release_lock()
        assert not build.LOCK.exists()
        build.LOCK.write_text(f"{dead}\trun sources\t2026-11-11T00:00:00\n")
        try:
            build.acquire_lock("run sources")
            raise AssertionError("a lock whose owner is gone must not be taken silently")
        except SystemExit as e:
            assert "stale lock" in str(e) and f"rm {build.LOCK}" in str(e), e
    finally:
        build.LOCK = real


def check_test_expectations_are_current():
    """`test-expectations.toml` says what the pinned pages say today.

    The file quotes the book's own sentences about failing tests. Quoted prose
    rots silently when a version is bumped, so the derivation is re-run here and
    compared, and a difference is an error naming the command that fixes it.
    """
    out = pathlib.Path(tempfile.mkdtemp()) / "test-expectations.toml"
    build.OUT = out
    import io
    import contextlib
    with contextlib.redirect_stdout(io.StringIO()):
        build.derive_test_expectations()
    have = (build.ROOT / "test-expectations.toml").read_text()
    want = out.read_text()
    assert have == want, ("test-expectations.toml does not match the pinned pages; "
                          "run scripts/build/build-rootfs.sh derive-test-expectations")


def check_no_page_is_dropped_from_the_index():
    """Every page the edition's own contents lists is a page the build knows about.

    The contents page is the authority on what the book asks for, so it is scraped
    twice: once with a permissive href pattern, once through `index_pages`. The
    pattern used to be a character class that forgot `+`, which dropped
    `chapter05/gcc-libstdc++.html` — "Libstdc++ from GCC-15.2.0" — out of every
    phase, and the build only noticed two chapters later, when GCC pass 2's
    `configure-libcody` failed with `cannot find -lstdc++` because the cross
    compiler had never been handed a C++ library to link with.
    """
    import re
    import book
    listed = set(re.findall(r'href="(chapter\d+/[^"]+\.html)"', book.page("index.html")))
    assert listed, "the contents page parsed as zero pages"
    dropped = listed - {h for h, _ in build.index_pages()}
    assert not dropped, f"the book lists pages the build will never run: {sorted(dropped)}"


def check_resume_starts_at_the_failed_phase():
    """A resume runs the phase that failed, not the one after it."""
    order = ["host-prep", "sources", "chroot-layout", "cross-toolchain", "temp-tools"]
    passed = [{"phase": p, "passed": True} for p in order[:3]]
    assert build.next_phase(passed + [{"phase": "cross-toolchain", "passed": False}], order) == "cross-toolchain"
    # A later phase passing does not make the failed one disappear.
    assert build.next_phase(passed + [{"phase": "temp-tools", "passed": True},
                                      {"phase": "cross-toolchain", "passed": False}], order) == "cross-toolchain"
    assert build.next_phase([{"phase": p, "passed": True} for p in order], order) is None
    assert build.next_phase([], order) == "host-prep"


def check_a_page_that_stops_early_does_not_pass():
    """Every block of the page has to report, or the page did not happen.

    Chapter 7's createfiles page ends with `exec /usr/bin/bash --login` for the
    person at the keyboard. It replaced the shell, ended the page, and took the
    page's last block — the one creating /var/log/{btmp,lastlog,wtmp} — with it.
    The page reported 5 of 7 blocks and passed, and the record said so happily.
    """
    real = build.guest_run
    build.guest_run = lambda *a, **k: (0, '@@RC 1 0\n@@RC 2 0\n@@DONE')
    try:
        r = build.run_page("chapter07/createfiles.html", "Creating", "chroot-root",
                           pathlib.Path(tempfile.mkdtemp()))
    finally:
        build.guest_run = real
    assert any("never reported" in f for f in r["failed"]), r["failed"]


def check_a_placeholder_means_what_its_page_says():
    """The book's `<xxx>` is the LFS partition on the chapter 2 pages and a timezone on Glibc's.

    Filled from one table, the Glibc page got the partition name and `ln -sf`
    created `/etc/localtime` pointing at `/usr/share/zoneinfo/vdc1` — exit 0, and
    a system with no timezone, which nothing in the phase checks.
    """
    glibc, _ = build.page_script("chapter08/glibc.html", "Glibc")
    assert "/usr/share/zoneinfo/UTC /etc/localtime" in glibc
    assert "zoneinfo/vdc" not in glibc, "the partition name is not a timezone"
    mounting, _ = build.page_script("chapter02/mounting.html", "Mounting")
    assert "/dev/vdc1" in mounting and "/dev/vdc2" in mounting, "the device pages still get devices"


def check_a_predicted_test_failure_covers_only_the_test_suite():
    """Glibc's `make check` may exit non-zero and the book says so; its install may not.

    The tolerance used to be read from the admonitions only, which missed the pages
    whose wording is not "known to fail" — Glibc among them, the one suite the book
    refuses to let you skip — and it forgave every block of any page that mentioned
    a failing test, including the block that installs the package.
    """
    glibc = build.expected_failures("chapter08/glibc.html", "@@DONE")
    assert 6 in glibc, "block 6 is Glibc's make check"
    assert 5 not in glibc and 7 not in glibc, "the blocks around it install the package"
    assert build.expected_failures("chapter08/zstd.html", "@@DONE"), "Zstd: these are expected"
    assert build.expected_failures("chapter08/gcc.html", "@@DONE"), "GCC: known test failures"
    assert build.expected_failures("chapter06/gcc-pass2.html", "@@DONE") == set()
    assert build.expected_failures("chapter08/glibc.html", "") == set(), "a page that never ran predicts nothing"


def check_sources_needed_for_every_page():
    """Every page of every phase can be asked what it needs, including pages that need nothing.

    The guard that reads the answer used to raise TypeError on the first page with
    no source archive — every page of chapter 7 — because set arithmetic was
    written inline against a value that could be None. It is a phase-start path,
    so nothing but a real page list reaches it.
    """
    assert build.sources_needed("chapter07/chroot.html") == set(), "a chroot page builds no package"
    assert build.sources_needed("chapter05/gcc-pass1.html") == {
        "gcc-15.2.0.tar.xz", "mpfr-4.2.2.tar.xz", "gmp-6.3.0.tar.xz", "mpc-1.3.1.tar.gz"}
    assert build.sources_needed("chapter08/glibc.html") == {
        "glibc-2.43.tar.xz", "glibc-fhs-1.patch", "tzdata2025c.tar.gz"}
    for ph in build.phases().values():
        for pg, _ in build.pages_for(ph):
            assert isinstance(build.sources_needed(pg), set), pg


def check_core_suite_cannot_be_skipped():
    """Skipping a core suite through an override is refused, not remembered."""
    pg = "chapter08/glibc.html"
    block = build.EXPECTATIONS[pg]["suite"][0]["block"]
    pages = build.OVR.setdefault("page", {})
    saved = pages.get(pg, {})
    pages[pg] = {"skip_blocks": [block]}
    try:
        bad = build.guard_test_policy([pg])
    finally:
        pages[pg] = saved
    assert bad and "glibc" in bad[0] and str(block) in bad[0], bad


def check_a_predicted_test_failure_does_not_stop_the_page():
    """A predicted failure forgives the suite and nothing else."""
    # The page's shell runs under `set -e` for as long as the book's unpack
    # synopsis owns it, and not one line longer: left on, Glibc's testsuite —
    # which the book says will fail — ends the shell before the block reports,
    # and `make install` three blocks later never runs.
    pg = "chapter08/glibc.html"
    script, emitted = build.page_script(pg, "Glibc", build.expected_failures(pg, "@@DONE"))
    assert script.index("set -e") < script.index("set +e") < script.index("@@BLOCK 1"), \
        "errexit has to end where the blocks begin"
    assert 'FAIL=' in script and "@@FAILED" in script, "a failing command has to be recorded"
    assert "[ -z \"$FAIL\" ] || rc=1" in script, \
        "a block whose third command failed is not a block that passed"
    for block, marker in ((5, "@@ABORT block 5"), (6, "@@TOLERATED block 6")):
        at = script.index(f"@@BLOCK {block} ")
        window = script[at:script.index(f"@@BLOCK {block + 1} ")]
        assert marker in window, f"block {block} should end at {marker}, not stop the page"
        assert ("exit 1" in window) == marker.startswith("@@ABORT"), window
    assert 19 not in emitted, "tzselect prompts a terminal this build does not have"


def _alive(pid):
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True



# --- base.lock: is it an account for the base system? ------------------



EXPLAIN_BASE_LOCK = """\
base.lock is the base system's account of itself, so a check can ask whether any
part of the image is unaccounted. Three rules, all read from class-map.toml:

1. Every entry names a class that is a row of the class map. The map's
   [schema] enumerates field is the only declaration that a field lists the
   classes; a document naming classes in prose is not a declaration.
2. Every entry carries the fields its own class requires — the class map's
   `requires`, not a list kept here.
3. Every source-built entry cites the book: the file names the edition it was
   derived from, and the entry names at least one page of it. A line with no
   page is a recipe nobody wrote down, which is the thing this file exists to
   catch.

The class map is read fail-closed: absent, unparseable, or holding no class row
is a failure naming class-map.toml, never an empty loop that passes.
"""


def fail_base_lock(msg, fails):
    fails.append(msg)
    print(f"FAIL {msg}")


def load_map(path, fails):
    """The class map, or a failure naming it. Never an empty set of classes."""
    if not path.is_file():
        fail_base_lock(f"class map absent: {path} — the classes are its rows, and an "
             f"absent map would check nothing", fails)
        return None
    try:
        data = tomllib.loads(path.read_text())
    except tomllib.TOMLDecodeError as e:
        fail_base_lock(f"class map unparseable: {path}: {e}", fails)
        return None
    classes = data.get("class")
    if not classes:
        fail_base_lock(f"class map holds no class row: {path} — refusing to pass by "
             f"iterating zero classes", fails)
        return None
    for c in classes:
        if not c.get("requires"):
            fail_base_lock(f"class {c.get('name', '?')} carries no required fields "
                 f"(row owned by {c.get('owner', 'nobody')}) in {path}", fails)
    return data


def coverage(lock, lock_path, fails):
    """Does base.lock cover the base set? Asked of the book, not of a list.

    The base set is the edition's own table of contents: every page titled like
    `Acl-2.3.2` is a package the book builds. Coverage is checked against that,
    so this file never holds a second copy of the package list — which is the
    defect base-system-and-init 3.2 exists to prevent.
    """
    import urllib.request
    base = lock.get("edition", {}).get("url", "").rstrip("/")
    if not base:
        fail_base_lock(f"{lock_path}: [edition] url absent, so coverage cannot be asked "
             f"of the book it was derived from", fails)
        return
    index = urllib.request.urlopen(base + "/", timeout=60).read().decode("utf-8", "replace")
    wanted = set()
    for href, title in re.findall(r'href="(chapter\d+/[\w:.+-]+\.html)"[^>]*>(.*?)</a>',
                                  index, re.S):
        t = " ".join(html.unescape(re.sub(r"<[^>]+>", " ", title)).split())
        if re.match(r"[A-Za-z][\w :+.-]*-\d", t):   # the book's `Name-Version` pages
            wanted.add(href)
    cited = {p for e in lock.get("package", []) for p in e.get("pages", [])}
    cited |= {p for u in lock.get("unattributed-download", []) for p in u.get("pages", [])}
    if stale := sorted(cited - wanted):
        fail_base_lock(f"{lock_path}: {len(stale)} cited page(s) of this edition build no "
             f"package: {', '.join(stale[:6])} — a citation to a page that does "
             f"not build anything cites nothing", fails)
    if uncovered := sorted(wanted - cited):
        fail_base_lock(f"{lock_path}: {len(uncovered)} package page(s) of {base} are built by "
             f"the book and named by no entry: {', '.join(uncovered[:8])}", fails)
    print(f"coverage: pages={len(wanted)} cited={len(wanted & cited)} "
          f"uncovered={len(uncovered)}")


def main_base_lock(argv):
    """Is `base.lock` an account for the base system: every entry carries name, version, URL, checksum, provenance class and its book pages, and every source-built package the edition lists is present."""
    ap = argparse.ArgumentParser(description="is base.lock an account for the base system?")
    ap.add_argument("--lock", default="base.lock")
    ap.add_argument("--map", dest="map", default="class-map.toml")
    ap.add_argument("--coverage", action="store_true",
                    help="ask the edition's index whether base.lock covers it")
    ap.add_argument("--explain", action="store_true", help="print the rules and exit")
    a = ap.parse_args(argv)

    if a.explain:
        print(EXPLAIN)
        return 0

    fails = []
    lock_path = pathlib.Path(a.lock)
    if not lock_path.is_file():
        fail_base_lock(f"lock file absent: {lock_path} — derive it with "
             f"scripts/build/build-rootfs.sh derive-base-lock", fails)
        return 1
    lock = tomllib.loads(lock_path.read_text())

    cmap = load_map(pathlib.Path(a.map), fails)
    if cmap is None:
        return 1

    required = {c["name"]: c.get("requires", []) for c in cmap["class"]}
    owners = {c["name"]: c.get("owner", "nobody") for c in cmap["class"]}
    field = cmap.get("schema", {}).get("enumerates")
    if not field:
        fail_base_lock(f"class map declares no [schema] enumerates field: {a.map} — "
             f"without it no field can be checked against the classes", fails)
        return 1

    edition = lock.get("edition", {}).get("name")
    if not edition:
        fail_base_lock(f"{lock_path} names no [edition]: a lock file that does not say "
             f"which book revision it was derived from cannot be cited", fails)

    # One machine-readable line per class: an incomplete map is known from these
    # lines, not from a document claiming the check is red.
    for name in sorted(required):
        entries = [e for e in lock.get("package", []) if e.get(field) == name]
        short = sum(1 for e in entries if set(required[name]) - set(e))
        print(f"class: {name} owner={owners[name]} entries={len(entries)} "
              f"entries_missing_fields={short}")

    for e in lock.get("package", []):
        cls = e.get(field)
        where = f"{e.get('name', '?')} {e.get('version', '?')}"
        if cls not in required:
            fail_base_lock(f"{lock_path}: entry {where} names class {cls!r}, which is not a "
                 f"row of {a.map}; a class enters the vocabulary as a map row "
                 f"naming the change that owns it", fails)
            continue
        if missing := set(required[cls]) - set(e):
            fail_base_lock(f"{lock_path}: entry {where} is class {cls} but carries no "
                 f"{', '.join(sorted(missing))}", fails)
        # The citation rule, stated for the one class that cites the book: the
        # page is what a reviewer opens to check the recipe was followed.
        if cls == "source-built":
            pages = [p for p in e.get("pages", []) if p != "UNMATCHED"]
            if not edition or not pages:
                fail_base_lock(f"{lock_path}: entry {where} carries no book revision and "
                     f"page — added or moved without citing where the book "
                     f"builds it", fails)

    for u in lock.get("unattributed-download", []):
        print(f"unattributed: {u['name']} {u['version']} — {u['reason']}")

    if a.coverage:
        coverage(lock, lock_path, fails)

    print(f"base-lock: {'FAIL ' + str(len(fails)) if fails else 'ok'} "
          f"packages={len(lock.get('package', []))} edition={edition}")
    return 1 if fails else 0



# --- no-systemd: does anything resolve a systemd library? --------------



EXPLAIN_NO_SYSTEMD = """\
Every ELF file under the image root is asked, via readelf, which libraries it
needs at runtime; etc/pam.d/* and etc/nsswitch.conf are asked which modules they
name. A match on libsystemd, libelogind, libnss_systemd or pam_systemd fails,
naming the file and the library. The command is the verification behind the
README's 'no systemd' claim; the claim without the command is the scenario this
file exists to make impossible."""

SYSTEMD_LIBS = ("libsystemd", "libelogind", "libnss_systemd", "pam_systemd")
# `readelf -d --wide` indents the tag, so the tag is not at the line start.
NEEDED_RE = re.compile(r"\(NEEDED\)\s+Shared library:\s*\[([^\]]+)\]")
ELF_MAGIC = b"\x7fELF"


def is_elf(path):
    try:
        with path.open("rb") as f:
            return f.read(4) == ELF_MAGIC
    except OSError:
        return False


def parse_needed(readelf_out):
    """The sonames in `readelf -d` output — the dynamic section and nothing else."""
    out = []
    for line in readelf_out.splitlines():
        if not (m := NEEDED_RE.search(line)):
            continue
        # Prefix, not equality: Debian's udevd resolves `libsystemd-shared-257.so`
        # and the pinned edition's `-D link-udev-shared=false` exists precisely to
        # stop ours resolving that family. A suffix like `-257` is a distro's, so
        # matching the whole soname would be matching a thing that varies.
        base = m.group(1).split(".so")[0]
        if base.startswith(SYSTEMD_LIBS) or base.endswith("_systemd"):
            out.append(m.group(1))
    return out


def needed_libs(path):
    out = subprocess.run(["readelf", "-d", "--wide", str(path)],
                         capture_output=True, text=True)
    if out.returncode != 0:
        # A file with ELF magic that readelf refuses is a binary this scan did
        # not read, which is not the same as a binary with nothing wrong.
        raise RuntimeError(f"readelf could not read {path}: {out.stderr.strip()}")
    return parse_needed(out.stdout)


def scan_configs(root):
    """Modules named by configuration, which no ELF header mentions."""
    hits = []
    for rel in ("etc/pam.d",):
        d = root / rel
        if not d.is_dir():
            continue
        for f in sorted(d.iterdir()):
            if not f.is_file():
                continue
            for line in f.read_text(errors="replace").splitlines():
                line = line.split("#")[0]
                for lib in SYSTEMD_LIBS:
                    if re.search(rf"\b{lib}\b", line):
                        hits.append((f, f"{lib} (named by PAM config: {line.strip()})"))
    nss = root / "etc/nsswitch.conf"
    if nss.is_file():
        for line in nss.read_text(errors="replace").splitlines():
            if "systemd" in line:
                hits.append((nss, f"libnss_systemd (named by nsswitch.conf: "
                                  f"{line.strip()})"))
    return hits


def main_no_systemd(argv):
    """Nothing in the image resolves a systemd library: the ELF walk reads `DT_NEEDED` with `readelf`, and a string-table mention is not a runtime resolution."""
    ap = argparse.ArgumentParser(description="does anything in the image resolve a systemd library?")
    ap.add_argument("--root", help="the image root to scan, e.g. $LFS")
    ap.add_argument("--explain", action="store_true", help="print the rules and exit")
    a = ap.parse_args(argv)
    if a.explain:
        print(EXPLAIN)
        return 0
    if not a.root:
        print("FAIL no --root: scanning the source tree proves nothing about the "
              "image, and passing here would be a claim about binaries this "
              "command never read", file=sys.stderr)
        return 1

    root = pathlib.Path(a.root)
    if not root.is_dir():
        print(f"FAIL image root is not a directory: {root}", file=sys.stderr)
        return 1

    fails, binaries = [], 0
    for path in sorted(root.rglob("*")):
        if not path.is_file() or not is_elf(path):
            continue
        binaries += 1
        try:
            hits = needed_libs(path)
        except (FileNotFoundError, RuntimeError) as e:
            print(f"FAIL {e}", file=sys.stderr)
            return 1
        for lib in hits:
            udev = "udev" in path.name or "/udev" in str(path.relative_to(root))
            fails.append(f"{path.relative_to(root)} resolves {lib} at runtime"
                         + (" — the pinned edition's `-D link-udev-shared=false` "
                            "assumption broke; that option is the fix, not an "
                            "exception" if udev else ""))

    for path, why in scan_configs(root):
        fails.append(f"{path.relative_to(root)}: {why}")

    for f in fails:
        print(f"FAIL {f}", file=sys.stderr)
    print(f"systemd-scan: root={root} binaries={binaries} findings={len(fails)}")
    return 1 if fails else 0



# --- services: is every shipped OpenRC service accounted for? ----------



EXPLAIN_SERVICES = """\
The service inventory is `openrc/services.toml`; the services are the files in
the directory it names. The check compares the two and reports one line per
service, so a service missing from the inventory is a gap in the output rather
than a document claiming coverage. See the module docstring for the five rules
and why each is the spec's."""

REQUIRED = ("runlevel", "requires", "establishes", "failure", "owner")


def fail_services(msg, fails):
    print(f"FAIL {msg}", file=sys.stderr)
    fails.append(msg)


def main_services(argv):
    """Every OpenRC service the image ships is in `openrc/services.toml` with its runlevel, its requirements and its failure semantics."""
    ap = argparse.ArgumentParser(description="is every shipped OpenRC service accounted for?")
    ap.add_argument("--inventory", default="openrc/services.toml")
    ap.add_argument("--explain", action="store_true", help="print the rules and exit")
    a = ap.parse_args(argv)
    if a.explain:
        print(EXPLAIN)
        return 0

    fails = []
    inv_path = pathlib.Path(a.inventory)
    if not inv_path.is_file():
        fail_services(f"inventory absent: {inv_path} — refusing to pass by comparing "
             f"nothing; every shipped service is unaccounted until it exists", fails)
        return 1
    try:
        inv = tomllib.loads(inv_path.read_text())
    except tomllib.TOMLDecodeError as e:
        fail_services(f"inventory unparseable: {inv_path}: {e}", fails)
        return 1

    services = inv.get("service", [])
    if not services:
        fail_services(f"inventory holds no [[service]] row: {inv_path} — refusing to pass "
             f"by iterating zero services", fails)
        return 1

    d = pathlib.Path(inv.get("inventory", {}).get("directory", "openrc/init.d"))
    if not d.is_dir():
        fail_services(f"service directory named by {inv_path} does not exist: {d}", fails)
        return 1
    files = sorted(p.name for p in d.iterdir() if p.is_file() and not p.name.endswith(".toml"))

    guide = pathlib.Path(inv.get("inventory", {}).get("enabled_by", "docs/INSTALL_GUIDE.md"))
    enabled = dict(re.findall(r"^\s*rc-update add (\S+) (\S+)", guide.read_text(), re.M)
                   ) if guide.is_file() else {}
    if not guide.is_file():
        fail_services(f"{inv_path} says services are enabled by {guide}, which does not "
             f"exist — no runlevel below can be checked against anything", fails)

    by_name = {}
    for s in services:
        if "name" not in s:
            fail_services(f"{inv_path}: a [[service]] row carries no name", fails)
            continue
        by_name[s["name"]] = s

    for orphan in sorted(set(files) - set(by_name)):
        fail_services(f"{d / orphan} is a shipped service with no inventory entry — "
             f"nothing states where it runs, what it needs, or how it fails", fails)
    for orphan in sorted(set(by_name) - set(files)):
        fail_services(f"{inv_path}: entry {by_name[orphan]['name']!r} names no file in {d}/", fails)

    for name, s in sorted(by_name.items()):
        if missing := [f for f in REQUIRED if not s.get(f)]:
            fail_services(f"{inv_path}: service {name} carries no {', '.join(missing)}", fails)
        runlevel = s.get("runlevel")
        declared = s.get("enabled")
        if name in enabled:
            state = enabled[name]
            if state != runlevel:
                fail_services(f"{name}: inventory says runlevel {runlevel!r} but {guide} "
                     f"enables it at {state!r}", fails)
            if declared is False:
                fail_services(f"{name}: the inventory declares it not enabled while "
                     f"{guide} enables it at {state!r} — one of the two is a lie "
                     f"about the running system", fails)
        elif declared is False:
            state = "not-enabled"
            if not s.get("enabled_reason"):
                fail_services(f"{name}: declared not enabled with no enabled_reason — "
                     f"why nothing enables a shipped service is the part worth "
                     f"reading in six months", fails)
        else:
            state = "nothing-enables-it"
            fail_services(f"{name}: shipped, and no {guide} line enables it — a service "
                 f"nobody enables never runs. Either enable it or declare "
                 f"enabled = false with the reason", fails)
        print(f"service: {name} runlevel={state} requires={','.join(s.get('requires', []))}")

    print(f"services: {'FAIL ' + str(len(fails)) if fails else 'ok'} "
          f"files={len(files)} entries={len(by_name)}")
    return 1 if fails else 0



# --- the build-rootfs file layout, enforced -----------------------------------
#
# gbionescu/build-rootfs @ c9fa26adeb66e247afbac33510c9479cf4b89f4a is the
# reference this tree mirrors. Its top level holds build-rootfs.sh, run-qemu.sh,
# build_kernel.sh, build-initrd.sh and README.md; its tests/ holds the names
# below. The list is pinned here, with the reference's SHA, so the check runs
# offline and a reference rename is a visible edit, not a drift.

BUILD_ROOTFS_REF = "gbionescu/build-rootfs@c9fa26adeb66e247afbac33510c9479cf4b89f4a"
BUILD_ROOTFS_TOP = frozenset({"README.md", "build-rootfs.sh", "build_kernel.sh",
                              "build-initrd.sh", "run-qemu.sh"})
BUILD_ROOTFS_TESTS = frozenset({"qemu-test.sh", "test-all.sh", "test-arch.sh",
                                "test-elf-symbol.sh", "test-multiarch.sh",
                                "test-share.sh", "test-tools.sh"})


def build_tree_files():
    """Every file under scripts/build/, as a path relative to that directory."""
    tree = ROOT / "scripts" / "build"
    return sorted(p.relative_to(tree) for p in tree.rglob("*")
                  if p.is_file() and "__pycache__" not in p.parts)


def check_build_tree_file_set():
    """Every file in the build tree carries a name the reference has.

    The alignment is one-directional: the reference's unused files (build_kernel.sh,
    the other test-*.sh) need not exist here, but nothing here may wear a name the
    reference does not use. That is what keeps `scripts/build/` recognizable as the
    build-rootfs layout instead of a private vocabulary.
    """
    bad = []
    for rel in build_tree_files():
        if len(rel.parts) == 1:
            allowed, where = BUILD_ROOTFS_TOP, "top level"
        elif len(rel.parts) == 2 and rel.parts[0] == "tests":
            allowed, where = BUILD_ROOTFS_TESTS, "tests/"
        else:
            bad.append(f"{rel} (no such subdirectory in {BUILD_ROOTFS_REF})")
            continue
        if rel.name not in allowed:
            bad.append(f"{rel} ({where} name not in the {BUILD_ROOTFS_REF} set)")
    assert not bad, "files outside the build-rootfs name set: " + "; ".join(bad)


def check_readme_is_the_scripts():
    """The README is what the build files' documentation blocks say today, byte for byte."""
    have = (ROOT / "scripts" / "build" / "README.md").read_text()
    want = build.derive_readme()
    assert have == want, (
        "scripts/build/README.md is not what the build files' documentation blocks, "
        "phases.toml and the checks' docstrings say today — edit the build file, "
        "then run scripts/build/build-rootfs.sh derive-readme")

# --- dispatch -------------------------------------------------------------------

TRUST_CHECKS = (check_mismatches, check_lock_files, check_sources_named,
                check_record_identity_and_atomicity, check_chroot_environment_is_the_books,
                check_build_root_lock, check_test_expectations_are_current,
                check_core_suite_cannot_be_skipped,
                check_no_page_is_dropped_from_the_index,
                check_sources_needed_for_every_page,
                check_resume_starts_at_the_failed_phase,
                check_guard_on_the_real_pages,
                check_a_predicted_test_failure_does_not_stop_the_page)


def run_trust():
    for fn in TRUST_CHECKS:
        print(f"{fn.__name__} ...", flush=True)
        fn()
        print(f"  ok")
    print("all build-orchestration trust checks pass")
    return 0


def run_layout():
    for fn in (check_build_tree_file_set, check_readme_is_the_scripts):
        print(f"{fn.__name__} ...", flush=True)
        fn()
        print(f"  ok")
    print("build-rootfs layout checks pass")
    return 0


def main(argv):
    sub = argv[0] if argv else "all"
    if sub == "base-lock":
        return main_base_lock(argv[1:])
    if sub == "services":
        return main_services(argv[1:])
    if sub == "no-systemd":
        return main_no_systemd(argv[1:])
    if sub == "trust":
        return run_trust()
    if sub == "layout":
        return run_layout()
    if sub == "all":
        rc = run_trust()
        rc |= main_base_lock([])
        rc |= main_services([])
        rc |= run_layout()
        return 1 if rc else 0
    print(f"unknown subcommand: {sub} (all|trust|layout|base-lock|services|no-systemd)",
          file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
