#!/usr/bin/env python3
# find std::system_error objects via typeinfo-driven vptr value
import os, re, struct

CRASH = os.path.expanduser("~/lite/glwork/crash")
segs = []
for fn in sorted(os.listdir(CRASH)):
    m = re.match(r"m(\d+)_(\w+)_(\w+)_(\w+)\.bin", fn)
    if not m:
        continue
    s, e, off = int(m.group(2), 16), int(m.group(3), 16), int(m.group(4), 16)
    data = open(os.path.join(CRASH, fn), "rb").read()
    segs.append([s, e, off, data, fn])

B1, B2, B3 = 0x7de1b1e00000, 0x7de1b6400000, 0x7de1b86c2000
LIBS = (("gpucomp", B1), ("eglcore", B2), ("icd", B3))
TEXT = {"gpucomp": (0x2abea0, 0x338B001), "eglcore": (0x242d80, 0xF05591), "icd": (0x347e0, 0xC04C4)}

def lib_of(va):
    for name, b in LIBS:
        if b <= va < b + (1 << 32):
            return name, va - b
    return None, 0

def find8(va):
    for s, e, off, data, fn in segs:
        if s <= va < e:
            i = va - s
            if i + 8 <= len(data):
                return struct.unpack_from("<Q", data, i)[0]
    return None

def findstr(hay, needle, start=0):
    idx = []
    i = hay.find(needle, start)
    while i >= 0:
        idx.append(i)
        i = hay.find(needle, i + 1)
    return idx

def cstr(va, maxlen=96):
    out = b""
    for i in range(0, maxlen, 8):
        b = find8(va + i)
        if b is None:
            break
        out += b.to_bytes(8, "little")
        if 0 in out[-8:]:
            break
    return out.split(b"\0", 1)[0]

# 1. find typeinfo name strings
targets = [b"St12system_error\0", b"St14system_error\0", b"14system_error\0"]
name_addrs = set()
for s, e, off, data, fn in segs:
    for t in targets:
        for i in findstr(data, t):
            name_addrs.add(s + i)
print(f"name string addresses: {[hex(a) for a in sorted(name_addrs)]}")

# 2. find typeinfo objects: 8-byte slot == name_addr
ti_addrs = set()
for a in sorted(name_addrs):
    needle = a.to_bytes(8, "little")
    for s, e, off, data, fn in segs:
        for i in findstr(data, needle):
            ti_addrs.add(s + i - 8)  # name* is at ti+8
print(f"typeinfo candidates: {[hex(a) for a in sorted(ti_addrs)]}")

# 3. expected vptr = ti + 8; verify typeinfo vptr points into lib
good_ti = []
for ti in sorted(ti_addrs):
    tv = find8(ti)
    n = lib_of(tv)[0] if tv else None
    name = cstr(find8(ti + 8)) if find8(ti + 8) else b"?"
    if n:
        good_ti.append((ti, tv, name))
print(f"verified typeinfo: {[(hex(t), hex(v), n) for t, v, n in good_ti]}")

# 4. scan for vptr value == ti+8
hits = []
for ti, tv, name in good_ti:
    vptr_val = ti + 8
    needle = vptr_val.to_bytes(8, "little")
    for s, e, off, data, fn in segs:
        for i in findstr(data, needle):
            X = s + i
            val = struct.unpack_from("<I", data, i + 8)[0]
            pad = struct.unpack_from("<I", data, i + 12)[0]
            cat = struct.unpack_from("<Q", data, i + 16)[0]
            cl, co = lib_of(cat)
            hits.append((X, name, val, pad, cat, cl))

print(f"\nobjects with system_error vptr: {len(hits)}")
for X, name, val, pad, cat, cl in hits:
    cat_ti = find8(cat - 8) if cat >= 8 else None
    catname = cstr(find8(cat_ti + 8)) if cat_ti else b"?"
    print(f"  X={X:#x} class={name!r} value={val} pad={pad} cat={cat:#x} ({cl}) catclass={catname!r}")

# 5. neighborhood .text refs
def near_text(X, span=0x1000):
    out = []
    for a in range(X - span, X + span, 8):
        v = find8(a)
        if v is None:
            continue
        nm, o = lib_of(v)
        if nm:
            lo, hi = TEXT[nm]
            if lo <= o < hi:
                out.append((a, nm, o))
    return out

for X, name, val, pad, cat, cl in hits:
    print(f"\n=== X={X:#x} (value={val}) nearby .text refs (±4KB):")
    for a, nm, o in near_text(X):
        print(f"  [{a:#x}] -> {nm}+{o:#x}")
