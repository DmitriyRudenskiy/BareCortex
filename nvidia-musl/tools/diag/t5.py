import os, struct, re
base = os.path.expanduser("~/lite/glwork/crash/")
segs = []
for fn in sorted(os.listdir(base)):
    m = re.match(r"m(\d+)_(\w+)_(\w+)_(\w+)\.bin", fn)
    if m:
        s, e = int(m.group(2), 16), int(m.group(3), 16)
        segs.append((s, e, open(base + fn, "rb").read(), fn))
segs.sort()
maps = open(base + "maps.txt").read()
B = {}
for line in maps.splitlines():
    mm = re.match(r"([0-9a-f]+)-([0-9a-f]+) \S+ 2ab000 \S+ \d+\s+(\S+libnvidia-gpucomp\S*)", line)
    if mm: B["gpucomp"] = int(mm.group(1), 16) - 0x2ab000
    mm = re.match(r"([0-9a-f]+)-([0-9a-f]+) \S+ 242000 \S+ \d+\s+(\S+libnvidia-eglcore\S*)", line)
    if mm: B["eglcore"] = int(mm.group(1), 16) - 0x242000
    mm = re.match(r"([0-9a-f]+)-([0-9a-f]+) \S+ 34000 \S+ \d+\s+(\S+libEGL_nvidia\S*)", line)
    if mm: B["icd"] = int(mm.group(1), 16) - 0x34000

def lib_of(va):
    for name, b in B.items():
        if b <= va < b + (1 << 28):
            return f"{name}+{va-b:#x}"
    return None

def get(va, n=8):
    for s, e, d, fn in segs:
        if s <= va < e and va - s + n <= len(d):
            return d[va-s:va-s+n]
    return None

def dump(label, va, nwords=12):
    print(f"--- {label} @ {va:#x}")
    for i in range(nwords):
        raw = get(va + i*8)
        if raw is None:
            print(f"  +{i*8:#x}: <gap>"); break
        v = struct.unpack("<Q", raw)[0]
        print(f"  +{i*8:#x}: {v:#018x}  {lib_of(v) or ''}")

# heap candidate (exception object?)
dump("heap", 0x60ee4ae1d100, 16)
# data ptrs from terminate frame
for p in (0x7a648dd3a190, 0x7a648dd3ce88, 0x7a648dd64e70, 0x7a648dd20820, 0x7a648dd282c0, 0x7a648b909320, 0x7a648e1286b0, 0x7a648b8d8a68, 0x7a648b7b7a80):
    dump(hex(p), p, 8)
# the typeinfo object itself + its vtable (ti-16..)
print("--- typeinfo/vtable at 0x7a648b7b7a80:")
for i in range(-3, 8):
    raw = get(0x7a648b7b7a80 + i*8)
    if raw is None: continue
    v = struct.unpack("<Q", raw)[0]
    print(f"  {0x7a648b7b7a80 + i*8:#x}: {v:#018x}  {lib_of(v) or ''}")
# which segment is 0x60ee4ae1d100 in?
for s, e, d, fn in segs:
    if s <= 0x60ee4ae1d100 < e:
        print(f"\n0x60ee4ae1d100 is in {fn}: {s:#x}-{e:#x}")
