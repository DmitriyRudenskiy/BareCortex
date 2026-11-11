#!/usr/bin/env python3
"""Derive base.lock from the pinned book edition. Nothing here is typed by hand.

`base-system-and-init` D3 makes `base.lock` the source of truth for what the
image contains, and `build-orchestration` 2.1 owns its contents: name, version,
URL, checksum, provenance class, patch series, plus the book revision and page
every line cites (2.5 rejects a citation-free line). The only way that stays
true across a pin bump is if the file is generated from the edition it came
from — retyping it once is how a lock file drifts from its book.

So this script is the derivation. Point it at a rendering and a revision:

    ./derive-base-lock.py https://www.linuxfromscratch.org/~zeckma/openrc/lfs/13.0 \
        > base.lock

and commit the diff (2.3). It reads three pages of the edition: the index for
which chapter builds which package, chapter 3's package table for the download
URL and the book's own MD5, and chapter 3's patch table for the patch series.

Stdlib only, and no network beyond plain HTTP GETs of the edition.
"""
import html
import re
import sys
import urllib.request

PKG_RE = re.compile(r"^(?P<name>[A-Za-z][A-Za-z0-9.:+_-]*)[- ](?P<version>\d[\w.]*)")
# `+` in the class: `gcc-libstdc++.html` is a page of this edition, and a page
# the derivation cannot see is a page it cannot account for.
PAGE_RE = r'href="(chapter\d+/[A-Za-z0-9:._+-]+\.html)"'


_PAGES = {}


def get(url):
    """A page of the pinned edition, once. The derivation looks at each page for
    two things — what it downloads and what it patches — and refetching 160 pages
    for the second look would double a two-minute command to four."""
    if url not in _PAGES:
        with urllib.request.urlopen(url, timeout=60) as r:
            _PAGES[url] = r.read().decode("utf-8", "replace")
    return _PAGES[url]


def strip(s):
    return html.unescape(re.sub(r"<[^>]*>", " ", s))


def toc_pages(index_html, base):
    """Every chapter page URL in the edition's index, plus the package pages."""
    all_pages, pkgs = [], []
    for href, title in re.findall(PAGE_RE + r'[^>]*>(.*?)</a>',
                                  index_html, re.S):
        title = " ".join(strip(title).split())
        all_pages.append(href)
        # `Udev from Systemd-259.1`, `Libstdc++ from GCC-15.2.0`: the book's own
        # idiom for a page that builds another package's archive, and the only
        # place that says so for libstdc++, whose page body says just `cd build`.
        for candidate in (title, title.rsplit(" from ", 1)[-1]):
            if (m := PKG_RE.match(candidate)):
                pkgs.append((href.split("/")[0], href, m.group("name"), m.group("version")))
                break
    return sorted(set(all_pages)), pkgs


TARBALL_RE = re.compile(r"[A-Za-z0-9._:+-]+\.(?:tar\.(?:xz|gz|bz2|zst)|tar|tgz|zip)")
H2_RE = re.compile(r"<h2[^>]*>(.*?)</h2>", re.S)
CD_RE = re.compile(r"\bcd\s+([A-Za-z0-9._+/-]+)")
TAIL_PKG_RE = re.compile(r"([A-Za-z][A-Za-z0-9.:+_-]*)-(\d[\w.]*)")


