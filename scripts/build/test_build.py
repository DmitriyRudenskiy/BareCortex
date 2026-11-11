#!/usr/bin/env python3
"""The two trust paths of the build, checked without a guest.

`fetch` decides what the build is made of and `guard_sources` decides what it may
reference. Both fail a build, so both are tested here rather than discovered by
a phase that got the wrong tarball. Run: `python3 scripts/build/test_build.py`.
"""

import json
import os
import pathlib
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import build  # noqa: E402


def check_mismatches():
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
    have = build.lock_files()
    assert len(have) == 93, f"base.lock carries 88 packages and 5 patches, got {len(have)}"
    assert "acl-2.3.2.tar.xz" in have
    assert "glibc-fhs-1.patch" in have


def check_sources_named():
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
    """Task 3.5: the chroot gets the book's six variables and nothing else."""
    book_vars, env_i, prelude = build.wrap_environment_delta()
    assert book_vars == {"HOME", "TERM", "PS1", "PATH", "MAKEFLAGS", "TESTSUITEFLAGS"}, \
        "the pinned page's invocation was read differently than the book prints it"
    assert env_i == book_vars, \
        f"env -i adds {sorted(env_i - book_vars)} drops {sorted(book_vars - env_i)}"
    assert prelude == {"LFS", "SRCDIR"}, \
        f"the page shell is handed {sorted(prelude)} beyond the book's environment"


def check_build_root_lock():
    """Task 4.6: one build per build root, and a dead owner is named as stale."""
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
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        "derive_expectations", str(build.ROOT / "scripts/build/derive-test-expectations.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    out = pathlib.Path(tempfile.mkdtemp()) / "test-expectations.toml"
    mod.OUT = out
    import io
    import contextlib
    with contextlib.redirect_stdout(io.StringIO()):
        mod.main()
    have = (build.ROOT / "test-expectations.toml").read_text()
    want = out.read_text()
    assert have == want, ("test-expectations.toml does not match the pinned pages; "
                          "run scripts/build/derive-test-expectations.py")


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
    """A resume runs the phase that failed, not the one after it (task 4.3)."""
    order = ["host-prep", "sources", "chroot-layout", "cross-toolchain", "temp-tools"]
    passed = [{"phase": p, "passed": True} for p in order[:3]]
    assert build.next_phase(passed + [{"phase": "cross-toolchain", "passed": False}], order) == "cross-toolchain"
    # A later phase passing does not make the failed one disappear.
    assert build.next_phase(passed + [{"phase": "temp-tools", "passed": True},
                                      {"phase": "cross-toolchain", "passed": False}], order) == "cross-toolchain"
    assert build.next_phase([{"phase": p, "passed": True} for p in order], order) is None
    assert build.next_phase([], order) == "host-prep"


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


def _alive(pid):
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


if __name__ == "__main__":
    for fn in (check_mismatches, check_lock_files, check_sources_named,
               check_record_identity_and_atomicity, check_chroot_environment_is_the_books,
               check_build_root_lock, check_test_expectations_are_current,
               check_core_suite_cannot_be_skipped,
               check_no_page_is_dropped_from_the_index,
               check_sources_needed_for_every_page,
               check_resume_starts_at_the_failed_phase,
               check_guard_on_the_real_pages):
        print(f"{fn.__name__} ...", flush=True)
        fn()
        print(f"  ok")
    print("all build-orchestration trust checks pass")
