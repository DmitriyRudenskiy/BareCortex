import os, struct, re
base = os.path.expanduser("~/lite/glwork/crash/")
segs = []
for fn in sorted(os.listdir(base)):
    m = re.match(r"m(\d+)_(\w+)_(\w+)_(\w+)\.bin", fn)
    if m:
        s, e = int(m.group(2), 16), int(m.group(3), 16)
        segs.append((s, e, open(base + fn, "rb").read(), fn))
segs.sort()

# identify lib bases from maps.txt
maps = open(base + "maps.txt").read()
B = {}
for line in maps.splitlines():
    mm = re.match(r"([0-9a-f]+)-([0-9a-f]+) \S+ ([0-9a-f]+) \S+ \d+\s+(\S+libnvidia-gpucomp\S*)", line)
    if mm:
        off = int(mm.group(3), 16)
        # plt starts at 0x2ab000 for gpucomp
        B.setdefault("gpucomp", int(mm.group(1), 16) - off + 0x0 if off == 0 else None)
        # better: find the mapping with plt offset
for line in maps.splitlines():
    mm = re.match(r"([0-9a-f]+)-([0-9a-f]+) \S+ 2ab000 \S+ \d+\s+(\S+libnvidia-gpucomp\S*)", line)
    if mm:
        B["gpucomp"] = int(mm.group(1), 16) - 0x2ab000
    mm = re.match(r"([0-9a-f]+)-([0-9a-f]+) \S+ 242000 \S+ \d+\s+(\S+libnvidia-eglcore\S*)", line)
    if mm:
        B["eglcore"] = int(mm.group(1), 16) - 0x242000
    mm = re.match(r"([0-9a-f]+)-([0-9a-f]+) \S+ 34000 \S+ \d+\s+(\S+libEGL_nvidia\S*)", line)
    if mm:
        B["icd"] = int(mm.group(1), 16) - 0x34000
print("bases:", {k: hex(v) for k, v in B.items()})

# find typeinfo string
t = b"St12system_error\0"
hits = []
for s, e, d, fn in segs:
    i = d.find(t)
    while i >= 0:
        hits.append(s + i)
        i = d.find(t, i + 1)
print("string at:", [hex(h) for h in hits])

ti_addr = None
for h in hits:
    needle = h.to_bytes(8, "little")
    for s, e, d, fn in segs:
        j = d.find(needle)
        while j >= 0:
            a = s + j - 8
            # verify: *(a) should be in lib range (typeinfo vptr)
            word = struct.unpack_from("<Q", d, j - 8)[0] if j >= 8 else 0
            if any(b <= word < b + (1 << 28) for b in B.values()):
                ti_addr = a
            j = d.find(needle, j + 1)
print("typeinfo:", hex(ti_addr) if ti_addr else None)
vptr = ti_addr + 8
print("vptr value:", hex(vptr))

# search everywhere for vptr
for s, e, d, fn in segs:
    n = d.find(vptr.to_bytes(8, "little"))
    if n >= 0:
        print(f"VPTR FOUND in {fn} at va {hex(s+n)}")

# stack analysis
stack = None
for s, e, d, fn in segs:
    if s > 0x7ff000000000:
        stack = (s, d)
print("\nstack:", hex(stack[0]), len(stack[1]))
d = stack[1]
s0 = stack[0]
# find all 8-byte words in stack pointing into gpucomp/eglcore/icd
refs = {}
for i in range(0, len(d) - 8, 8):
    v = struct.unpack_from("<Q", d, i)[0]
    for name, b in B.items():
        if b <= v < b + (1 << 28):
            refs.setdefault((name, v - b), []).append(s0 + i)
for (name, off), locs in sorted(refs.items(), key=lambda kv: kv[1][0]):
    print(f"  stack[{locs[0]:#x}] -> {name}+{off:#x}  (x{len(locs)})")