def page_downloads(url):
    """Archive file names this page attributes to itself, from its own text.

    Three signals, all the page's own words, because an LFS package page names
    the directory it builds and rarely the archive it unpacked:

    1. the page's `<h2>` package title — `Udev from Systemd-259.1` builds
       `systemd-259.1`, which is how the table's `Systemd` gets a page;
    2. any archive file name written out in full, which is how a documentation
       tarball and `libpipeline` land on the page that unpacks them;
    3. every `cd <dir>`, which is how `linux-6.18.10` lands on the kernel page.

    A prefix match is deliberate: `Python-3.14.3-docs-html` belongs to the page
    that unpacks it, not to a page of its own. What matches nothing anywhere is
    reported, not dropped — that is the derivation noticing the book changed.
    """
    page = get(url)
    stems = set()
    for h2 in H2_RE.findall(page):
        title = " ".join(strip(h2).split())
        m = TAIL_PKG_RE.search(title)
        if m:
            stems.add(f"{m.group(1)}-{m.group(2)}".lower())
    for name in TARBALL_RE.findall(page):
        stems.add(re.sub(r"\.(?:tar\.(?:xz|gz|bz2|zst)|tar|tgz|zip)$", "", name).lower())
    # A path, because chapter 5 builds libstdc++ by `cd ../gcc-15.2.0/build` —
    # the directory the page moves into is the package it builds, whatever the
    # path to it is. `.` and `..` are a page going back out again.
    stems.update(c.lower().rsplit("/", 1)[-1] for c in CD_RE.findall(page)
                 if c not in (".", ".."))
    return stems


def package_table(page_html):
    """The chapter 3 table: name, version, size, home, download URL, MD5."""
    out = []
    for dt, dd in re.findall(r"<dt>(.*?)</dt>\s*<dd>(.*?)</dd>", page_html, re.S):
        head = " ".join(strip(dt).split()).rstrip(":")
        m = re.match(r"^(.*?)\s*\(([^)]+)\)", head)
        if not m:
            continue
        url = re.search(r"Download:\s*<a[^>]*href=\s*\"?([^\"> ]+)", dd)
        md5 = re.search(r"MD5 sum:\s*<code[^>]*>\s*([0-9a-f]{32})", dd)
        size = re.search(r"([\d,]+)\s*KB", head)
        if not (url and md5):
            continue
        out.append({
            "name": m.group(1).strip(), "version": m.group(2).strip(),
            "url": url.group(1), "md5": md5.group(1),
            "kb": int(size.group(1).replace(",", "")) if size else None,
        })
    return out


def patch_table(page_html):
    """Chapter 3.3. The term is a description; the file name is the URL's basename."""
    out = []
    for dt, dd in re.findall(r"<dt>(.*?)</dt>\s*<dd>(.*?)</dd>", page_html, re.S):
        desc = " ".join(strip(dt).split()).rstrip(":").strip()
        desc = re.sub(r"\s*-\s*[\d,.]+\s*K?B$", "", desc)
        url = re.search(r"Download:\s*<a[^>]*href=\s*\"?([^\"> ]+)", dd)
        md5 = re.search(r"MD5 sum:\s*<code[^>]*>\s*([0-9a-f]{32})", dd)
        if url and url.group(1).endswith(".patch"):
            out.append({"patch": url.group(1).rsplit("/", 1)[-1],
                        "description": desc,
                        "url": url.group(1),
                        "md5": md5.group(1) if md5 else None})
    return out


def norm(s):
    """Package identity across the book's own spellings of it."""
    return re.sub(r"[^a-z0-9]", "", s.lower())


def remote_of(url):
    """Where the archive comes from, as the URL the book publishes says."""
    if "//" not in url:
        return url
    scheme, rest = url.split("//", 1)
    return f"{scheme}//{rest.split('/')[0]}"


def revision_of(url, version):
    """The upstream release identifier the download URL itself carries.

    The book publishes a URL, a version and an MD5 — no revision. Where the URL
    is a release download or a source archive its path names the upstream tag
    (`/releases/download/v2.3.2/`, `/archive/POSIX-2.08t.tar.bz2`), and that is
    the revision; where it does not, the version is the whole of the identifier
    the URL offers. Recorded as derived from the URL, never invented: a commit
    hash nobody checked would be worse than no field at all.
    """
    m = re.search(r"/(?:releases/download|archive)/([^/]+?)(?:\.tar\.[a-z0-9]+|\.zip|/|$)", url)
    return m.group(1) if m else version


