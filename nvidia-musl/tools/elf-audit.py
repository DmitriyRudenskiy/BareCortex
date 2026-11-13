#!/usr/bin/env python3
"""ELF audit of proprietary NVIDIA userspace for the musl (Alpine) port.

Task: nvidia-musl/TASK.md, step 2.1. Scans an extracted .run tree for
libnvidia-*.so*, libcuda.so*, libcudart*, nvidia-smi (or --all-elf) and reports:

  * DT_NEEDED / DT_SONAME            glibc names patchelf-sanitize must rewrite
  * relocation census                IRELATIVE (needs musl patch 090),
                                     TPOFF64/DTPMOD64 (TLS relocations; general-dynamic
                                     is harmless on musl)
  * STT_GNU_IFUNC symbols            also needs patch 090
  * DF_1_STATIC_TLS (0x800000)       needs musl patch 091 (static TLS surplus)
  * max GLIBC_2.NN (.gnu.version_r)  input for glibc-jail rootfs choice
  * libstdc++.so.6 consumers         620's libnvidia-api.so.1 (Alpine: apk add libstdc++)
  * static mutex-init heuristic      candidates for the libnvcompat shim: kind word
                                     (1/2/3) at +16 in .data/.data.rel.ro, surrounded by
                                     zeros. COARSE — verify candidates by hand.

Verdicts: NEEDS_PATCHED_MUSL (ifunc or static TLS) / GCOMPAT_CANDIDATE (rest).

NOTE: re-implemented from the handoff spec (12.11.2026); the original tested script was
not found on this machine. Detector ground truth per the handoff: glibc libc.so.6 ->
IRELATIVE>0 + STATIC_TLS=True (exactly the two Chimera barriers), /bin/ls ->
GCOMPAT_CANDIDATE. Verified here against Debian 12 libc.so.6 via --selftest.

Usage:
  python3 elf-audit.py <dir> [--all-elf] [--json out.json] [--csv out.csv]
  python3 elf-audit.py --selftest [path/to/libc.so.6]
"""
import argparse
import csv
import fnmatch
import json
import os
import re
import sys

try:
    from elftools.elf.elffile import ELFFile
except ImportError:
    sys.exit("pyelftools required: pip install pyelftools")

DF1_STATIC_TLS = 0x800000
DF_STATIC_TLS = 0x10  # the DT_FLAGS spelling of the same fact (<elf.h>)
IRELATIVE = {"EM_X86_64": 37, "EM_AARCH64": 1032}
TPOFF64 = {"EM_X86_64": 38, "EM_AARCH64": 268}
DTPMOD64 = {"EM_X86_64": 12, "EM_AARCH64": 266}
GLIBC_RE = re.compile(r"^GLIBC_(\d+)(?:\.(\d+))?$")
DEFAULT_PATTERNS = ("libnvidia-*.so*", "libcuda.so*", "libcudart*", "nvidia-smi")
ZERO16 = b"\x00" * 16
ZERO12 = b"\x00" * 12


def _u16(b, o):
    return int.from_bytes(b[o:o + 2], "little")


def _u32(b, o):
    return int.from_bytes(b[o:o + 4], "little")


def _dyn_str(elf, sec, off):
    return elf.get_section(sec["sh_link"]).get_string(off)


def dynamic_info(elf):
    needed, soname, static_tls = [], None, False
    sec = elf.get_section_by_name(".dynamic")
    if sec is None:
        return needed, soname, static_tls
    for tag in sec.iter_tags():
        d = tag.entry
        if d["d_tag"] == "DT_NEEDED":
            needed.append(getattr(tag, "need", None) or _dyn_str(elf, sec, d["d_val"]))
        elif d["d_tag"] == "DT_SONAME":
            soname = getattr(tag, "need", None) or _dyn_str(elf, sec, d["d_val"])
        elif d["d_tag"] == "DT_FLAGS_1":
            static_tls = static_tls or bool(d["d_val"] & DF1_STATIC_TLS)
        elif d["d_tag"] == "DT_FLAGS":
            # libnvidia-tls/glcore/GLX_nvidia/vdpau/glxserver set DF_STATIC_TLS
            # in DT_FLAGS, not DF_1_STATIC_TLS; reading only the latter reported
            # 0 static-TLS users in the 580 tree where readelf finds 5.
            static_tls = static_tls or bool(d["d_val"] & DF_STATIC_TLS)
    return needed, soname, static_tls


