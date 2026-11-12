#!/usr/bin/env python3
"""The pinned book, as an executable source of instructions.

`base.lock` records what to download and which pages build it. This reads those
pages and returns the commands they contain, in the order a builder would type
them, with the sentence that introduces each block. Nothing here retypes a
command: the book is the recipe, and this is the kitchen.

The block markup is `<pre class="userinput"><kbd class="command">…</kbd></pre>`.
Every command a page tells the builder to run appears in one, including the
`patch -Np1` lines, so a package's recipe is its blocks in document order.

Two things the pages do not contain, and the book states once for the whole
build in `partintro/generalinstructions.html`: the unpack, and the `cd` into the
directory the unpack created. `wrapper()` is that synopsis, made executable.
"""

import html
import pathlib
import re
import urllib.request

BASE = "https://www.linuxfromscratch.org/~zeckma/openrc/lfs/13.0"
CACHE = pathlib.Path(__file__).resolve().parent.parent.parent / ".book-cache"

PRE_RE = re.compile(r'<pre class="userinput">(.*?)</pre>', re.S)
# The sentence that introduces a block, and only that sentence: a paragraph that
# ends here, with nothing but whitespace and closing tags between it and the
# commands. The first version of this let `<p>.*?</p>` run across the document,
# and a `<p>` in the navigation footer then swallowed the block that followed it —
# silently, because the block count still looked plausible.
BLOCK_RE = re.compile(r'<pre class="userinput">(.*?)</pre>', re.S)
INTRO_RE = re.compile(r'<p>((?:(?!</p>).)*?)</p>((?:\s|</?[a-zA-Z][^>]*>)*)$', re.S)
TAG_RE = re.compile(r"<[^>]*>")


def page(url_path):
    """A page of the pinned edition, cached — the build refetches nothing twice.

    The cache is valid for exactly one edition, and the file names say nothing
    about which: `chapter05/binutils-pass1.html` exists in every edition of the
    book. So the first fetch records the base URL the cache was filled from, and
    any later build against a different base fails instead of quietly mixing two
    books' commands into one build. Pointing the build at a new edition is a
    deliberate act: delete the cache.
    """
    out = CACHE / (url_path.replace("/", "+") + ".html")
    marker = CACHE / "BASE"
    if marker.is_file() and marker.read_text().strip() != BASE:
        raise SystemExit(
            f"book cache was filled from {marker.read_text().strip()!r}, this build asks "
            f"{BASE!r}; run: rm -rf {CACHE} — the cache holds one edition's pages")
    if not out.is_file():
        out.parent.mkdir(parents=True, exist_ok=True)
        marker.write_text(BASE + "\n")
        with urllib.request.urlopen(f"{BASE}/{url_path}", timeout=60) as r:
            out.write_text(r.read().decode("utf-8", "replace"))
    return out.read_text()


def text(url_path):
    """A page as the words on it, for reading the prose that carries a rule."""
    return " ".join(html.unescape(TAG_RE.sub(" ", page(url_path))).split())


def _clean(s):
    return "\n".join(l.rstrip() for l in html.unescape(TAG_RE.sub("", s)).splitlines()
                     if l.strip())


def commands(url_path):
    """[(introducing sentence, command text)] in document order.

    The sentence is kept because it is the evidence: `Compile the package:` next
    to `make` is what makes a log readable by someone who did not type it, and
    the sentences that say a test is known to fail sit in the same place.
    """
    p = page(url_path)
    out = []
    for m in BLOCK_RE.finditer(p):
        cmd = _clean(m.group(1))
        if not cmd:
            continue
        intro = INTRO_RE.search(p[max(0, m.start() - 4000):m.start()])
        why = " ".join(html.unescape(TAG_RE.sub(" ", intro.group(1))).split()) if intro else ""
        # The nav footer's <p> can precede the first block; that is not an intro.
        if "Prev" in why and "Next" in why:
            why = ""
        out.append((why, cmd))
    return out


def notes(url_path):
    """The page's stated expectations — known test failures, verification steps.

    Chapter 4's `About the Test Suites` says a failed test is a fact to record,
    and each package page names its own known failures. Those sentences are what
    a phase record compares a nonzero exit against, so a suite that was expected
    to fail one test does not read as a broken build.
    """
    p = page(url_path)
    body = p[p.find('<div class="sect'):] if '<div class="sect' in p else p
    out = []
    for m in re.finditer(r'<div class="(note|warning|important|tip)"[^>]*>(.*?)</div>\s*</div>',
                         body, re.S):
        t = " ".join(html.unescape(TAG_RE.sub(" ", m.group(2))).split())
        if len(t) > 20:
            out.append(f"{m.group(1)}: {t}")
    for m in re.finditer(r"<p>(.*?)</p>", body, re.S):
        t = " ".join(html.unescape(TAG_RE.sub(" ", m.group(1))).split())
        if re.search(r"known to fail|fails?\b|not\b.*test|test.*fail", t, re.I) and len(t) > 30:
            out.append(t)
    return out