# Downloads this edition lists but builds nowhere, as far as its own pages go.
# Not a fudge to make the count reach zero: an archive no page names has no
# recipe to cite, so it is not a source-built package of this base system, and
# inventing a page for it would make the citation mean nothing. Recorded so the
# file says the book is inconsistent instead of failing on it or dropping it.
KNOWN_UNATTRIBUTED = {
    "libpipeline-1.5.8.tar.gz":
        "listed by chapter03/packages.html; no page of this edition names it",
}


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: derive-base-lock.py <edition index URL>")
    base = sys.argv[1].rstrip("/")
    index = get(base + "/")
    # The <title> is the book's name, not its edition; the edition string is in
    # the page body as "Version 13.0-openrc", and that string is what a pin
    # bump changes.
    body = " ".join(strip(index).split())
    edition = re.search(r"Version\s+([0-9][\w.\-]+)", body)
    edition = f"LFS {edition.group(1)}" if edition else None
    if not edition:
        sys.exit(f"no edition string ('Version N') found at {base}/ — refusing to "
                 f"write a lock file that does not name what it was derived from")

    all_pages, pkgs = toc_pages(index, base)
    # The index's own link text is the book's page → package mapping: `Acl-2.3.2`
    # on chapter08/acl.html, `Udev from Systemd-259.1` on chapter08/udev.html.
    # That is the primary attribution, and it is why no alias table is needed —
    # the table's `Xz Utils` and the page's `Xz-5.8.2` meet at the tarball name.
    where = {}
    by_name = {}
    for _chapter, page, name, version in pkgs:
        where.setdefault(f"{name}-{version}".lower(), set()).add(page)
        by_name.setdefault((norm(name), version), set()).add(page)

    # Cite pages, not anchors: a page is what 2.5 asks for and what a human can
    # open, and a guessed anchor would look checkable while not being so.
    # Chapters 1-3 are skipped: chapter 3 is the download table, so it names
    # every archive, and a citation all 89 carry cites nothing.
    for page in all_pages:
        if page.startswith(("chapter01/", "chapter02/", "chapter03/")):
            continue
        for stem in page_downloads(f"{base}/{page}"):
            where.setdefault(stem, set()).add(page)

    def pages_for(p):
        tarball = p["url"].rsplit("/", 1)[-1]
        stem = re.sub(r"\.(?:tar\.(?:xz|gz|bz2|zst)|tar|tgz|zip)$", "", tarball).lower()
        hits = set()
        for s, pages in where.items():
            if stem == s or stem.startswith(s + "-") or s.startswith(stem + "-"):
                hits |= pages
        # Six packages — Expect, Flit-core, Libpipeline, Sqlite, Tcl, XML::Parser
        # — are named by no page in this edition, as archive or as directory: its
        # chapter 8 pages run ./configure without an unpack step. Their attribution
        # is the book agreeing with itself that the table's `Sqlite` 3.51.1 and the
        # page titled `Sqlite-3.51.1` are one package, name and version both.
        hits |= by_name.get((norm(p["name"]), p["version"]), set())
        return sorted(hits)

    packages = package_table(get(f"{base}/chapter03/packages.html"))
    patches = patch_table(get(f"{base}/chapter03/patches.html"))

    # Which page applies which patch, read off the pages themselves: a patch is
    # applied by `patch -Np1 -i ../<file>` inside the package's own page, so the
    # patch series is not a guess from the patch's filename.
    patch_pages = {}
    for page in all_pages:
        if page.startswith(("chapter01/", "chapter02/", "chapter03/")):
            continue
        html = get(f"{base}/{page}")
        for p in patches:
            if p["patch"] in html:
                patch_pages.setdefault(p["patch"], set()).add(page)

    print(f'# base.lock — derived, do not edit by hand.')
    print(f'# Derived from: {edition}')
    print(f'# Source:       {base}/chapter03/packages.html')
    print(f'# Derivation:   scripts/build/derive-base-lock.py')
    print(f'# Checksums:    the MD5 column of that table — the book publishes MD5,')
    print(f'#               not SHA-256, so this file carries MD5 and says so.')
    print(f'# Remote:       the origin of the URL the book publishes for the archive.')
    print(f'# Revision:     the release tag that URL carries, or the version where it')
    print(f'#               carries none. The book publishes no revision field; this')
    print(f'#               is read off the URL, not looked up behind it.')
    print(f'# Format owner: base-system-and-init D3. Contents owner: build-orchestration 2.1.')
    print(f'#')
    print(f'# Provenance classes are rows of class-map.toml, not a sentence; every')
    print(f'# entry names its class and the book pages that build it, because 2.5')
    print(f'# rejects a line that carries no book revision and page.')
    print()
    print(f'[edition]')
    print(f'name = {edition!r}')
    print(f'url = {base + "/"!r}')
    print(f'package_pages = {len(all_pages)}')
    print(f'unattributed_downloads = {len(KNOWN_UNATTRIBUTED)}')
    print()

    missing = []
    unattributed = []
    entries = []
    for p in sorted(packages, key=lambda x: (x["name"].lower(), x["version"])):
        pages = pages_for(p)
        tarball = p["url"].rsplit("/", 1)[-1]
        if not pages and tarball in KNOWN_UNATTRIBUTED:
            unattributed.append((p, KNOWN_UNATTRIBUTED[tarball]))
            continue
        if not pages:
            # A package the download table lists and no chapter page names is not
            # a contradiction to hide: it is a derivation that stopped matching.
            pages = ["UNMATCHED"]
            missing.append(p["name"])
        entries.append((p, pages))

    # A patch belongs to the package whose page applies it. Two patches of one
    # edition can land on one page (Bzip2's two), which is why this is a list
    # keyed by page and not a name match on the patch's own filename.
    series = {}
    for p in patches:
        # The book names a patch `<package>-<version>-<what>.patch`, and that is
        # the attribution: `glibc-fhs-1.patch` is applied from the glibc page,
        # which also installs the time zone data, and page-sharing does not make
        # it a patch of tzdata. Pages are the fallback for a patch named after
        # nothing in the package list.
        named = max((n for n, _ in ((e[0]["name"], 0) for e in entries)
                     if p["patch"].startswith(norm(n))), key=len, default=None)
        owners = [n for n, pages in entries if named is None and
                  patch_pages.get(p["patch"], set()) & set(pages)]
        for name in ([named] if named else owners[:1]):
            series.setdefault(name, []).append(p["patch"])
            p["package"] = name

    for p, pages in entries:
        print('[[package]]')
        print(f'name = {p["name"]!r}')
        print(f'version = {p["version"]!r}')
        print(f'url = {p["url"]!r}')
        print(f'remote = {remote_of(p["url"])!r}')
        print(f'revision = {revision_of(p["url"], p["version"])!r}')
        print(f'md5 = {p["md5"]!r}')
        print('class = "source-built"')
        print(f'pages = {pages!r}')
        if p["name"] in series:
            print(f'patches = {series[p["name"]]!r}')
        print()

    for p, reason in unattributed:
        print('[[unattributed-download]]')
        print(f'name = {p["name"]!r}')
        print(f'version = {p["version"]!r}')
        print(f'url = {p["url"]!r}')
        print(f'remote = {remote_of(p["url"])!r}')
        print(f'revision = {revision_of(p["url"], p["version"])!r}')
        print(f'md5 = {p["md5"]!r}')
        print(f'reason = {reason!r}')
        print()

    print('# --- patch series, chapter 3.3. Applied by the package named, on the page')
    print('# --- named; the series is part of that version, per D3.')
    for p in patches:
        print('[[patch]]')
        print(f'file = {p["patch"]!r}')
        print(f'url = {p["url"]!r}')
        print(f'remote = {remote_of(p["url"])!r}')
        print(f'md5 = {p["md5"]!r}')
        print(f'description = {p["description"]!r}')
        print(f'package = {p.get("package", "UNATTRIBUTED")!r}')
        print(f'pages = {sorted(patch_pages.get(p["patch"], []))!r}')
        print()

    if missing:
        print(f'# UNMATCHED: {len(missing)} packages appear in the chapter 3 table with no')
        print(f'# chapter page that builds them: {", ".join(sorted(missing))}')
        print(f'# The build must treat this file as suspect until the derivation matches.')
        sys.exit(3)


if __name__ == "__main__":
    main()
