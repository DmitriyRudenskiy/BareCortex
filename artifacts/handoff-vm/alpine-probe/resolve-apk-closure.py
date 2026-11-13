#!/usr/bin/env python3
"""Alpine v3.24 index: resolve a seed set to the package closure, the way apk would.

Edges are Alpine's own: `D:` (depend) and `p:` (provides, including the so: virtuals
that apk uses to bind a DT_NEEDED soname to a package). Written for the BareCortex
Alpine probe; prints a closure table and the .apk filenames to fetch.
"""
import json, pathlib, sys, collections

idx_path, out_dir, seed = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), sys.argv[3:]
pkgs, cur = [], None
for line in idx_path.read_text().split("\n"):
    if not line.strip():
        cur = None
        continue
    if not line.startswith(("P:", "V:", "A:", "S:", "D:", "p:", "c:", "T:")):
        continue
    k, v = line[0], line[2:]
    if k == "P":
        cur = {"name": v}
        pkgs.append(cur)
    elif cur is not None:
        cur[k] = v

by_provide = collections.defaultdict(list)
for p in pkgs:
    by_provide[p["name"]].append(p)
    for prov in p.get("p", "").split():
        base = prov.split("=")[0]
        by_provide[base].append(p)

def pick(dep):
    """apk's rule, reduced: a versioned or virtual dep is satisfied by any provider;
    when several do, apk takes the first — record the ambiguity instead of hiding it."""
    name, _, ver = dep.partition("=")
    cands = by_provide.get(name, [])
    if ver:
        cands = [c for c in cands if c.get("V", "") >= ver] or cands
    return name, cands

seen, stack, edges, ambiguous = {}, list(seed), [], []
while stack:
    dep = stack.pop()
    name, cands = pick(dep)
    if not cands:
        ambiguous.append((dep, "NO PROVIDER"))
        continue
    if len({c["name"] for c in cands}) > 1:
        ambiguous.append((dep, " ".join(sorted({c["name"] for c in cands}))))
    p = cands[0]
    if p["name"] in seen:
        continue
    seen[p["name"]] = p
    for d in p.get("D", "").split():
        stack.append(d)
        edges.append((p["name"], d))

out = {n: {"version": p.get("V", ""), "arch": p.get("A", ""), "size": int(p.get("S", "0") or 0),
           "depend": p.get("D", "").split(), "provides": p.get("p", "").split(),
           "desc": p.get("T", ""),
           # v3.24 names an apk <name>-<version>.apk; the architecture lives in the
           # repository path. The old <name>-<version>-<arch>.apk is a 404.
           "apk": f"{n}-{p.get('V','')}.apk"}
       for n, p in seen.items()}
out_dir.mkdir(parents=True, exist_ok=True)
(out_dir / "closure.json").write_text(json.dumps({"seed": seed, "packages": out,
                                                  "edges": edges, "ambiguous": ambiguous}, indent=1))
total = sum(p["size"] for p in out.values())
print(f"seed {'+'.join(seed)}: {len(out)} packages, {total:,} bytes declared installed size")
for n, p in sorted(out.items(), key=lambda kv: -kv[1]["size"]):
    print(f"  {n:24} {p['size']:>9,}  {p['version']:16} {p['desc'][:38]}")
print("\ndepends on something outside the closure / ambiguous:")
for d, why in sorted(set(ambiguous)):
    print(f"  {d:34} {why}")
print("\napk files:")
for n in sorted(out):
    print(out[n]["apk"])
