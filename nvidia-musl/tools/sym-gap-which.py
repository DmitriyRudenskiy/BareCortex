#!/usr/bin/env python3
"""Exactly which glibc-isms gcompat 1.1.0 supplies to the headless CUDA set."""
import os, subprocess, sys, pathlib

ROOT = pathlib.Path(os.environ.get("NV580", pathlib.Path.home() / ".cache/nv580"))
TREE = next((ROOT / "ex").glob("NVIDIA-Linux-*"))
SYM = ROOT / "symgap"
MUSL_SO = SYM / "lib/ld-musl-x86_64.so.1"
GCOMPAT_SO = SYM / "lib/libgcompat.so.0"
for req, apk in ((MUSL_SO, "musl-1.2.6-r6.apk"), (GCOMPAT_SO, "gcompat-1.1.0-r4.apk")):
    if not req.exists():
        sys.exit(f"нет провайдера {req} — без него замер врёт.\n"
                 f"  cd {SYM} && curl -O https://dl-cdn.alpinelinux.org/alpine/v3.25/main/x86_64/{apk}"
                 f" && tar -xf {apk}")
LFS64 = set("""aio_cancel aio_error aio_fsync aio_read aio_return aio_suspend aio_write
alphasort creat fallocate fgetpos fopen freopen fseeko fsetpos fstat fstatat fstatfs
fstatvfs ftello ftruncate ftw getdents getrlimit glob globfree lio_listio lockf lseek
lstat mkostemp mkostemps mkstemp mkstemps mmap nftw open openat posix_fadvise
posix_fallocate pread preadv prlimit pwrite pwritev readdir scandir sendfile setrlimit
stat statfs statvfs tmpfile truncate versionsort __fxstat __fxstatat __lxstat __xstat""".split())

def dyn(p):
    out = subprocess.run(["readelf", "--dyn-syms", "-W", str(p)], capture_output=True, text=True).stdout
    for line in out.splitlines():
        f = line.split()
        if len(f) >= 8 and f[0].endswith(":") and f[7]:
            yield f[7].split("@")[0], f[4], f[6]

def provided(p):
    return {n for n, _b, ndx in dyn(p) if ndx != "UND"}

def is_elf(p):
    try:
        return p.read_bytes()[:4] == b"\x7fELF"
    except OSError:
        return False

files = [p for p in TREE.iterdir() if p.is_file() and is_elf(p)]
musl = provided(MUSL_SO)
gc = provided(GCOMPAT_SO)
prov = set(musl)
for f in files:
    prov |= provided(f)

def resolves(n):
    return n in prov or (n.endswith("64") and len(n) > 3 and n[:-2] in LFS64 and n[:-2] in musl)

for name in ("libcuda.so.580.196.07", "libnvidia-ml.so.580.196.07", "nvidia-smi",
             "nvidia-modprobe", "nvidia-persistenced", "libnvidia-opencl.so.580.196.07",
             "libnvidia-ptxjitcompiler.so.580.196.07"):
    miss = sorted(n for n, b, ndx in dyn(TREE / name) if ndx == "UND" and b != "WEAK" and not resolves(n))
    if not miss:
        print(f"{name}: —")
        continue
    print(f"{name}: без gcompat не хватает {len(miss)}")
    for n in miss:
        print(f"    {n:32} {'gcompat даёт' if n in gc else 'НЕТ НИ ГДЕ'}")
