#!/usr/bin/env python3
"""build.py — run the pinned book against the disposable host, and keep receipts.

  build.py pages <phase>              the pages a phase runs, from the book's index
  build.py run <phase> [--page P]     run a phase (or one page of it) in the guest
  build.py fetch                       download and verify base.lock's sources
  build.py status                      the phase records, as written
  build.py log <phase>                append the phase to docs/BUILD-LOG.md

Every command executed comes from `book.py` — that is, from a page of the pinned
edition — except where `build-overrides.toml` says otherwise, and each such
exception is written into the log with the reason the override gives. The device
placeholders the book leaves as `<xxx>` are filled from one place, `devices` in
that file, so the machine this build runs on is a fact in one file and not in
eight pages of shell.

Evidence lands in `build-evidence/<phase>/`: one log per page containing each
block's introducing sentence, its text, and its exit code, plus `record.json`.
A phase is `passed` only when every block's exit code was evaluated; a nonzero
code is a failure unless the page's own text expected it.
"""

import argparse
import datetime
import hashlib
import json
import os
import pathlib
import posixpath
import re
import shlex
import subprocess
import sys
import threading
import time
import tomllib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import book  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
EVIDENCE = ROOT / "build-evidence"
LOGMD = ROOT / "docs" / "BUILD-LOG.md"
OVERRIDES = ROOT / "build-overrides.toml"

# `ServerAliveInterval` because a chapter 8 page is silent for hours while Glibc
# compiles, and an idle timeout on the way to the guest would otherwise be
# reported as a failed package.
GUEST = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=25",
         "-o", "ServerAliveInterval=15", "-o", "ServerAliveCountMax=4", "-J",
         "user@192.168.0.86", "-p", "2222", "bct@127.0.0.1"]
CHROOT_ENV = ("env -i HOME=/root TERM={term} "
              "PS1='(lfs chroot) \\u:\\w\\$ ' PATH=/usr/bin:/usr/sbin "
              'MAKEFLAGS="-j$(nproc)" TESTSUITEFLAGS="-j$(nproc)"')


def load_toml(p):
    return tomllib.loads(p.read_text()) if p.is_file() else {}


PHASES = load_toml(ROOT / "phases.toml")
OVR = load_toml(OVERRIDES)
DEV = OVR.get("devices", {})
LFS = DEV.get("LFS", "/mnt/lfs")


def phases():
    return {p["name"]: {**PHASES.get("defaults", {}), **p} for p in PHASES["phase"]}


def index_pages():
    """Every package page of the edition, in the book's own order."""
    import html as H
    import re as RE
    idx = book.page("index.html")
    out = []
    # `[^"]+` and not a character class: the edition has a page named
    # `gcc-libstdc++.html`, and a class that forgot `+` dropped "Libstdc++ from
    # GCC" from every phase, which surfaced two chapters later as GCC pass 2's
    # `configure-libcody` failing with `cannot find -lstdc++` — the cross
    # compiler had never been given a C++ library to link with.
    # `check_no_page_is_dropped_from_the_index` keeps that from happening again.
    for href, title in RE.findall(r'href="(chapter\d+/[^"]+\.html)"[^>]*>(.*?)</a>',
                                  idx, re.S):
        out.append((href, " ".join(H.unescape(re.sub(r"<[^>]*>", " ", title)).split())))
    return out


def pages_for(ph):
    want = ph["chapter"]
    only, skip = set(ph.get("only", [])), set(ph.get("skip", []))
    out = []
    for href, title in index_pages():
        if href.split("/")[0] != f"chapter{want}":
            continue
        slug = href.split("/")[1][:-5]
        if slug == "introduction" or slug in skip:
            continue
        if only and slug not in only:
            continue
        out.append((href, title))
    # A page the index forgot is still a page of the chapter. `extra` puts one
    # back where the book's own Next links say it belongs; `guard_chapter_complete`
    # is what notices it is needed.
    for e in ph.get("extra", []):
        at = next((i for i, (h, _) in enumerate(out) if h == e.get("after")), len(out))
        out.insert(at + 1, (e["page"], e.get("title", e["page"].split("/")[-1][:-5])))
    return out


# --- running things in the guest --------------------------------------------------


