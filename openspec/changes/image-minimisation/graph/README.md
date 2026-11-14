# The declared-build graph, and what it proved

Built during proposal, from `base.lock` only.

## Regenerate

From the repository root — the generator reads `./base.lock` and writes next to
this file:

```sh
python3 openspec/changes/image-minimisation/graph/declared-graph.py
```

Then build and query with graphify (0.9.46, a build-host tool via `uv tool`, never
an image package). Its CLI looks for `graphify-out/graph.json` relative to the
current directory, so run these from here:

```sh
"$(uv tool dir)/graphifyy/bin/python" -c "
import json
from graphify.build import build_from_json
from graphify.export import to_json
g = build_from_json(json.loads(open('declared.json').read()), directed=True)
to_json(g, {}, 'graphify-out/graph.json')"   # communities is positional; {} for none

graphify affected "page:chapter08_Python" --relation built-by
graphify god-nodes
graphify query "which packages does chapter08/udev.html build"
```

`declared.json` and `graph.json` are **not committed** — `.gitignore` ignores
`*.json` repo-wide under "Datasets (too large for git)". Both are one command away.

## What the artifact is

`declared.json` is graphify's `graphify-extraction/v1` shape — 210 nodes, 240
edges, every edge `confidence: EXTRACTED` with `source_file: base.lock`. The
relations are the ones the lock asserts: a package `built-by` a book page, a page
`declared-in` a chapter, a package `patched-by` a distfile (five packages carry a
patch: Bzip2, Coreutils, Expect, Glibc, Kbd).

**There is no `requires` edge in this file, and there cannot be one here**: those
edges are read off a built image (design D3), and inventing them is the failure
mode this change exists to prevent. Treat this as the pipeline proof and the
declared view, not as the dependency graph the trim decisions rest on.

## What it found, and what it changed

Eight pages build more than one package, and 15 of the 89 declared packages share
a page with another:

| Page | Packages it builds |
| --- | --- |
| `chapter05/gcc-pass1.html` | GCC, GMP, MPC, MPFR |
| `chapter06/gcc-pass2.html` | GCC, GMP, MPC, MPFR |
| `chapter07/Python.html` | Python, Python Documentation |
| `chapter08/Python.html` | Python, Python Documentation |
| `chapter08/glibc.html` | Glibc, Time Zone Data |
| `chapter08/sqlite.html` | Sqlite, Sqlite Documentation |
| `chapter08/tcl.html` | Tcl, Tcl Documentation |
| `chapter08/udev.html` | Systemd, Systemd Man Pages, Udev-lfs Tarball |

The skip mechanism this change proposed is **page**-granular and the verdict is
**package**-granular, so for those 15 packages `absent` is not available: skipping
`chapter08/tcl.html` removes Tcl and Tcl Documentation together, and skipping
`chapter05/gcc-pass1.html` would remove GMP, MPC and MPFR along with the first-pass
compiler. Design D8 is the decision that followed, and task 6.8's first subject
changed from five documentation packages to the one of them that is a page's sole
product (`Man-pages`) — the other four are `build-only` by construction, not by
choice.

Two smaller facts fell out. `Python Documentation` is built by **two** pages, in
chapter 7 and chapter 8, so a skip would have to catch both. And GMP, MPFR and MPC
each have a chapter 8 page of their own *in addition* to the two GCC pass pages —
so a package can be built by several pages, and "the page that builds it" is not
always one.
