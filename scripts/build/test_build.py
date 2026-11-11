#!/usr/bin/env python3
"""The two trust paths of the build, checked without a guest.

`fetch` decides what the build is made of and `guard_sources` decides what it may
reference. Both fail a build, so both are tested here rather than discovered by
a phase that got the wrong tarball. Run: `python3 scripts/build/test_build.py`.
"""

import json
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


if __name__ == "__main__":
    for fn in (check_mismatches, check_lock_files, check_sources_named,
               check_record_identity_and_atomicity,
               check_guard_on_the_real_pages):
        print(f"{fn.__name__} ...", flush=True)
        fn()
        print(f"  ok")
    print("all build-orchestration trust checks pass")
