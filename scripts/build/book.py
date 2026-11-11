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
BLOCK_RE = re.compile(r'(<p>.*?</p>)?\s*<pre class="userinput">(.*?)</pre>', re.S)
TAG_RE = re.compile(r"<[^>]*>")


def page(url_path):
    """A page of the pinned edition, cached — the build refetches nothing twice."""
    out = CACHE / (url_path.replace("/", "+") + ".html")
    if not out.is_file():
        out.parent.mkdir(parents=True, exist_ok=True)
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
    for intro, body in BLOCK_RE.findall(p):
        cmd = _clean(body)
        if not cmd:
            continue
        why = " ".join(html.unescape(TAG_RE.sub(" ", intro or "")).split()) if intro else ""
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
cd $SRCDIR
before=$(ls -A)
case {archive} in
  *.zip) unzip -q {archive} ;;
  *) tar -xvf {archive} > $LOG/unpack.log ;;
esac
newdir=$(comm -13 <(echo "$before" | tr ' ' '\\n' | sort) <(ls -A | sort) | head -1)
test -n "$newdir" || {{ echo "UNPACK PRODUCED NO DIRECTORY: {archive}"; exit 1; }}
echo "UNPACKED: $newdir"
cd "$newdir"
"""

WRAPPER_POST = """cd $SRCDIR
echo "CLEANUP: $newdir"
rm -rf "$newdir"
"""


def wrapper(archive):
    return WRAPPER_PRE.format(archive=html.unescape(archive)) if archive else ""


def wrapper_post():
    return WRAPPER_POST
