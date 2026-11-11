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
import json
import pathlib
import re
import shlex
import subprocess
import sys
import tomllib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import book  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
EVIDENCE = ROOT / "build-evidence"
LOGMD = ROOT / "docs" / "BUILD-LOG.md"
OVERRIDES = ROOT / "build-overrides.toml"

GUEST = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=25", "-J",
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
    for href, title in RE.findall(r'href="(chapter\d+/[A-Za-z0-9:._-]+\.html)"[^>]*>(.*?)</a>',
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
    return out


# --- running things in the guest --------------------------------------------------


def guest_run(script, timeout=10800):
    p = subprocess.run(GUEST + ["bash -s"], input=script.encode(),
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
    return p.returncode, p.stdout.decode("utf-8", "replace")


def wrap(script, context):
    """The same command, in the environment the book types it in.

    `chroot-root` is the chapter 7 invocation verbatim — `env -i` and its six
    variables are the environment the build really gets, which is what task 3.5
    asks to be recorded, so it is recorded by being the code.

    The variables go *inside* the command, because `sudo` resets the environment
    and `env -i` discards it — which is exactly the behaviour being modelled.
    """
    srcdir = "/sources" if context == "chroot-root" else LFS + "/sources"
    prelude = f"export LFS={shlex.quote(LFS)} SRCDIR={shlex.quote(srcdir)}; "
    if context == "host-root":
        cmd = f"sudo bash -c {shlex.quote(prelude + script)}"
    elif context == "host-lfs":
        # `-l` would source the chapter 4 .bash_profile, which ends in
        # `exec env -i … /bin/bash` and waits for a human. The variables the
        # build needs are in .bashrc, so that file is sourced and nothing execs.
        cmd = (f"sudo -u lfs -H bash -c "
               + shlex.quote(f"source $HOME/.bashrc; {prelude}{script}"))
    elif context == "chroot-root":
        cmd = (f"sudo chroot $LFS {CHROOT_ENV.format(term='xterm')} "
               f"/bin/bash --login -c {shlex.quote(prelude + script)}")
        cmd = f"export LFS={shlex.quote(LFS)}; " + cmd
    else:
        raise SystemExit(f"context {context!r} is not runnable by this script")
    return "set -u\n" + cmd + "\n"


def substitute(cmd):
    """The book's placeholders, filled from one place.

    The pages write `/dev/<xxx>` and say “replace <xxx> with the name of the LFS
    partition”, so the value is a bare device name, not a path.
    """
    for ph, key in (("<xxx>", "LFS_PART"), ("<yyy>", "SWAP_DEV"), ("<zzz>", "SWAP_DEV")):
        cmd = cmd.replace(ph, DEV[key])
    return cmd


def safe(s):
    """Prose going into a shell echo: no quoting, no expansion, one line."""
    return re.sub(r"[\"'`$\\&;|<>(){}\n\r]", " ", s).strip()


def page_script(pg, title):
    """One page's recipe: the book's unpack synopsis, its blocks, its cleanup."""
    o = OVR.get("page", {}).get(pg, {})
    archive = o.get("archive") or archive_for(pg)
    note = "\n".join("#   " + safe(l) for l in o.get("note", "").splitlines() if l.strip())
    lines = ["#!/bin/bash", "# " + pg, "set -u",
             # The book's synopsis runs with -e, so a failing command inside a block
             # stops the page the way stopping to read an error stops a person. The
             # trap is what makes that visible in the log instead of a silent gap.
             'trap \'echo "@@FAILED line $LINENO: $BASH_COMMAND"\' ERR',
             f'echo "PAGE: {safe(pg)} | {safe(title)}"']
    if o.get("pre"):
        lines += ["# override pre: " + note, o["pre"]]
    if archive and not o.get("no_unpack"):
        lines.append(book.wrapper(archive).replace("$SRCDIR", '"$SRCDIR"'))
    take = o.get("only_blocks")
    skip = set(o.get("skip_blocks", []))
    why_skipped = safe(o.get("note", "overridden"))
    n = 0
    for i, (why, cmd) in enumerate(book.commands(pg), 1):
        if take is not None and i not in take:
            continue
        if i in skip:
            lines.append(f'echo "@@SKIP block {i}: {why_skipped}"')
            continue
        n += 1
        lines.append(f'echo "@@BLOCK {i} START: {safe(why)[:120]}"')
        lines.append(substitute(cmd))
        lines.append("rc=$?")
        lines.append(f'echo "@@RC {i} $rc"')
        lines.append(f'[ "$rc" -eq 0 ] || {{ echo "@@ABORT block {i}"; exit 1; }}')
    if archive and not o.get("no_unpack"):
        lines.append(book.wrapper_post())
    if o.get("post"):
        lines.append(o["post"])
    # The checkpoint stops the guest, and a stop that has to fall back to a kill
    # is a power cut. Flushing at the end of a page keeps a phase's files off the
    # list of things a hard stop can lose.
    lines.append("sync")
    lines.append('echo "@@DONE"')
    return "\n".join(lines), n


def archive_for(pg):
    """The archive this page builds, from base.lock's own page citations."""
    lock = tomllib.loads((ROOT / "base.lock").read_text())
    for p in lock["package"]:
        if pg in p["pages"]:
            return p["url"].rsplit("/", 1)[-1]
    return None


def run_page(pg, title, context, logdir):
    script, nblocks = page_script(pg, title)
    print(f"  {pg} ({nblocks} blocks, {context})", flush=True)
    rc, out = guest_run(wrap(script, context))
    log = logdir / (pg.split("/")[1][:-5] + ".log")
    log.write_text(f"# {pg}\n# context: {context}\n# ssh exit: {rc}\n\n{out}")
    codes = dict(re.findall(r"@@RC (\d+) (-?\d+)", out))
    blocks = [(i, why, codes.get(str(i))) for i, (why, _) in enumerate(book.commands(pg), 1)
              if str(i) in codes]
    expected = expected_failures(pg, out)
    bad = [b for b in blocks if b[2] != "0" and not expected]
    return {"page": pg, "title": title, "exit": rc, "blocks": len(blocks),
            "failed": [f"block {b[0]} rc {b[2]}" for b in bad],
            "expected_failure": bool(expected), "log": str(log.relative_to(ROOT))}


def expected_failures(pg, out):
    """A page that says a test is known to fail has stated its own expectation."""
    if "@@DONE" not in out:
        return []
    return [n for n in book.notes(pg) if "known to fail" in n.lower()]


def run_phase(name):
    ph = phases()[name]
    logdir = EVIDENCE / name
    logdir.mkdir(parents=True, exist_ok=True)
    started = datetime.datetime.now().isoformat(timespec="seconds")
    if ph.get("needs_mount", True) and not ensure_mounted():
        raise SystemExit(f"{name}: $LFS is not mounted and the phase cannot start")
    results = []
    todo = pages_for(ph)
    only_page = run_phase.only_page
    for pg, title in todo:
        if only_page and not pg.endswith(only_page):
            continue
        ctx = OVR.get("page", {}).get(pg, {}).get("context", ph["context"])
        r = run_page(pg, title, ctx, logdir)
        results.append(r)
        print(f"    -> exit {r['exit']} blocks {r['blocks']} failed {r['failed']}", flush=True)
        if r["exit"] != 0 or r["failed"]:
            break
    checks = verify(ph, logdir)
    evidence = results and all(pathlib.Path(ROOT / r["log"]).is_file() for r in results)
    pages_ok = bool(results) and all(r["exit"] == 0 and not r["failed"] for r in results)
    # `verify = []` says the criteria are evaluated by the phase's own
    # implementation (the sources phase checks every MD5 as it fetches), not that
    # there are none. A phase with no `verify` key at all has neither, and fails.
    criteria_ok = bool(ph.get("verify")) and all(c["rc"] == 0 for c in checks) \
        if ph.get("verify") else ph.get("verify") == []
    rec = {"phase": name, "started": started,
           "finished": datetime.datetime.now().isoformat(timespec="seconds"),
           "context": ph["context"], "pages": len(results),
           "pages_passed": pages_ok,
           "evidence_complete": bool(evidence),
           "criteria": checks,
           "passed": pages_ok and bool(evidence) and criteria_ok,
           "results": results,
           "entry": ph["entry"], "exit_criteria": ph["exit"],
           "checkpoint": ph["checkpoint"]}
    (logdir / "record.json").write_text(json.dumps(rec, indent=1) + "\n")
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
    rc, _ = guest_run(f"findmnt {shlex.quote(LFS)} >/dev/null || "
                      f"(sudo mkdir -p {LFS} && sudo mount /dev/{DEV['LFS_PART']} {LFS})")
    if rc:
        print("  $LFS could not be mounted", flush=True)
    return rc == 0


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


def checkpoint(tag):
    print(f"checkpointing the LFS disk as {tag}", flush=True)
    subprocess.run(["ssh", "-o", "BatchMode=yes", "user@192.168.0.86",
                    f"cd ~/barecortex-vm && ./vm.sh snapshot {tag}"])


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
    (logdir / "record.json").write_text(json.dumps(rec, indent=1) + "\n")
    write_log(rec, ph)
    print(f"sources: {'passed' if rec['passed'] else 'FAILED'}", flush=True)
    if rec["passed"] and ph["checkpoint"] != "none":
        checkpoint("sources")
    return rec


def fetch():
    """Download what base.lock names, and verify it against the book's checksums."""
    lock = tomllib.loads((ROOT / "base.lock").read_text())
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
    for url, md5, name in items:
        script.append(f'if [ ! -f {shlex.quote(name)} ]; then '
                      f'wget -q --tries=3 -O {shlex.quote(name)}.part {shlex.quote(url)} '
                      f'&& mv {shlex.quote(name)}.part {shlex.quote(name)} '
                      f'|| {{ rm -f {shlex.quote(name)}.part; echo "@@FETCH-FAIL {name}"; }}; fi')
        script.append(f'echo "@@MD5 {name} $(md5sum {shlex.quote(name)} 2>/dev/null '
                      f'| cut -d" " -f1) {md5}"')
    rc, out = guest_run(wrap("\n".join(script), "host-root"), timeout=7200)
    bad = []
    for line in out.splitlines():
        if line.startswith("@@MD5"):
            _, name, got, want = line.split()
            if got != want:
                bad.append(name)
    (EVIDENCE / "sources").mkdir(parents=True, exist_ok=True)
    (EVIDENCE / "sources" / "fetch.log").write_text(out)
    print(f"fetched {len(items)} files, checksum mismatches: {bad or 'none'}")
    return bad, len(items)


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
    ap.add_argument("cmd", choices=["pages", "run", "fetch", "status", "guest"])
    ap.add_argument("arg", nargs="?")
    ap.add_argument("--page")
    a = ap.parse_args()
    run_phase.only_page = a.page
    if a.cmd == "pages":
        for pg, t in pages_for(phases()[a.arg]):
            print(f"{pg:44} {t[:40]}")
    elif a.cmd == "run":
        rec = run_sources() if a.arg == "sources" else run_phase(a.arg)
        return 0 if rec["passed"] else 1
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
