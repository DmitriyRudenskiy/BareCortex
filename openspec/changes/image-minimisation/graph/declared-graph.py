"""Declared-build graph from base.lock, emitted as graphify extraction JSON.

Edges here are the ones base.lock itself asserts. No `requires` edge exists in
this file: those are read off a built image (design D3), and inventing one is the
failure mode the change exists to prevent.
"""
import json, pathlib, tomllib, collections

ROOT = pathlib.Path(".")
lock = tomllib.loads((ROOT / "base.lock").read_text())

nodes, links = {}, []
def node(nid, label, kind, src, **extra):
    if nid not in nodes:
        nodes[nid] = {"id": nid, "label": label, "file_type": kind, "source_file": src, **extra}
def edge(s, t, rel, src):
    links.append({"source": s, "target": t, "relation": rel, "confidence": "EXTRACTED",
                  "source_file": src, "weight": 1.0})

def pid(name): return "pkg:" + name.replace(" ", "_")
def vid(page): return "page:" + page.replace("/", "_").removesuffix(".html")
def did(patch): return "distfile:" + patch.split("/")[-1]

for p in lock["package"]:
    name, src = p["name"], "base.lock"
    node(pid(name), name, "package", src,
         **{"version": p.get("version", ""), "class": p.get("class", ""),
            "chapter": (p["pages"][0].split("/")[0] if p.get("pages") else "")})
    for page in p.get("pages", []):
        chapter = page.split("/")[0]
        node(vid(page), page, "book-page", src, **{"chapter": chapter})
        node("chapter:" + chapter, chapter, "chapter", src)
        edge(pid(name), vid(page), "built-by", src)
        edge(vid(page), "chapter:" + chapter, "declared-in", src)
    # page -> chapter is a property of the page, emitted once per page below
    for d in p.get("patches", []) + p.get("extra_patches", []):
        f = d.split("/")[-1]
        if not f.endswith((".patch", ".diff", ".tar", ".xz", ".gz", ".zip", ".tgz")):
            continue
        node(did(f), f, "distfile", src)
        edge(pid(name), did(f), "patched-by", src)

seen, deduped = set(), []
for l in links:
    k = (l["source"], l["target"], l["relation"])
    if k not in seen:
        seen.add(k); deduped.append(l)
links = deduped

out = {"nodes": list(nodes.values()), "links": links,
       "meta": {"schema": "graphify-extraction/v1", "project": "BareCortex base.lock",
                "generator": "declared-build graph from base.lock",
                "edges_are": "declared by base.lock only; no ELF `requires` edge — none exists until an image is scanned"}}
(ROOT / "openspec/changes/image-minimisation/graph/declared.json").write_text(json.dumps(out, indent=2))

c = collections.Counter(l["relation"] for l in links)
print(f"packages={sum(1 for n in nodes.values() if n['file_type']=='package')} "
      f"pages={sum(1 for n in nodes.values() if n['file_type']=='book-page')} "
      f"distfiles={sum(1 for n in nodes.values() if n['file_type']=='distfile')} "
      f"nodes={len(nodes)} links={len(links)} {dict(c)}")
