#!/usr/bin/env python3
"""Symbol gap of the 580 userspace against Alpine 3.25 musl + gcompat.

Two things a symbol-table-only census gets wrong, and both were measured the hard way:

1. WEAK undefined symbols do not stop the loader (musl's do_relocs only errors for
   non-weak), so __gmon_start__/_ITM_* are noise.
2. musl >= 1.2.3 resolves the glibc LFS64 names inside the dynamic linker itself
   (ldso/dynlink.c: get_lfs64, present in Alpine musl 1.2.5-r13 and 1.2.6-r6), so
   open64/__xstat64/... never appear in any symbol table and must not be counted.
"""
import os, subprocess, pathlib, collections, re

ROOT = pathlib.Path(os.environ.get("NV580", pathlib.Path.home() / ".cache/nv580"))
TREE = next((ROOT / "ex").glob("NVIDIA-Linux-*"))
SYM = ROOT / "symgap"

# ldso/dynlink.c get_lfs64() base list (musl master, 2024-06).
LFS64 = set("""aio_cancel aio_error aio_fsync aio_read aio_return aio_suspend aio_write
alphasort creat fallocate fgetpos fopen freopen fseeko fsetpos fstat fstatat fstatfs
fstatvfs ftello ftruncate ftw getdents getrlimit glob globfree lio_listio lockf lseek
lstat mkostemp mkostemps mkstemp mkstemps mmap nftw open openat posix_fadvise
posix_fallocate pread preadv prlimit pwrite pwritev readdir scandir sendfile setrlimit
stat statfs statvfs tmpfile truncate versionsort __fxstat __fxstatat __lxstat __xstat""".split())

def dyn(p):
    out = subprocess.run(["readelf", "--dyn-syms", "-W", str(p)],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        f = line.split()
        if len(f) >= 8 and f[0].endswith(":") and f[7]:
            yield f[7].split("@")[0], f[4], f[6]  # name, bind, ndx

def provided(p):
    return {n for n, _b, ndx in dyn(p) if ndx != "UND"}

def is_elf(p):
    try:
        return p.read_bytes()[:4] == b"\x7fELF"
    except OSError:
        return False

files = sorted(p for p in TREE.iterdir() if p.is_file() and is_elf(p))
musl = provided(SYM / "x-musl-1.2.6-r6/lib/ld-musl-x86_64.so.1")
gcompat = provided(SYM / "x-gcompat-1.1.0-r4/lib/libgcompat.so.0")
prov = (musl | gcompat) if os.environ.get("USE_GCOMPAT", "1") == "1" else set(musl)
for f in files:
    prov |= provided(f)

def resolves(name):
    if name in prov:
        return True
    if name.endswith("64") and len(name) > 3:
        base = name[:-2]
        if base in LFS64 and base in musl:
            return True          # musl's get_lfs64 fallback
    return False

CAT = [
    (r"^(X|_X|x)[A-Z]", "X11 (libx11/libXext — есть в Alpine)"),
    (r"^(dix|Add|Create|Free|Find|Legal|Load|Write|Fake|Standard|Fatal|Log|Loader|Xext|screen|server|Resource)", "X server internals (только Xorg-модули)"),
    (r"^(_Z|__cxa_|__gxx|_Unwind|__dynamic_cast)", "C++ runtime (libstdc++/libgcc)"),
    (r"^(gl|egl|gbm|EGL|GLX|glX|wgl)", "GL/EGL (libglvnd — в Alpine нет)"),
    (r"^(vdp|vdpau|drm|wl|wayland|_mesa|xcb)", "прочая графика (есть в Alpine)"),
    (r"^(g_|pango|gdk|gtk|cairo)", "GLib/Pango (есть в Alpine)"),
    (r"^(ZSTD|ZSTD_)", "zstd (есть в Alpine)"),
]
def classify(s):
    for pat, name in CAT:
        if re.match(pat, s):
            return name
    return "LIBC/прочее"

miss_g, miss_w, per_file = collections.Counter(), collections.Counter(), []
for f in files:
    g = w = 0
    for name, bind, ndx in dyn(f):
        if ndx != "UND" or resolves(name):
            continue
        if bind == "WEAK":
            w += 1; miss_w[name] += 1
        else:
            g += 1; miss_g[name] += 1
    per_file.append((f.name, g, w))

print(f"дерево: {TREE.name}, 64-битных ELF: {len(files)}")
print(f"поставщики: musl 1.2.6-r6" + (" + gcompat 1.1.0-r4" if os.environ.get("USE_GCOMPAT","1")=="1" else " (БЕЗ gcompat)") + f" + само дерево = {len(prov)} символов")
print(f"не разрешается GLOBAL: {len(miss_g)} уникальных;  WEAK: {len(miss_w)} (загрузчик их терпит)")
print("\n== GLOBAL по категориям ==")
cats = collections.Counter(classify(s) for s in miss_g)
for c, n in cats.most_common():
    print(f"  {n:5}  {c}")
print("\n== то, что не закрывается пакетом Alpine ==")
for s, n in sorted(miss_g.items(), key=lambda kv: (-kv[1], kv[0])):
    if classify(s) == "LIBC/прочее":
        print(f"  {n:3} файлов  {s}")
print("\n== по файлам (GLOBAL/WEAK), только ненулевые ==")
for name, g, w in sorted(per_file, key=lambda x: -x[1]):
    if g or w:
        print(f"  {g:4} {w:4}  {name}")

KEY = ("libcuda.so.580.196.07", "libnvidia-ml.so.580.196.07", "nvidia-smi", "nvidia-modprobe",
       "nvidia-persistenced", "libnvidia-tls.so.580.196.07", "libnvidia-opencl.so.580.196.07",
       "libnvidia-ptxjitcompiler.so.580.196.07", "libnvidia-glsi.so.580.196.07")
print("\n== ступень 1 лестницы (headless CUDA): GLOBAL/WEAK без разрешения ==")
for name, g, w in sorted(per_file):
    if name in KEY:
        print(f"  {g:4} {w:4}  {name}")
print("  (файлы вне списка выше не имеют неразрешённых — 0)")

print("\n== символы git-bruh ==")
for s in ("_IO_2_1_stdout_", "dladdr1"):
    print(f"  {s}: GLOBAL в {miss_g.get(s, 0)} файлах, WEAK в {miss_w.get(s, 0)}")