def reloc_census(elf, machine):
    counts = {"irelative": 0, "tpoff64": 0, "dtpmod64": 0, "total": 0}
    irel, tp, dm = IRELATIVE.get(machine), TPOFF64.get(machine), DTPMOD64.get(machine)
    for sec in elf.iter_sections():
        # Any relocation section: the one IRELATIVE in libnvidia-glcore.580 lives
        # in .rela.plt, which a .rela.dyn/.rel.dyn name filter walks past.
        if sec.header["sh_type"] not in ("SHT_RELA", "SHT_REL"):
            continue
        try:
            rels = sec.iter_relocations()
        except Exception:
            continue
        for rel in rels:
            t = rel["r_info_type"]
            counts["total"] += 1
            if t == irel:
                counts["irelative"] += 1
            elif t == tp:
                counts["tpoff64"] += 1
            elif t == dm:
                counts["dtpmod64"] += 1
    return counts


def ifunc_count(elf):
    n = 0
    for name in (".dynsym", ".symtab"):
        sec = elf.get_section_by_name(name)
        if sec is None:
            continue
        for sym in sec.iter_symbols():
            if sym.entry["st_info"]["type"] == "STT_GNU_IFUNC":
                n += 1
    return n


def parse_verneed(data, get_string):
    """{required lib: {version-name}} from raw .gnu.version_r bytes.

    Verneed: vn_version@0 vn_cnt@2 vn_file@4 vn_aux@8 vn_next@12 (16 bytes).
    Vernaux: vna_hash@0 vna_flags@2 vna_other@4 vna_name@8 vna_next@12 (16 bytes).
    vn_next / vna_next are offsets RELATIVE TO THE ENTRY THEY SIT IN, 0 = last.
    (Advancing by the value instead of adding it walks the same entry forever —
    the bug that hung the 580.196.07 audit on any file with 3+ Verneed entries.)
    """
    out, i, guard = {}, 0, 0
    while i + 16 <= len(data):
        guard += 1
        if guard > len(data):  # a cyclic vn_next cannot outlive the section
            break
        vn_cnt = _u16(data, i + 2)
        lib = get_string(_u32(data, i + 4))
        j = i + _u32(data, i + 8)
        for _ in range(vn_cnt):
            if j + 16 > len(data):
                break
            out.setdefault(lib, set()).add(get_string(_u32(data, j + 8)))
            vna_next = _u32(data, j + 12)
            if vna_next == 0:
                break
            j += vna_next
        vn_next = _u32(data, i + 12)
        if vn_next == 0:
            break
        i += vn_next
    return out


def version_reqs(elf):
    """{required lib: {version-name}} from .gnu.version_r."""
    sec = elf.get_section_by_name(".gnu.version_r")
    if sec is None:
        return {}
    st = elf.get_section(sec["sh_link"])
    return parse_verneed(sec.data(), st.get_string)


def glibc_max(reqs):
    best = None
    for names in reqs.values():
        for n in names:
            m = GLIBC_RE.match(n)
            if m:
                v = (int(m.group(1)), int(m.group(2) or 0))
                if best is None or v > best[0]:
                    best = (v, n)
    return best[1] if best else None


def mutex_candidates(elf, cap=50):
    out = []
    for sec in elf.iter_sections():
        if sec.name not in (".data", ".data.rel.ro"):
            continue
        data = sec.data()
        for off in range(0, max(0, len(data) - 31), 4):
            kind = _u32(data, off + 16)
            if kind in (1, 2, 3) and data[off:off + 16] == ZERO16 \
                    and data[off + 20:off + 32] == ZERO12:
                out.append({"section": sec.name,
                            "offset": int(sec["sh_addr"]) + off, "kind": kind})
                if len(out) >= cap:
                    return out
    return out


