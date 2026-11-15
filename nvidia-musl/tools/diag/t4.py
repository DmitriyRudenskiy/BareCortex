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
        if s <= va < e:
            i = va - s
            if i + n <= len(d):
                return d[i:i+n]
    return None

# top-of-stack hexdump with annotations: 0x7ffdd4d7f000 .. 0x7ffdd4d7fc00
start = 0x7ffdd4d7f000
for a in range(start, start + 0xc00, 8):
    raw = get(a)
    if raw is None:
        print(f"{a:#x}: <gap>")
        continue
    v = struct.unpack("<Q", raw)[0]
    tag = lib_of(v)
    extra = ""
    if tag is None and v < 0x100000 and v != 0:
        extra = f"  ; int {v}"
    print(f"{a:#x}: {v:#018x}  {tag or ''}{extra}")