def guest_run(script, timeout=None, tries=2, done=None, restart=None, poll=90):
    """Three hours for one page, and a page that says it finished is not re-run.

    Retried once when the *transport* died rather than the build: this build's
    Glibc page ran for 45 minutes, the ssh session between this machine and the
    guest went away half-open, the local `ssh` sat waiting out its keepalive
    timeout, and the phase would have been recorded as a failed package on a
    system that had no way to know whether Glibc built. `rc 255` with no `@@DONE`
    is ssh saying it lost the connection — no block of the page said anything —
    and a page that never reported a block failure is safe to ask again.

    `restart` is asked before that retry, and answers a question the transport
    cannot: whether running the script again is safe, given what the first attempt
    already did to the guest.

    `done` is for the case the retry cannot see: the page finished, printed its
    `@@DONE` into the log on the guest's disk, and the connection carrying its
    stdout never noticed — the guest has no session for it, the socket is still
    ESTAB, and the keepalives keep answering over a channel that will never
    deliver anything again. Glibc's page did exactly that at 03:41: twenty-two
    blocks, `@@DONE`, `@@EXIT 0` in the log, and the runner still waiting on it
    an hour and a half later, three hours from deciding to run the whole thing
    again. When `done()` says the page is over, this stops waiting on the channel
    and returns what it has; the log, which the page writes as it goes, is the
    evidence, and the channel was only ever a copy of it.
    """
    timeout = timeout or PAGE_TIMEOUT
    for attempt in range(tries):
        p = subprocess.Popen(GUEST + ["bash -s"], stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        # Read on another thread: a page that streams has to keep streaming while
        # this one is asking whether it is finished, and a full pipe stops it.
        got = []
        reader = threading.Thread(target=lambda: got.append(
            p.stdout.read().decode("utf-8", "replace")), daemon=True)
        reader.start()
        try:
            with p.stdin:
                p.stdin.write(script.encode())
        except (BrokenPipeError, ValueError, OSError):
            pass  # the page is not obliged to consume all of its script
        why, end = None, time.monotonic() + timeout
        while True:
            left = end - time.monotonic()
            if left <= 0:
                why = f"@@TIMEOUT after {timeout}s"
                break
            try:
                p.wait(timeout=poll if done else left)
                break
            except subprocess.TimeoutExpired:
                if done and done():
                    why = "@@CHANNEL-ABANDONED: the page finished; the connection did not notice"
                    break
        if why:
            p.kill()
        reader.join(60)
        out = "".join(got)
        # An abandoned channel is not a failure: the page said it was done. A
        # timeout is, and is reported as a lost connection so the retry below,
        # which is written for exactly that, gets to decide.
        abandoned = bool(why) and why.startswith("@@CHANNEL-ABANDONED")
        rc = 0 if abandoned else (255 if why else p.returncode)
        if why:
            out += "\n" + why + "\n"
        if rc != 255 or PAGE_DONE in out or attempt + 1 == tries:
            return rc, out
        if restart is not None and not restart():
            # The page is part-built and its own log says so. Running it again
            # unpacks a second copy, installs over what the first attempt installed,
            # and reports blocks that never ran — this build's Tcl page did exactly
            # that at 06:40, nine blocks "never reported" over a Tcl that was
            # already on the disk. Left where it stopped, the phase fails once and
            # says so; run again, it silently lies.
            print("    connection lost mid-page; its own log has markers in it, so "
                  "the page is left where it stopped rather than run twice",
                  flush=True)
            return rc, out
        print(f"    connection to the guest was lost, running the page again "
              f"(attempt {attempt + 2} of {tries})", flush=True)
    return rc, out


# Chapter 7's four mounts, as the host sees them. Guarded by `findmnt` so the
# context can be re-established after a restart without failing on what is
# already there.
CHROOT_MOUNTS = [("/dev", "mount --bind /dev {lfs}/dev"),
                 ("/dev/pts", "mount --bind /dev/pts {lfs}/dev/pts"),
                 ("/proc", "mount -t proc proc {lfs}/proc"),
                 ("/sys", "mount -t sysfs sysfs {lfs}/sys")]


def wrap(script, context, net=False):
    """The same command, in the environment the book types it in.

    `chroot-root` is the chapter 7 invocation verbatim — `env -i` and its six
    variables are the environment the build really gets, which is what task 3.5
    asks to be recorded, so it is recorded by being the code.

    The variables go *inside* the command, because `sudo` resets the environment
    and `env -i` discards it — which is exactly the behaviour being modelled.

    Every command runs inside `unshare -n`: a new network namespace, empty by
    construction, so a build step that reaches for the network finds no route
    rather than silently downloading something `base.lock` never approved. The
    `sources` phase passes `net=True` because fetching is the one step the design
    gives network to. The namespace is created by root and the command is then
    dropped to whoever the context runs as, so the denial does not depend on the
    build user being unprivileged.
    """
    srcdir = "/sources" if context == "chroot-root" else LFS + "/sources"
    prelude = WRAP_PRELUDE.format(lfs=shlex.quote(LFS), srcdir=shlex.quote(srcdir))
    # A fresh namespace has a loopback interface and leaves it DOWN, which is fine
    # until a package's testsuite opens a socket: Tcl's chanio tests run `echo.tcl`,
    # which does `socket -server accept 0`, prints its port, and `vwait`s for a
    # client that can never connect to 127.0.0.1 with no interface to reach it. The
    # test does not fail, it hangs, and it took two 90-minute page timeouts to see
    # it. Loopback is not the network this denial is about — a namespace with only
    # `lo` still has no route out, which is what `NET_DENIED` below claims.
    ns = "" if net else ('unshare -n bash -c \'ip link set lo up 2>/dev/null; '
                         'exec "$0" "$@"\' ')
    if context == "host-root":
        cmd = f"sudo {ns}bash -c {shlex.quote(prelude + script)}"
    elif context == "host-lfs":
        # `-l` would source the chapter 4 .bash_profile, which ends in
        # `exec env -i … /bin/bash` and waits for a human. The variables the
        # build needs are in .bashrc, so that file is sourced and nothing execs.
        cmd = (f"sudo {ns}sudo -u lfs -H bash -c "
               + shlex.quote(f"source $HOME/.bashrc; {prelude}{script}"))
    elif context == "chroot-root":
        # Re-applied on every call, because a mount table is runtime state: a
        # checkpoint does not freeze it, so a VM that comes up for this phase has
        # an empty $LFS/proc and every page inside dies on
        # `comm: /dev/fd/63: No such file or directory`. These are chapter 7's own
        # mount commands, guarded so re-running one is not an error.
        cmd = "".join(f"findmnt {shlex.quote(LFS + where)} >/dev/null || "
                      f"sudo {what.format(lfs=LFS)}; " for where, what in CHROOT_MOUNTS)
        cmd += (f"sudo {ns}chroot $LFS {CHROOT_ENV.format(term='xterm')} "
               f"/bin/bash --login -c {shlex.quote(prelude + script)}")
        cmd = f"export LFS={shlex.quote(LFS)}; " + cmd
    else:
        raise SystemExit(f"context {context!r} is not runnable by this script")
    return "set -u\n" + cmd + "\n"


def substitute(cmd, o=None):
    """The book's placeholders, filled from one place.

    The pages write `/dev/<xxx>` and say “replace <xxx> with the name of the LFS
    partition”, so the value is a bare device name, not a path. The same token
    means something else elsewhere — Glibc's page asks for `<xxx>` to be replaced
    with *a timezone*, and filling it with a partition name leaves
    `/etc/localtime` pointing at `/usr/share/zoneinfo/vdc1`, which `ln -sf`
    creates without complaint — so a page may rename its own placeholders in
    `build-overrides.toml`, and what it renames is not touched here.
    """
    for tok, val in (o or {}).get("placeholder", {}).items():
        cmd = cmd.replace(tok, val)
    for ph, key in (("<xxx>", "LFS_PART"), ("<yyy>", "SWAP_DEV"), ("<zzz>", "SWAP_DEV"),
                 ("<fff>", "ROOT_FS")):
        cmd = cmd.replace(ph, DEV[key])
    return cmd


def deviate(cmd, subs, pg, i):
    """A word of a page's command replaced, and the replacement left in the log.

    `build-overrides.toml` may say that one flag of one block of one page is not
    what this build runs. Saying it in the file is not enough: the log of the run
    has to show the command that actually ran, or the record describes a build
    nobody performed.
    """
    for old, new in subs.items():
        if old not in cmd:
            raise SystemExit(f"{pg} block {i}: build-overrides.toml substitutes {old!r}, "
                             "which the page's command does not contain — the pinned "
                             "edition changed and this deviation is now a guess")
        cmd = cmd.replace(old, new)
        lines = f'echo "@@SUBSTITUTED in block {i}: {safe(old)} -> {safe(new)}"'
        cmd = lines + "\n" + cmd
    return cmd


def safe(s):
    """Prose going into a shell echo: no quoting, no expansion, one line."""
    return re.sub(r"[\"'`$\\&;|<>(){}\n\r]", " ", s).strip()


def page_script(pg, title, tolerates=()):
    """One page's recipe: the book's unpack synopsis, its blocks, its cleanup."""
    o = OVR.get("page", {}).get(pg, {})
    archive = o.get("archive") or archive_for(pg)
    note = "\n".join("#   " + safe(l) for l in o.get("note", "").splitlines() if l.strip())
    lines = ["#!/bin/bash", "# " + pg, "set -u",
             # The runner's own bookkeeping, kept under a name the book cannot
             # collide with: Tcl's second block ends with `unset SRCDIR`, and the
             # cleanup that follows the last block needs the source directory.
             'BCT_SRCDIR="${BCT_SRCDIR:-}"', 
             # fd 3 stays the ssh session; stdout becomes the log. A marker goes to
             # both, because the channel is what dies and the log is what survives,
             # and a marker that exists only on the dead one is not evidence.
             'exec 3>&1', f'exec >"{PAGE_LOG}" 2>&1',
             'm() { echo "$*" | tee -a "' + PAGE_LOG + '" >&3; }',
             # If the shell goes without printing @@DONE, this says it went, and
             # with what. A SIGKILL takes the trap with it, which is also an answer.
             'trap \'m "@@EXIT $?"\' EXIT',
             # The page runs under `set -e`, which the unpack synopsis turns on and
             # nothing turns off, so a failing command ends the shell where it
             # stands — before `rc=$?`, before the abort marker. Without this the
             # page just stops, and a page that stops and a page whose connection
             # died look identical from here. Glibc's testsuite is the case: the
             # suite failed, errexit killed the shell, blocks 7-22 (the install)
             # never ran, and the channel sat open waiting for a shell that was gone.
             # $BCT_RC first: the assignment to FAIL below resets $?, so a trap that
             # reads $? inside the message reports the assignment's success — every
             # failure logged as rc 0, which is a lie in the one place the evidence
             # is supposed to be honest.
             'trap \'BCT_RC=$?; FAIL="$FAIL rc$BCT_RC@$LINENO"; '
             'm "@@FAILED block ${BLOCK:-pre} line $LINENO rc $BCT_RC: '
             '$BASH_COMMAND"\' ERR',
             # The book's synopsis runs with -e, so a failing command inside a block
             # stops the page the way stopping to read an error stops a person. The
             # trap is what makes that visible in the log instead of a silent gap.
              f'echo "PAGE: {safe(pg)} | {safe(title)}"']
    if tolerates:
        lines.append(f'm "@@EXPECTED blocks {sorted(tolerates)} may fail: '
                     + safe(o.get("note", "predicted by the page itself"))[:200] + '"')
    if o.get("pre"):
        lines += ["# override pre: " + note, o["pre"]]
    if archive and not o.get("no_unpack"):
        lines.append(book.wrapper(archive).replace("$BCT_SRCDIR", '"$BCT_SRCDIR"'))
    # `set -e` comes in with the unpack synopsis above and stops here. Left on, it
    # ends the shell at the first command that returns non-zero, and the book
    # expects a person to read that result rather than abort on it — chapter 8's
    # Glibc page asks for `grep "Timed out" *.out`, where finding nothing is the
    # success, and its testsuite exits 2 by design. With it off, the ERR trap
    # records every failing command and the page stops at the block boundary,
    # which is where this script's own judgement of a block has always lived.
    lines.append("set +e")
    take = o.get("only_blocks")
    skip = set(o.get("skip_blocks", []))
    why_skipped = safe(o.get("note", "overridden"))
    # The numbers of the blocks this script actually runs. A block the override
    # skipped is not a block that failed to report, and a page that stops early is
    # only detectable against the list of what was asked for.
    emitted = []
    for i, (why, cmd) in enumerate(book.commands(pg), 1):
        if take is not None and i not in take:
            continue
        if i in skip:
            lines.append(f'm "@@SKIP block {i}: {why_skipped}"')
            continue
        emitted.append(i)
        lines.append(f'BLOCK={i}; FAIL=""')
        lines.append(f'm "@@BLOCK {i} START: {safe(why)[:120]}"')
        block = deviate(substitute(cmd, o), o.get("block_subst", {}).get(str(i), {}), pg, i)
        if i in tolerates:
            # A test suite the page predicts failing is also a test suite that can
            # sit there forever: chapter 8's Tcl page hung half an hour in `chanio`,
            # its `echo.tcl` helper waiting on a child that would never answer, the
            # load average at zero and the page's three-hour clock running. A
            # predicted failure is cheap; a predicted hang costs the phase. So the
            # blocks the book expects to fail get a wall clock — `timeout` puts the
            # suite in its own process group and signals the group, which a plain
            # background job would not: killing the shell leaves `tcltest`'s
            # grandchildren alive and holding the log open.
            cap = o.get("test_block_timeout", TEST_BLOCK_TIMEOUT)
            lines.append("cat > /tmp/.bct-block.sh <<'BCTBLOCK'\n" + block + "\nBCTBLOCK")
            lines.append(f"timeout -k 60 {cap} bash /tmp/.bct-block.sh")
        else:
            lines.append(block)
        # The block's own exit code, and the ERR trap's verdict: a block whose
        # third command failed and whose last one succeeded did not succeed, and
        # a page that reports it as passed is the lie this script exists to catch.
        lines.append('rc=$?; [ -z "$FAIL" ] || rc=1')
        lines.append(f'm "@@RC {i} $rc"')
        if i in tolerates:
            # The page predicts this one failing, so the page keeps going: stopping
            # here would abandon `make install` over a test the book calls safe to
            # ignore, and the phase would report a package that was never installed.
            lines.append(f'[ "$rc" -eq 0 ] || m "@@TOLERATED block {i} rc $rc"')
        else:
            lines.append(f'[ "$rc" -eq 0 ] || {{ m "@@ABORT block {i}"; exit 1; }}')
    if archive and not o.get("no_unpack"):
        lines.append(book.wrapper_post())
    if o.get("post"):
        lines.append(o["post"])
    # The checkpoint stops the guest, and a stop that has to fall back to a kill
    # is a power cut. Flushing at the end of a page keeps a phase's files off the
    # list of things a hard stop can lose.
    lines.append("sync")
    lines.append('m "@@DONE"')
    return "\n".join(lines), emitted


def archive_for(pg):
    """The archive this page builds, from base.lock's own page citations.

    Eight pages cite more than one source — GCC's page unpacks GMP, MPFR and MPC
    into the GCC tree, Python's and Tcl's and SQLite's pages unpack documentation
    beside the source, the udev page has three tarballs — and on those the page
    that cites a file does not say which one it builds. Returning the first match
    would be correct today only because of the order `derive-base-lock.py` wrote
    the file in, so it is refused instead: name the archive in
    `build-overrides.toml`, where the choice is written down with a reason.
    """
    names = [p["url"].rsplit("/", 1)[-1] for p in load_lock()["package"] if pg in p["pages"]]
    if not names:
        return None
    override = OVR.get("page", {}).get(pg, {}).get("archive")
    if len(names) > 1 and not override:
        raise AmbiguousArchive(f"{pg} is cited by {len(names)} sources "
                               f"({', '.join(sorted(names))}); name one with "
                               f"[page.\"{pg}\"] archive = \"...\" in build-overrides.toml")
    return override or names[0]


class AmbiguousArchive(Exception):
    """A page cites several sources and nothing says which one it builds."""


def load_lock():
    return tomllib.loads((ROOT / "base.lock").read_text())


def lock_files():
    """Every file name base.lock approves, package and patch together."""
    lock = load_lock()
    return ({p["url"].rsplit("/", 1)[-1] for p in lock["package"]}
            | {p["file"] for p in lock["patch"]})


# A file name, not a word that ends like one: the token has to start at a
# boundary and carry a digit before its extension, or `gzip`, `bunzip` and the
# `2` of `bzip2-1.0.8-install_docs-1.patch` all read as names.
ARCHIVE_RE = re.compile(r"(?<![\w.+-])[\w.+:~-]*?\d[\w.+:~-]*?"
                        r"(?:tar\.(?:xz|gz|bz2|zst|lzma)|zip|patch|diff|tgz|txz)"
                        r"(?![\w-])")


def sources_named(pg):
    """Archive and patch file names a page's own commands mention.

    The wrapper unpacks what base.lock names. A page can still name a file in a
    command — a `patch -i ../foo.patch`, a hand-written `tar -xvf`, a `wget` —
    and that reference is not checked by anything else.
    """
    names = set()
    for _, cmd in book.commands(pg):
        names |= {t.rsplit("/", 1)[-1] for t in ARCHIVE_RE.findall(cmd)}
    return names


# chapter04/abouttestsuites.html: "the test suites for the core toolchain
# packages—GCC, binutils, and glibc—are of the utmost importance".
CORE_SUITES = {"chapter08/binutils.html": "binutils", "chapter08/gcc.html": "GCC",
               "chapter08/glibc.html": "glibc"}


def test_expectations():
    """The derived record of what the book predicts about its own test suites."""
    path = ROOT / "test-expectations.toml"
    if not path.is_file():
        raise SystemExit("test-expectations.toml is missing; run "
                         "scripts/build/derive-test-expectations.py")
    return {r["id"]: r for r in load_toml(path).get("page", [])}

# Which blocks of a page run its suite is the book's fact, not this file's: it
# comes from the derivation, so a version bump that moves `make check` to another
# block moves the guard with it. Loaded once; a phase cannot run without it.
EXPECTATIONS = test_expectations()


def guard_test_policy(pages):
    """The three suites the book names as utmost importance are never skipped.

    The only way this build skips a command the book printed is an override in
    `build-overrides.toml`, so the policy is a check on the overrides rather than
    a rule in prose: an override that skips, or narrows away, the block holding a
    core suite command fails the phase and says which package lost its tests.
    """
    bad = []
    for pg in pages:
        if pg not in CORE_SUITES:
            continue
        o = OVR.get("page", {}).get(pg, {})
        skip, take = set(o.get("skip_blocks", [])), o.get("only_blocks")
        for block in EXPECTATIONS[pg]["suite"]:
            i = block["block"]
            if i in skip:
                bad.append(f"{pg}: {CORE_SUITES[pg]} test suite skipped (block {i})")
            elif take is not None and i not in take:
                bad.append(f"{pg}: {CORE_SUITES[pg]} test suite not in only_blocks (block {i})")
    return bad


def guard_chapter_complete(ph, planned):
    """Refuse to start a phase whose chapter has a page the plan does not run.

    The edition's index is not its table of contents. In 13.0-openrc the chapter 8
    page for Libpipeline-1.5.8 sits in the sequence — `kbd.html`'s Next link points
    at it, and the man-db page after it fails its `configure` with
    `Package requirements (libpipeline >= 1.5.0) were not met` — but `index.html`
    never lists it, so a build that follows the index alone omits a library the
    chapter depends on and finds out eight pages later. Following the Next links
    is the book's own order, and it costs nothing after the first walk because
    every page is cached.
    """
    chain = book.sequence(ph["chapter"])
    # Only pages the *index* dropped. A page that is in the index and not in this
    # phase is a page the phase said no to by name, in `only` or in `skip`, and
    # that is a decision, not a hole: chapter 7's phase runs eight of its pages.
    listed = {h for h, _ in index_pages()}
    skip = {p + ".html" for p in ph.get("skip", [])}
    want = {p for p in chain if p not in listed and p.split("/")[1] not in skip}
    gap = sorted(want - {p for p, _ in planned})
    return [f"{ch} has {g} in the book's own page sequence and the phase does not "
            f"run it; add it to phases.toml as "
            f'extra = [{{ page = "{g}", after = "<previous page>" }}]'
            for g in gap]


def guard_sources(pages):
    """Sources a page or an override names but base.lock does not carry.

    Checked before anything is downloaded or unpacked, so a phase that reaches
    for an unapproved tarball fails with its name instead of building with it.
    """
    have = lock_files()
    missing = []
    for pg in pages:
        try:
            archive_for(pg)
        except AmbiguousArchive as e:
            missing.append(str(e))
        for name in sorted(sources_named(pg)):
            if name not in have:
                missing.append(f"{pg}: {name}")
    for pg, o in sorted(OVR.get("page", {}).items()):
        archive = o.get("archive")
        if archive and archive not in have:
            missing.append(f"build-overrides.toml page {pg}: {archive}")
    return missing


def sources_needed(pg):
    """Every file the page needs in `$LFS/sources`: its archive and its patches.

    Separate from `guard_present` because it is the part with the edge cases — a
    page with no archive at all (`chapter07/chroot.html`), a page whose archive
    is only in an override, a page whose name is ambiguous — and the caller needs
    a live guest. A `| ({archive_for(pg)} - {None})` written inline once raised
    TypeError on the first page that had no archive, which is every page of
    chapter 7; `check_sources_needed_for_every_page` is why it cannot happen again.
    """
    names = set(sources_named(pg))
    try:
        a = archive_for(pg)
    except AmbiguousArchive:
        a = None
    if a:
        names.add(a)
    return names


def guard_present(pages):
    """Every source the pages of this phase need is on the disk, not just in base.lock.

    A checkpoint freezes the whole LFS disk, sources included, so rolling back to
    one rolls the tarballs back too. `sources` records passed and takes no
    checkpoint of its own, which means a rollback to a later phase can leave the
    tree missing a file a passed phase vouched for — this build hit it twice, once
    as `tar: ncurses-6.6.tar.gz: Cannot open` two pages into chapter 6. The phase
    is blocked here instead, naming the file and the command that puts it back.
    """
    need = {pg: sources_needed(pg) for pg in pages}
    rc, out = guest_run(f"ls {LFS}/sources 2>/dev/null")
    have = set(out.split())
    return [f"{pg}: {a} is named by base.lock but is not in $LFS/sources — "
            f"run: build.py run sources"
            for pg in pages for a in sorted(need[pg] - have)]


def page_reported_anything(context):
    """Whether the page now running has written a single marker to its own log.

    Asked after a connection dies, because the log is on the guest's disk, out of
    reach of whatever went wrong with the socket, and it is the only record of how
    far a page whose channel died actually got.
    """
    rc, out = guest_run(wrap(f"grep -c '^@@' {PAGE_LOG} 2>/dev/null || echo 0", context),
                        timeout=60)
    counts = [line for line in out.splitlines() if line.strip().isdigit()]
    return bool(counts) and counts[-1].strip() != "0"


def run_page(pg, title, context, logdir, timeout_long=True):
    # What the page predicts about its own test suite, asked of the book before
    # the run: a page that stops at a predicted failure never reaches the install
    # that follows it, which is the difference between a tolerated test and a
    # package that was never built.
    predicts = expected_failures(pg, PAGE_DONE)
    script, emitted = page_script(pg, title, predicts)
    print(f"  {pg} ({len(emitted)} blocks, {context})", flush=True)
    # The page prints `@@DONE` into its own log as its last act, so the log can be
    # asked directly, on a short connection of its own, whether the page is over.
    # Only worth asking for a page that can run for an hour; a short page that
    # loses its connection is cheaper to re-run than to poll.
    finished = (lambda: PAGE_DONE in guest_run(
        wrap(f"tail -c 4096 {PAGE_LOG} 2>/dev/null", context), timeout=60)[1]
    ) if timeout_long else None
    rc, out = guest_run(wrap(script, context), done=finished,
                        restart=lambda: not page_reported_anything(context))
    # A second, short connection, which is the point: the page's own channel is
    # the thing that dies, and it dies with the evidence still in the guest. The
    # file is capped because a chapter 8 `make check` log runs to hundreds of
    # megabytes and the head and the tail are what a reader wants of it.
    fetch = (f'f={PAGE_LOG}; n=$(wc -c <"$f" 2>/dev/null || echo 0); '
             'if [ "$n" -le 2000000 ]; then cat "$f"; else head -c 1000000 "$f"; '
             'echo; echo "@@TRUNCATED $n bytes"; tail -c 1000000 "$f"; fi')
    log_rc, body = guest_run(wrap(fetch, context))
    # The channel is a copy of the log, and a copy that stopped being written.
    # When the two disagree about whether the page finished, the log is right.
    if PAGE_DONE not in out and PAGE_DONE in body:
        out = body + "\n" + out
    log = logdir / (pg.split("/")[1][:-5] + ".log")
    log.write_text(f"# {pg}\n# context: {context}\n# ssh exit: {rc}\n"
                   f"# markers: {out.strip()[-200:]}\n\n{body}")
    codes = dict(re.findall(r"@@RC (\d+) (-?\d+)", out))
    blocks = [(i, why, codes.get(str(i))) for i, (why, _) in enumerate(book.commands(pg), 1)
              if str(i) in codes]
    expected = expected_failures(pg, out)
    bad = [f"block {b[0]} rc {b[2]}" for b in blocks if b[2] != "0" and b[0] not in expected]
    # A block that never reported is a block that never ran. `exec` is the usual
    # cause — chapter 7's createfiles page ends with `exec /usr/bin/bash --login`
    # for the person at the keyboard, which replaced the shell and took the page's
    # last block, the one creating /var/log/{btmp,lastlog,wtmp}, with it. The page
    # reported 5 of 7 blocks and passed. Nothing downstream can tell, so this does.
    ran = {b[0] for b in blocks}
    bad += [f"block {i} never reported (the page stopped early)"
            for i in emitted if i not in ran]
    return {"page": pg, "title": title, "exit": rc, "log_rc": log_rc,
            "done": PAGE_DONE in out, "blocks": len(blocks),
            "failed": bad, "tolerated_blocks": sorted(expected),
            "log": str(log.relative_to(ROOT))}


def expected_failures(pg, out):
    """The blocks whose failure the book predicts, read from the derived table.

    `test-expectations.toml` is re-read from the pinned edition by
    derive-test-expectations.py: it knows which block of a page runs that page's
    suite — `make test` and `make check` and the ones wrapped in `su tester -c`
    alike, which no regex of mine matched without going on a guessing spree — and
    it knows whether the page says anything at all about failures. Both halves
    matter. A suite the book calls failing is expected and the page continues to
    its `make install`; a suite the book says nothing about (Tcl, DejaGNU) has
    said nothing, and a red line from it is news, not weather.

    Tolerance is read narrowly: the page's suite blocks and nothing else, because
    a page that predicts a failing test has spoken about its test suite, not about
    its `make install`, and a run that forgives a failed install while reporting a
    green build is the failure this whole script exists to catch.
    """
    # The override says so, whatever the book says: a block that reports the news a
    # person wanted (chapter 8's `grep "Timed out" *.out`, which exits 1 when
    # nothing timed out) is not a failure, and no command can tell that by itself.
    declared = {int(i) for i in OVR.get("page", {}).get(pg, {}).get("expected_blocks", [])}
    if "@@DONE" not in out:
        return declared
    predicts = EXPECTATIONS.get(pg) or {}
    if not predicts.get("expect"):
        return declared
    return declared | {int(b["block"]) for b in predicts.get("suite", [])}



# Every page script prints this last, after its `sync`. A page whose stream lacks
# it stopped early whatever else it managed to print, and that is the only
# question worth asking about a truncated stream: `@@RC` appearing somewhere is
# no answer, because block 1 reporting is compatible with dying at block 6.
PAGE_DONE = "@@DONE"

# Where a page's real output goes. The channel carries markers; the log lives on
# the disk the checkpoint protects, and is fetched afterwards on a short
# connection. A ninety minute `make check` streaming a hundred thousand lines
# through qemu's user-mode networking is the thing that keeps wedging.
PAGE_LOG = "/tmp/.bct-page.log"

# Three hours for one page: long enough for the slowest thing the book asks of a
# single package (GCC, with its testsuite), short enough that a channel that has
# stopped carrying anything costs a retry rather than half a working day.
PAGE_TIMEOUT = 3 * 3600

# Ninety minutes for one test suite, on the pages that predict it failing: long
# enough that nothing the book asks of chapter 8 is cut short (Glibc's suite runs
# about an hour), short enough that a hung helper costs a suite and not a day.
# A page whose suite genuinely needs longer says so with `test_block_timeout`.
TEST_BLOCK_TIMEOUT = 90 * 60

WRAP_PRELUDE = "export LFS={lfs} BCT_SRCDIR={srcdir}; "
NET_DENIED = "denied (unshare -n: no routes, no interfaces; verified against the same probe outside)"

# --- one build at a time ---------------------------------------------------------

LOCK = ROOT / ".build-lock"


def pid_cmdline(pid):
    """What a pid is, for the message that tells someone to go ask it to stop."""
    try:
        out = subprocess.run(["ps", "-p", str(pid), "-o", "command="],
                             stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=10)
        return out.stdout.decode().strip()[:120] or "unknown command"
    except Exception:
        return "unknown command"


def acquire_lock(what):
    """Refuse to start a second build against the same build root.

    Two `build.py run` invocations share one $LFS, one qcow2 and one snapshot
    chain: the second one's checkpoint would freeze the first one's half-written
    files and call it a passed phase. The lock is a file holding the owner's pid,
    created with O_EXCL so two simultaneous starters cannot both believe they won.
    """
    try:
        fd = os.open(LOCK, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o644)
    except FileExistsError:
        pid = 0
        try:
            pid = int(LOCK.read_text().split("\t")[0])
        except (ValueError, OSError):
            pass
        alive = False
        if pid > 0:
            try:
                os.kill(pid, 0)
                alive = True
            except ProcessLookupError:
                alive = False
            except PermissionError:
                alive = True  # somebody else's process, and very much alive
        if alive:
            raise SystemExit(
                f"build root is locked by pid {pid} running {pid_cmdline(pid)} — that run "
                f"owns $LFS and the snapshot chain; wait for it, or stop it and remove "
                f"{LOCK}")
        raise SystemExit(
            f"stale lock: {LOCK} names pid {pid}, which is not running — the last build "
            f"died mid-phase. Nothing owns $LFS now; clear it with: rm {LOCK}")
    with os.fdopen(fd, "w") as f:
        f.write(f"{os.getpid()}\t{what}\t"
                f"{datetime.datetime.now().isoformat(timespec='seconds')}\n")


def release_lock():
    LOCK.unlink(missing_ok=True)


def chroot_environment():
    """The variables the book's own chroot invocation passes, as a set.

    `chapter07/chroot.html` prints the command; this reads the `VAR=` tokens off
    it, so the comparison below is against the page and not against a memory of
    an older edition's command.
    """
    names = set()
    for _, cmd in book.commands("chapter07/chroot.html"):
        if "chroot" in cmd and "env -i" in cmd.replace("\n", " "):
            names |= set(re.findall(r"^\s+([A-Z][A-Z0-9_]*)=", cmd, re.M))
    return names


def wrap_environment_delta():
    """What this orchestrator hands the chroot beyond the book's invocation.

    Two layers, and the difference between them is the point. `env -i` is the
    *environment*: the book's own six variables, no more, and the check is that
    this build adds nothing there. The *shell that runs the page* then gets the
    prelude's two exports, because a script has to name the source directory
    without hardcoding where the disk is mounted — `BCT_SRCDIR` is `/sources` inside
    the chroot and `$LFS/sources` outside it, which is the whole reason the
    orchestrator has a variable for it instead of a literal in 139 page scripts.
    """
    book_vars = chroot_environment()
    env_i = set(re.findall(r"([A-Z][A-Z0-9_]*)=", CHROOT_ENV))
    prelude = set(re.findall(r"\b([A-Z][A-Z0-9_]*)=", WRAP_PRELUDE))
    return book_vars, env_i, prelude


def write_record(logdir, rec):
    """One record per phase, replaced atomically.

    The record is written once, after the phase's pages ran and its criteria were
    evaluated, and it is moved into place rather than opened: a run that dies
    while writing leaves the previous run's record whole instead of truncated.
    That is what makes `build.py status` readable after a crash, and what lets a
    resume decide where to continue from.

    Every record carries the identity of what it was built against — the bytes of
    `base.lock` and the book it was read from — because a record that does not say
    which inputs it had cannot be evidence about any of them.
    """
    rec = {**rec, "base_lock": hashlib.sha256((ROOT / "base.lock").read_bytes()).hexdigest(),
           "book": book.BASE}
    tmp = logdir / "record.json.tmp"
    tmp.write_text(json.dumps(rec, indent=1) + "\n")
    os.replace(tmp, logdir / "record.json")


def run_phase(name):
    ph = phases()[name]
    logdir = EVIDENCE / name
    logdir.mkdir(parents=True, exist_ok=True)
    started = datetime.datetime.now().isoformat(timespec="seconds")
    if ph.get("needs_mount", True) and not ensure_mounted():
        raise SystemExit(f"{name}: $LFS is not mounted and the phase cannot start")
    results = []
    only_page = run_phase.only_page
    todo = [p for p in pages_for(ph)
            if not only_page or p[0].endswith(only_page)]
    if only_page and not todo:
        # A typo in --page used to run no pages and report the phase FAILED,
        # which reads like the build broke rather than like the name did.
        raise SystemExit(f"--page {only_page!r} matches no page of phase {name!r}; "
                         f"run: build.py pages {name}")
    missing = (guard_sources([pg for pg, _ in todo])
               + guard_test_policy([pg for pg, _ in todo])
               + guard_present([pg for pg, _ in todo])
               + ([] if only_page else guard_chapter_complete(ph, todo)))
    if missing:
        # Nothing was downloaded, unpacked or run: the phase is over before it
        # started, and the record says which page asked for what.
        rec = {"phase": name, "started": started,
               "finished": datetime.datetime.now().isoformat(timespec="seconds"),
               "context": ph["context"], "pages": 0, "pages_passed": False,
               "evidence_complete": False, "criteria": [], "passed": False,
               "blocked": missing, "results": [], "entry": ph["entry"],
               "exit_criteria": ph["exit"], "checkpoint": "none"}
        write_record(logdir, rec)
        write_log(rec, ph)
        print(f"{name}: FAILED before running anything:\n  "
              + "\n  ".join(missing), flush=True)
        return rec
    # A phase that died on page 15 of 84 has 14 pages of work on the disk that no
    # record can point at, because the record is per phase and says FAILED. With
    # --skip-passed the previous attempt's passed pages are believed: the disk is
    # the same disk, nothing rolled back underneath it, and rebuilding Glibc to
    # find out that Tcl still hangs costs two hours an attempt.
    already = set()
    if getattr(run_phase, "skip_passed", False) and (logdir / "record.json").is_file():
        prev = json.loads((logdir / "record.json").read_text())
        already = {r["page"]: r for r in prev.get("results", [])
                   if r.get("done") and not r.get("failed") and r.get("log")}
    for pg, title in todo:
        ctx = OVR.get("page", {}).get(pg, {}).get("context", ph["context"])
        if pg in already:
            print(f"  {pg} (passed in the previous attempt of this phase)", flush=True)
            # The previous attempt's log, named in this record: the evidence a
            # phase has to point at is still on the disk, and a record that
            # invents a page with no log would fail its own evidence check.
            results.append({**already[pg], "title": title,
                            "skipped": "already passed; --skip-passed"})
            continue
        r = run_page(pg, title, ctx, logdir)
        results.append(r)
        print(f"    -> exit {r['exit']} blocks {r['blocks']} failed {r['failed']}", flush=True)
        if r["exit"] != 0 or r["failed"]:
            break
    checks = verify(ph, logdir)
    if name == "sources":
        # This phase's exit criteria are about files in $LFS/sources, and the only
        # thing that knows them is the fetch. `verify = []` used to mean "checked
        # elsewhere", which let the phase record passed with nothing evaluated —
        # and it had: two of the 93 sources were never downloaded, and the phase
        # that exists to notice that said passed.
        bad, n = fetch()
        checks.append({"name": "every file base.lock names is present with the published md5",
                       "cmd": "fetch: wget what is absent, md5sum everything",
                       "rc": 1 if bad else 0,
                       "out": f"{n} files named by base.lock; mismatches and absences: "
                              f"{bad or 'none'}"})
    evidence = results and all(pathlib.Path(ROOT / r["log"]).is_file() for r in results)
    # A page is what it did, not what its shell returned: the page script exits
    # non-zero on a failing block, and a block the page itself predicts failing
    # (`do not pass`, `usually safe to ignore`) is not a failure. What a page has
    # to prove is that it ran to the end and that nothing unexpected failed.
    pages_ok = bool(results) and all(r["done"] and not r["failed"] for r in results)
    # A phase passes on criteria that were evaluated. Not having any is not a pass:
    # the same fail-closed rule the base.lock check applies to an empty class map.
    criteria_ok = bool(checks) and all(c["rc"] == 0 for c in checks)
    rec = {"phase": name, "started": started,
           "finished": datetime.datetime.now().isoformat(timespec="seconds"),
           "context": ph["context"], "pages": len(results),
           # In the record, because "the build cannot reach the network" is a claim
           # about the run and the run is what the record is. Measured 2026-11-11:
           # a TCP connect to the guest's own gateway answers outside the
           # namespace and finds no route inside one.
           "net": NET_DENIED,
           "pages_passed": pages_ok,
           "evidence_complete": bool(evidence),
           "criteria": checks,
           "passed": pages_ok and bool(evidence) and criteria_ok,
           "results": results,
           "entry": ph["entry"], "exit_criteria": ph["exit"],
           "checkpoint": ph["checkpoint"]}
    write_record(logdir, rec)
    print(f"{name}: {'passed' if rec['passed'] else 'FAILED'} "
          f"({len(results)}/{len(todo)} pages, "
          f"{sum(1 for c in checks if c['rc'] == 0)}/{len(checks)} criteria)", flush=True)
    write_log(rec, ph)
    if rec["passed"] and ph["checkpoint"] != "none":
        checkpoint(name)
    return rec


def ensure_mounted():
    """Put $LFS back where the book expects it.

    A checkpoint restarts the guest, and a restart drops the mount. The book
    leaves re-mounting to the human who comes back to the build; this is the
    machine coming back, and it is the phase's entry criterion, so it is done
    here and said out loud rather than assumed.
    """
    # Retried, because the usual reason this fails is that a rollback just
    # restarted the guest: ssh answers in two seconds, `sudo` takes a few more,
    # and a phase that gives up on the first try is a phase that needs a human to
    # type the same command again.
    for attempt in range(4):
        rc, _ = guest_run(f"findmnt {shlex.quote(LFS)} >/dev/null || "
                          f"(sudo mkdir -p {LFS} && sudo mount /dev/{DEV['LFS_PART']} {LFS})")
        if rc == 0:
            return True
        time.sleep(5 * (attempt + 1))
    print("  $LFS could not be mounted", flush=True)
    return False


def verify(ph, logdir):
    """Evaluate the phase's own exit criteria, and write what each one answered.

    A phase with no criteria to evaluate cannot record passed. That is the same
    rule the base.lock check applies to an empty class map: a gate that checks
    nothing has not been passed.
    """
    if "verify" not in ph:
        print("  no verify commands: the phase has no criteria to evaluate", flush=True)
        return []
    if not ph["verify"]:
        return []
    checks = []
    for cmd in ph["verify"]:
        rc, out = guest_run(wrap(cmd, "host-root"))
        checks.append({"check": cmd, "rc": rc, "out": out.strip()[:200]})
        print(f"  [{'ok' if rc == 0 else 'FAIL'}] {cmd}", flush=True)
    (logdir / "criteria.log").write_text(
        "\n\n".join(f"$ {c['check']}\nrc {c['rc']}\n{c['out']}" for c in checks) + "\n")
    return checks


SUBSTRATE = ["ssh", "-o", "BatchMode=yes", "user@192.168.0.86"]


def host(script):
    """Run a shell command on the substrate, where qemu and the disk images are."""
    return subprocess.run(SUBSTRATE + [script], capture_output=True, text=True)


def checkpoint(tag):
    print(f"checkpointing the LFS disk as {tag}", flush=True)
    subprocess.run(SUBSTRATE + [f"cd ~/barecortex-vm && ./vm.sh snapshot {tag}"])


def phase_records():
    out = []
    if EVIDENCE.is_dir():
        for d in sorted(EVIDENCE.iterdir()):
            if (d / "record.json").is_file():
                out.append(json.loads((d / "record.json").read_text()))
    return out


def next_phase(records, order):
    """The first phase with no passed record. That is where a resume starts.

    Pure, because the interesting part of resuming is deciding where: a phase
    that failed keeps its name (its pages have to run again), a phase that passed
    is skipped even if it is the one that just failed its criteria and was
    re-run. `order` comes from phases.toml, so the sequence is still the book's.
    """
    done = {r["phase"] for r in records if r["passed"]}
    return next((p for p in order if p not in done), None)


def cmd_resume():
    """Run the next phase, on the disk the phase before it checkpointed.

    The rollback is the part a person forgets: a failed phase leaves the disk
    half-built, and running the next phase on top of that builds chapter 7 on a
    broken chapter 6. So resume rolls back to the checkpoint the phase before
    this one recorded, unconditionally — a phase's writes are only worth keeping
    once it has passed — and then runs the phase that has not.
    """
    order = list(phases())
    recs = phase_records()
    nxt = next_phase(recs, order)
    if nxt is None:
        print("every phase in phases.toml has passed")
        return 0
    i = order.index(nxt)
    if i:
        prev = next((r for r in recs if r["phase"] == order[i - 1]), None)
        # The tag is the phase's name — `checkpoint(name)` in run_phase — while
        # the record's `checkpoint` field is the *policy* from phases.toml
        # ("after"), which is not a file on the substrate.
        tag = order[i - 1] if (prev or {}).get("checkpoint", "none") != "none" else "none"
        if tag != "none":
            # Unconditionally, not "when the disk is not already that checkpoint":
            # the working image is a child of the checkpoint even when it is a
            # child covered in the failed phase's own writes, and a backing-chain
            # check cannot tell those two apart. A phase's writes are only worth
            # keeping once the phase has passed — the same rule that makes a
            # chapter 6 page un-retryable in place (`ln -sv` meets the symlink its
            # own previous attempt left behind).
            gen = host(f"ls -1 ~/barecortex-vm/sn/{tag}-*.qcow2 2>/dev/null | tail -1").stdout.strip()
            if gen:
                name = gen.rsplit("/", 1)[-1].removesuffix(".qcow2")
                print(f"{nxt} follows {order[i-1]}: rolling the disk back to {name} "
                      f"so the phase starts where that one ended", flush=True)
                subprocess.run(SUBSTRATE + [f"cd ~/barecortex-vm && ./vm.sh rollback {name}"])
            else:
                print(f"no {tag} checkpoint on the substrate; running {nxt} "
                      f"on the disk as it is", flush=True)
    # After the rollback, not before: the checkpoint froze the whole disk,
    # `$LFS/sources` included, so it is the rollback that can take a tarball back
    # while `sources` still records passed. `run` refuses to build on that;
    # `resume` is the verb meant to get the build going again, and re-running
    # `sources` is a verified fetch, not a guess.
    if guard_present([pg for pg, _ in pages_for(phases()[nxt])]):
        print(f"{nxt} needs sources the disk does not have — re-running sources first",
              flush=True)
        run_sources()
    return run_phase(nxt)


def write_log(rec, ph):
    """Every build step, in the documentation, as it happens."""
    if not LOGMD.is_file():
        LOGMD.write_text("# Build log\n\n"
                         "Every phase of the system build, recorded as it ran: what was executed,\n"
                         "where in the pinned book it comes from, what it returned, and what was\n"
                         "frozen afterwards. Written by `scripts/build/build.py`; nothing here is\n"
                         "reconstructed afterwards from memory.\n")
    with LOGMD.open("a") as f:
        f.write(f"\n## {rec['phase']} — {'passed' if rec['passed'] else 'FAILED'}\n\n")
        f.write(f"{rec['started']} → {rec['finished']}, context `{rec['context']}`, "
                f"{rec['pages']} page(s) of chapter {ph['chapter']}.\n\n")
        f.write(f"Book pages: " + ", ".join(f"`{r['page']}`" for r in rec["results"]) + "\n\n")
        f.write("Exit criteria this phase claims to meet:\n\n")
        for c in rec["exit_criteria"]:
            f.write(f"- {c}\n")
        f.write("\nFailures: " + (", ".join(f"`{x}`" for r in rec["results"]
                                            for x in r["failed"]) or "none") + "\n")
        if rec["criteria"]:
            f.write("\nThe phase's exit criteria, evaluated after the pages ran:\n\n")
            for c in rec["criteria"]:
                f.write(f"- `{'ok' if c['rc'] == 0 else 'FAILED ' + str(c['rc'])}` "
                        f"`{c['check']}`\n")
        f.write(f"\nCheckpoint: `{rec['checkpoint']}`\n")


def run_sources():
    """The sources phase: chapter 3 is two tables, not commands, so the phase is
    the fetch and the fetch's checksum comparison is the phase's criterion."""
    logdir = EVIDENCE / "sources"
    logdir.mkdir(parents=True, exist_ok=True)
    started = datetime.datetime.now().isoformat(timespec="seconds")
    bad, n = fetch()
    ph = phases()["sources"]
    r = {"page": "chapter03/packages.html", "title": "Packages", "exit": 1 if bad else 0,
         "blocks": n, "failed": [f"checksum mismatch {b}" for b in bad],
         "log": "build-evidence/sources/fetch.log"}
    rec = {"phase": "sources", "started": started,
           "finished": datetime.datetime.now().isoformat(timespec="seconds"),
           "context": ph["context"], "pages": 1, "pages_passed": not bad,
           "evidence_complete": (logdir / "fetch.log").is_file(),
           "criteria": [{"check": f"{n} files, MD5 as published by the book",
                         "rc": 1 if bad else 0, "out": ", ".join(bad) or "all matched"}],
           "passed": not bad and (logdir / "fetch.log").is_file(),
           "results": [r], "entry": ph["entry"], "exit_criteria": ph["exit"],
           "checkpoint": ph["checkpoint"]}
    write_record(logdir, rec)
    write_log(rec, ph)
    print(f"sources: {'passed' if rec['passed'] else 'FAILED'}", flush=True)
    if rec["passed"] and ph["checkpoint"] != "none":
        checkpoint("sources")
    return rec


def fetch():
    """Download what base.lock names, and verify it against the book's checksums.

    The cache is `$LFS/sources` itself: a file that exists with the published
    checksum is never requested again, and a file whose checksum does not match
    is never unpacked. There is no second copy and no second place to be stale.
    """
    lock = load_lock()
    if not ensure_mounted():
        raise SystemExit("fetch: $LFS is not mounted; refusing to write into the host's "
                         "directory tree instead of the LFS filesystem")
    src = LFS + "/sources"
    items = [(p["url"], p["md5"], p["url"].rsplit("/", 1)[-1]) for p in lock["package"]]
    items += [(p["url"], p["md5"], p["file"]) for p in lock["patch"]]
    script = [f"mkdir -p {src}", f"cd {src} || exit 1",
              # an interrupted fetch leaves .part files behind; they are never renamed
              # and never verified, so they are cleared before anything is resumed
              "rm -f *.part"]
    mirrors = OVR.get("mirror", {})
    for url, md5, name in items:
        # The book's URL is tried first and alone if the book is reachable. When it
        # is not, a mirror listed in build-overrides.toml may stand in, and the
        # authority stays the book's own MD5 — that is what a checksum is for.
        urls = [url] + list(mirrors.get(name, []))
        quote = " ".join(shlex.quote(u) for u in urls)
        script.append(f'if [ ! -f {shlex.quote(name)} ]; then '
                      f'for u in {quote}; do '
                      f'wget -q --tries=3 -O {shlex.quote(name)}.part "$u" '
                      f'&& {{ mv {shlex.quote(name)}.part {shlex.quote(name)}; '
                      f'echo "@@FROM {name} $u"; break; }}; '
                      f'rm -f {shlex.quote(name)}.part; done; '
                      f'test -f {shlex.quote(name)} || echo "@@FETCH-FAIL {name}"; fi')
        script.append(f'echo "@@MD5 {name} $(md5sum {shlex.quote(name)} 2>/dev/null '
                      f'| cut -d" " -f1) {md5}"')
    rc, out = guest_run(wrap("\n".join(script), "host-root", net=True), timeout=7200)
    bad = mismatches(out)
    (EVIDENCE / "sources").mkdir(parents=True, exist_ok=True)
    (EVIDENCE / "sources" / "fetch.log").write_text(out)
    print(f"fetched {len(items)} files, checksum mismatches: {bad or 'none'}")
    return bad, len(items)


def mismatches(fetch_output):
    """The files the fetch names as not matching what base.lock published.

    Three shapes arrive: a match, a mismatch, and a file that is not there at all
    (md5sum printed nothing, so the line has one field fewer). All three name the
    file when they are a problem, and none of them is retried, repaired or
    ignored — a source that does not match is a build that stops.
    """
    bad = []
    for line in fetch_output.splitlines():
        if line.startswith("@@FETCH-FAIL"):
            bad.append(line.split()[1])
        elif line.startswith("@@MD5"):
            fields = line.split()
            if len(fields) != 4 or fields[2] != fields[3]:
                bad.append(fields[1])
    return sorted(set(bad))


def status():
    for d in sorted(EVIDENCE.iterdir()) if EVIDENCE.is_dir() else []:
        r = d / "record.json"
        if r.is_file():
            j = json.loads(r.read_text())
            print(f"{j['phase']:16} {'passed' if j['passed'] else 'FAILED':7} "
                  f"pages={j['pages']} checkpoint={j['checkpoint']} {j['finished']}")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["pages", "run", "fetch", "resume", "status", "guest"])
    ap.add_argument("arg", nargs="?")
    ap.add_argument("--page")
    ap.add_argument("--skip-passed", action="store_true",
                    help="in a phase that failed part way, do not re-run the pages "
                         "its last record says passed; the disk is unchanged")
    a = ap.parse_args()
    run_phase.only_page = a.page
    run_phase.skip_passed = a.skip_passed
    if a.cmd in ("run", "fetch", "resume"):
        acquire_lock(f"{a.cmd} {a.arg}")
        try:
            return _dispatch(a)
        finally:
            release_lock()
    return _dispatch(a)


def _dispatch(a):
    if a.cmd == "pages":
        for pg, t in pages_for(phases()[a.arg]):
            print(f"{pg:44} {t[:40]}")
    elif a.cmd == "run":
        rec = run_sources() if a.arg == "sources" else run_phase(a.arg)
        return 0 if rec["passed"] else 1
    elif a.cmd == "resume":
        return cmd_resume()
    elif a.cmd == "fetch":
        bad, _ = fetch()
        return 1 if bad else 0
    elif a.cmd == "guest":
        rc, out = guest_run(a.arg)
        print(out)
        return rc
    else:
        return status()


if __name__ == "__main__":
    sys.exit(main())