# The book's synopsis, made executable. `tar -xvf` and nothing else: the same
# page warns that `cp -R` destroys timestamps and the build, and the directory
# to enter is discovered from what the extraction created rather than guessed
# from the archive name, because for several packages they differ.
WRAPPER_PRE = """set -e
cd $BCT_SRCDIR
# The book's general instructions keep a log per package; $LOG is that directory,
# defaulted here rather than inherited, because the page runs in `env -i` and
# nothing outside the build sets it. A redirect and not a pipe: the page script
# runs with -e, and a pipeline would report tee's exit code instead of tar's.
LOG="${{LOG:-$BCT_SRCDIR/logs}}"
mkdir -p "$LOG"
# A page that fails leaves its extracted tree behind: the script dies at the
# failing command and the cleanup at the end never runs. Building again on that
# tree would happen in a half-configured source directory, which is not the book's
# environment, and the diff below would find no new directory. So the stale tree
# goes first; the log of the run that failed stays where it is.
candidate={archive}
candidate=${{candidate%.tar.*}}
candidate=${{candidate%.zip}}
candidate=${{candidate%.tar}}
test "$candidate" = {archive} && candidate=""
before=$(ls -A)
if test -n "$candidate" && test -d "$candidate"; then
  echo "STALE: $candidate is left from a run that failed; unpacking fresh"
  rm -rf "$candidate"
  before=$(printf '%s\\n' $before | grep -vx "$candidate" | tr '\\n' ' ')
fi
case {archive} in
  *.zip) unzip -q {archive} ;;
  *) tar -xvf {archive} > "$LOG/{archive}.unpack.log" ;;
esac
newdir=$(comm -13 <(echo "$before" | tr ' ' '\\n' | sort) <(ls -A | sort) | head -1)
if test -z "$newdir"; then
  # The stale tree above is found by the name the archive was downloaded under,
  # and for Tcl those two disagree: tcl8.6.17-src.tar.gz extracts to tcl8.6.17,
  # so the tree a failed attempt left behind was neither noticed nor removed, the
  # extraction overwrote into it, and the diff saw nothing new. Ask the archive
  # what it holds, throw the half-built tree away, extract again — the book's
  # environment is a fresh one, not the one a dead run left behind.
  newdir=$(tar -tf {archive} 2>/dev/null | head -1 | cut -d/ -f1)
  if test -n "$newdir" && test -d "$newdir"; then
    echo "STALE: $newdir is left from a run that failed; unpacking fresh"
    rm -rf "$newdir"
    tar -xvf {archive} >> "$LOG/{archive}.unpack.log"
  else
    newdir=""
  fi
fi
test -n "$newdir" || {{ echo "UNPACK PRODUCED NO DIRECTORY: {archive} (expected $candidate)"; exit 1; }}
echo "UNPACKED: $newdir"
cd "$newdir"
"""

WRAPPER_POST = """# The runner's own name, not $SRCDIR: the book uses SRCDIR as a scratch
# variable of its own (Tcl's second block ends with `unset SRCDIR`, which is what
# made the cleanup below die on an unbound variable after every block of Tcl had
# passed), and Vim's top-level configure stub does `cd "${SRCDIR:-src}"`, so a
# shared name means either the package wins or the page does.
cd "${BCT_SRCDIR:-.}"
echo "CLEANUP: $newdir"
rm -rf "$newdir"
"""


def wrapper(archive):
    return WRAPPER_PRE.format(archive=html.unescape(archive)) if archive else ""


def wrapper_post():
    return WRAPPER_POST


NEXT_RE = re.compile(r'class="next">\s*<a[^>]*href="([^"]+)"', re.S)


def sequence(chapter):
    """Every page of a chapter, in the order the book links them.

    Not the index: the 13.0-openrc edition's chapter 8 has a page for
    Libpipeline-1.5.8 — `kbd.html`'s Next link is `libpipeline.html`, and man-db's
    `configure` stops with `Package requirements (libpipeline >= 1.5.0) were not
    met` without it — and `index.html` does not list it. Anything that asks the
    book what a chapter contains has to follow the book's own links, or it builds
    a system missing a library and finds out pages later.
    """
    import posixpath
    ch = f"chapter{chapter}"
    out, cur, seen = [], f"{ch}/introduction.html", set()
    while cur and cur not in seen:
        seen.add(cur)
        try:
            m = NEXT_RE.search(page(cur))
        except OSError:
            break  # a page that will not load is a gap to report, not a crash here
        if not m:
            break
        cur = posixpath.normpath(posixpath.join(posixpath.dirname(cur), m.group(1)))
        if not cur.startswith(ch + "/"):
            break
        out.append(cur)
    return out