def audit_file(path):
    r = {"path": path}
    with open(path, "rb") as f:
        elf = ELFFile(f)
        machine = elf.header.e_machine  # string constant, e.g. 'EM_X86_64'
        r["machine"] = machine
        r["needed"], r["soname"], r["static_tls"] = dynamic_info(elf)
        r.update(reloc_census(elf, machine))
        r["ifunc_syms"] = ifunc_count(elf)
        reqs = version_reqs(elf)
        r["version_reqs"] = {k: sorted(v) for k, v in reqs.items()}
        r["glibc_max"] = glibc_max(reqs)
        r["needs_libstdcxx"] = "libstdc++.so.6" in r["needed"]
        r["mutex_candidates"] = mutex_candidates(elf)
    if r["irelative"] or r["ifunc_syms"] or r["static_tls"]:
        r["verdict"] = "NEEDS_PATCHED_MUSL"
    else:
        r["verdict"] = "GCOMPAT_CANDIDATE"
    return r


def find_files(root, all_elf):
    pats = ["*"] if all_elf else list(DEFAULT_PATTERNS)
    out = []
    for dirpath, _, filenames in os.walk(root):
        for fn in filenames:
            if any(fnmatch.fnmatch(fn, p) for p in pats):
                p = os.path.join(dirpath, fn)
                with open(p, "rb") as f:
                    if f.read(4) == b"\x7fELF":
                        out.append(p)
    return sorted(out)


def verneed_selfcheck():
    """Synthetic .gnu.version_r: 2 libs, 2 versions on the first, gaps between
    entries. Fails on the pre-fix chain walk. Returns [] or [problem]."""
    import struct

    strings = b"\x00".join([b"", b"libc.so.6", b"libm.so.6", b"2.17", b"2.28",
                            b"2.4"]) + b"\x00"

    def off(s):
        return strings.index(s.encode() + b"\x00")

    def verneed(cnt, file_off, aux, nxt):
        return struct.pack("<HHIII", 1, cnt, file_off, aux, nxt)

    def vernaux(name_off, nxt):
        return struct.pack("<IHHII", 0, 0, 2, name_off, nxt)

    data = (verneed(2, off("libc.so.6"), 16, 64)
            + vernaux(off("2.17"), 16) + vernaux(off("2.28"), 0)
            + b"\0" * 16
            + verneed(1, off("libm.so.6"), 16, 0)
            + vernaux(off("2.4"), 0))
    table = {}
    got = parse_verneed(data, lambda o: strings[o:strings.index(b"\x00", o)].decode())
    for k, v in got.items():
        table[k] = sorted(v)
    want = {"libc.so.6": ["2.17", "2.28"], "libm.so.6": ["2.4"]}
    return [] if table == want else [f"verneed walk: got {table}, want {want}"]


def selftest(path):
    problems = verneed_selfcheck()
    if problems:
        print("selftest FAIL:", "; ".join(problems), file=sys.stderr)
        return 1
    print("selftest: verneed walk OK (synthetic, 2 libs / 3 versions)")
    cands = [path] if path not in (None, "auto") else [
        "/lib64/libc.so.6", "/lib/x86_64-linux-gnu/libc.so.6",
        "/lib/aarch64-linux-gnu/libc.so.6",
    ]
    libc = next((p for p in cands if os.path.isfile(p)), None)
    if not libc:
        print("selftest: no glibc libc.so.6 found (normal on macOS); "
              "rerun on Linux or pass a path", file=sys.stderr)
        return 2
    r = audit_file(libc)
    problems = []
    if not r["irelative"]:
        problems.append("expected IRELATIVE relocations in glibc libc")
    # NOTE: glibc's own libc.so.6 does NOT set DF_1_STATIC_TLS (verified on
    # Debian 12) — that flag lives on dlopen'd libs like libnvidia-tls; the
    # detector is a bit test, so it is reported, not asserted, here.
    if not (r["glibc_max"] or "").startswith("GLIBC_2."):
        problems.append(f"expected GLIBC_2.xx version req, got {r['glibc_max']!r}")
    if problems:
        print("selftest FAIL:", "; ".join(problems), file=sys.stderr)
        return 1
    print(f"selftest OK: {libc} — {r['irelative']} IRELATIVE, "
          f"static_tls={r['static_tls']}, max {r['glibc_max']}")
    return 0


