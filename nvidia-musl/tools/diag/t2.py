import os
base = os.path.expanduser("~/lite/glwork/crash/")
needle = (0x7de1b61b7a88).to_bytes(8, "little")
for fn in sorted(os.listdir(base)):
    if not fn.endswith(".bin"):
        continue
    d = open(base + fn, "rb").read()
    i = d.find(needle)
    if i >= 0:
        print(fn, "HIT at file off", hex(i))
print("done")
# also: what big anon regions were NOT dumped? check maps from a fresh probe run is not possible here.
# check total dumped bytes vs expected
tot = sum(os.path.getsize(base + f) for f in os.listdir(base) if f.endswith(".bin"))
print("total dumped:", tot)