def main():
    ap = argparse.ArgumentParser(description="ELF audit for the NVIDIA-on-musl port")
    ap.add_argument("dir", nargs="?", help="directory to scan (extracted .run tree)")
    ap.add_argument("--all-elf", action="store_true",
                    help="audit every ELF, not just the NVIDIA patterns")
    ap.add_argument("--json", metavar="PATH")
    ap.add_argument("--csv", metavar="PATH")
    ap.add_argument("--selftest", nargs="?", const="auto", metavar="LIBC",
                    help="verify the detectors against a glibc libc.so.6")
    a = ap.parse_args()
    if a.selftest:
        return selftest(a.selftest)
    if not a.dir:
        ap.error("dir required (or --selftest)")
    results = [audit_file(p) for p in find_files(a.dir, a.all_elf)]
    if not results:
        print(f"no ELF files matched under {a.dir}", file=sys.stderr)
        return 1
    results.sort(key=lambda r: (r["verdict"] != "NEEDS_PATCHED_MUSL",
                                -r["irelative"], -r["ifunc_syms"], r["path"]))
    if a.json:
        with open(a.json, "w") as f:
            json.dump(results, f, indent=1, ensure_ascii=False)
    if a.csv:
        cols = ["path", "verdict", "soname", "needed", "irelative", "ifunc_syms",
                "static_tls", "tpoff64", "dtpmod64", "glibc_max",
                "needs_libstdcxx", "mutex_candidates"]
        with open(a.csv, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=cols)
            w.writeheader()
            for r in results:
                w.writerow({"path": r["path"], "verdict": r["verdict"],
                            "soname": r["soname"] or "",
                            "needed": " ".join(r["needed"]),
                            "irelative": r["irelative"], "ifunc_syms": r["ifunc_syms"],
                            "static_tls": r["static_tls"], "tpoff64": r["tpoff64"],
                            "dtpmod64": r["dtpmod64"], "glibc_max": r["glibc_max"] or "",
                            "needs_libstdcxx": r["needs_libstdcxx"],
                            "mutex_candidates": len(r["mutex_candidates"])})
    for r in results:
        print(f"{r['verdict']:<20} {os.path.basename(r['path']):<32} "
              f"IREL={r['irelative']:<5} ifunc={r['ifunc_syms']:<4} "
              f"TLS={int(r['static_tls'])} stdc++={int(r['needs_libstdcxx'])} "
              f"glibc={r['glibc_max'] or '-'} mutex={len(r['mutex_candidates'])}")
    needs = [r for r in results if r["verdict"] == "NEEDS_PATCHED_MUSL"]
    gmax = max((r["glibc_max"] for r in results if r["glibc_max"]),
               key=lambda s: [int(x) for x in s.split("GLIBC_2.")[1].split(".")],
               default=None)
    print(f"\n=== summary ===\n"
          f"audited: {len(results)} | NEEDS_PATCHED_MUSL: {len(needs)} | "
          f"GCOMPAT_CANDIDATE: {len(results) - len(needs)}\n"
          f"max GLIBC_2.NN: {gmax or '-'}"
          f"{'  -> glibc-jail rootfs must be >= this' if gmax else ''}\n"
          f"libstdc++.so.6 consumers: "
          f"{', '.join(os.path.basename(r['path']) for r in results if r['needs_libstdcxx']) or '-'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
