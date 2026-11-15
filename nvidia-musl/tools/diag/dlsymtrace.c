/* dlsymtrace.c v2: intercept dlsym (the driver builds its libc layer via
 * runtime dlsym). Real dlsym resolved by ELF parsing (no PLT recursion). */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <elf.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>

static FILE *symlog, *calllog;
static void *(*real_dlsym)(void *, const char *);
static __thread int in_hook;

/* find gcompat's dlsym address by parsing maps + ELF (no dlsym call) */
static FILE *rlog;
static void resolve_real(void)
{
    if (real_dlsym) return;
    rlog = fopen("/gl/resolve.log", "w");
    FILE *m = fopen("/proc/self/maps", "r");
    if (!rlog) return;
    if (!m) { fprintf(rlog, "no maps\n"); return; }
    char line[512];
    char paths[8][256];
    unsigned long bases[8];
    int np = 0, tried = 0;
    while (fgets(line, sizeof line, m)) {
        unsigned long s, e;
        char perms[8], p[300];
        unsigned long off, d1, d2;
        unsigned long long inode;
        if (sscanf(line, "%lx-%lx %7s %lx %lx:%lx %llu %299[^\n]",
                   &s, &e, perms, &off, &d1, &d2, &inode, p) == 8) {
            char *q = p;
            while (*q == ' ') q++;
            if (strstr(q, ".so")) fprintf(rlog, "MAP: %s (base=%lx)\n", q, s - off);
            if ((strstr(q, "libc.so") || strstr(q, "musl")) && np < 8) {
                int dup = 0, i;
                for (i = 0; i < np; i++) if (!strcmp(paths[i], q)) { dup = 1; break; }
                if (!dup) { bases[np] = s - off; snprintf(paths[np], 256, "%s", q); np++; }
            }
        }
    }
    fclose(m);
    fprintf(rlog, "candidates: %d\n", np);
    while (np-- > 0 && !real_dlsym) {
        int fd = open(paths[np], O_RDONLY);
        fprintf(rlog, "try %s (base=%lx)\n", paths[np], bases[np]);
        if (fd < 0) continue;
        Elf64_Ehdr eh;
        if (read(fd, &eh, sizeof eh) != sizeof eh || memcmp(eh.e_ident, ELFMAG, 4)) { fprintf(rlog, "bad ehdr\n"); close(fd); continue; }
        Elf64_Shdr *sh = malloc(eh.e_shentsize * eh.e_shnum);
        if (!sh) { close(fd); continue; }
        if (pread(fd, sh, eh.e_shentsize * eh.e_shnum, eh.e_shoff) != (ssize_t)(eh.e_shentsize * eh.e_shnum)) { free(sh); close(fd); continue; }
        Elf64_Sym *syms = NULL; const char *strs = NULL;
        int si = 0;
        char *shstr = malloc(eh.e_shstrndx < eh.e_shnum ? sh[eh.e_shstrndx].sh_size : 1);
        if (shstr && pread(fd, shstr, sh[eh.e_shstrndx].sh_size, sh[eh.e_shstrndx].sh_offset) == (ssize_t)sh[eh.e_shstrndx].sh_size) {
            for (int i = 0; i < eh.e_shnum; i++)
                if (sh[i].sh_name && !strcmp(shstr + sh[i].sh_name, ".dynsym")) si = i;
            for (int i = 0; i < eh.e_shnum; i++)
                if (sh[i].sh_name && !strcmp(shstr + sh[i].sh_name, ".symtab")) { si = i; break; }
        }
        free(shstr);
        if (si < eh.e_shnum && sh[si].sh_size) {
            syms = malloc(sh[si].sh_size);
            if (pread(fd, syms, sh[si].sh_size, sh[si].sh_offset) != (ssize_t)sh[si].sh_size) { free(syms); syms = 0; }
        }
        if (syms) {
            int sti = sh[si].sh_link;
            if (sti < eh.e_shnum) {
                strs = malloc(sh[sti].sh_size);
                if (pread(fd, (void *)strs, sh[sti].sh_size, sh[sti].sh_offset) != (ssize_t)sh[sti].sh_size) { free((void *)strs); strs = 0; }
            }
        }
        if (syms && strs) {
            unsigned n = sh[si].sh_size / sizeof(Elf64_Sym);
            for (unsigned i = 0; i < n; i++)
                if (syms[i].st_value && !strcmp(strs + syms[i].st_name, "dlsym")) {
                    real_dlsym = (void *)(uintptr_t)(bases[np] + syms[i].st_value);
                    fprintf(rlog, "found dlsym in %s at %p\n", paths[np], (void *)real_dlsym);
                    break;
                }
        }
        free((void *)syms); free((void *)strs); free(sh);
        close(fd);
    }
    fprintf(rlog, "result: real_dlsym=%p\n", (void *)real_dlsym);
    if (rlog) { fclose(rlog); rlog = 0; }
}

static void init(void)
{
    static int inited;
    if (!inited) {
        symlog = fopen("/gl/dlsym.log", "w");
        calllog = fopen("/gl/calls.log", "w");
        inited = 1;
    }
}
static void L(FILE *f, const char *fmt, ...)
{
    va_list ap;
    if (!f) return;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fflush(f);
}
static void *next_real(void *h, const char *name)
{
    if (!real_dlsym) resolve_real();
    return real_dlsym ? real_dlsym(h, name) : 0;
}

static int name_is(const char *name, const char *sym)
{
    const char *base = name;
    while (*base && *base != '@') base++;
    return strncmp(base, sym, strlen(sym)) == 0 && (*base == 0 || *base == '@');
}

/* ---- wrappers ---- */
static int w_open(const char *p, int fl, ...)
{
    mode_t m = 0;
    if (fl & O_CREAT) { va_list ap; va_start(ap, fl); m = va_arg(ap, mode_t); va_end(ap); }
    static int (*r)(const char *, int, ...);
    if (!r) r = (void *)next_real(RTLD_DEFAULT, "open");
    if (!r) { errno = ENOSYS; return -1; }
    int rv = r(p, fl, m);
    L(calllog, "open(\"%s\",0x%x) => %d%s\n", p, fl, rv, rv < 0 ? " FAIL" : "");
    return rv;
}
static int w_openat(int d, const char *p, int fl, ...)
{
    mode_t m = 0;
    if (fl & O_CREAT) { va_list ap; va_start(ap, fl); m = va_arg(ap, mode_t); va_end(ap); }
    static int (*r)(int, const char *, int, ...);
    if (!r) r = (void *)next_real(RTLD_DEFAULT, "openat");
    if (!r) { errno = ENOSYS; return -1; }
    int rv = r(d, p, fl, m);
    L(calllog, "openat(%d,\"%s\",0x%x) => %d%s\n", d, p, fl, rv, rv < 0 ? " FAIL" : "");
    return rv;
}
static int w_openat2(int d, const char *p, void *how, unsigned fl)
{
    static int (*r)(int, const char *, void *, unsigned);
    if (!r) r = (void *)next_real(RTLD_DEFAULT, "openat2");
    if (!r) { errno = ENOSYS; return -1; }
    int rv = r(d, p, how, fl);
    L(calllog, "openat2(%d,\"%s\",0x%x) => %d%s\n", d, p, fl, rv, rv < 0 ? " FAIL" : "");
    return rv;
}
static int w_access(const char *p, int m)
{
    static int (*r)(const char *, int);
    if (!r) r = (void *)next_real(RTLD_DEFAULT, "access");
    if (!r) { errno = ENOSYS; return -1; }
    int rv = r(p, m);
    L(calllog, "access(\"%s\",0x%x) => %d%s\n", p, m, rv, rv < 0 ? " FAIL" : "");
    return rv;
}
static int w_faccessat(int d, const char *p, int m, int fl)
{
    static int (*r)(int, const char *, int, int);
    if (!r) r = (void *)next_real(RTLD_DEFAULT, "faccessat");
    if (!r) { errno = ENOSYS; return -1; }
    int rv = r(d, p, m, fl);
    L(calllog, "faccessat(%d,\"%s\",0x%x) => %d%s\n", d, p, m, rv, rv < 0 ? " FAIL" : "");
    return rv;
}
static int w_newfstatat(int d, const char *p, struct stat *b, int fl)
{
    static int (*r)(int, const char *, struct stat *, int);
    if (!r) r = (void *)next_real(RTLD_DEFAULT, "newfstatat");
    if (!r) { errno = ENOSYS; return -1; }
    int rv = r(d, p, b, fl);
    if (rv < 0) L(calllog, "newfstatat(%d,\"%s\") FAIL\n", d, p);
    return rv;
}
static ssize_t w_readlink(const char *p, char *b, size_t n)
{
    static ssize_t (*r)(const char *, char *, size_t);
    if (!r) r = (void *)next_real(RTLD_DEFAULT, "readlink");
    if (!r) { errno = ENOSYS; return -1; }
    ssize_t rv = r(p, b, n);
    L(calllog, "readlink(\"%s\") => %zd%s\n", p, rv, rv > 0 ? b : "");
    return rv;
}
static FILE *w_fopen(const char *p, const char *m)
{
    static FILE *(*r)(const char *, const char *);
    if (!r) r = (void *)next_real(RTLD_DEFAULT, "fopen");
    if (!r) { errno = ENOSYS; return 0; }
    FILE *rv = r(p, m);
    L(calllog, "fopen(\"%s\",\"%s\") => %p%s\n", p, m, (void *)rv, rv ? "" : " FAIL");
    return rv;
}
static int w_ioctl(int d, int r, ...)
{
    static int (*real)(int, int, ...);
    if (!real) real = (void *)next_real(RTLD_DEFAULT, "ioctl");
    if (!real) { errno = ENOSYS; return -1; }
    va_list ap;
    va_start(ap, r);
    void *a = va_arg(ap, void *);
    va_end(ap);
    int rv = real(d, r, a);
    L(calllog, "ioctl(fd=%d, 0x%x) => %d%s\n", d, r, rv, rv < 0 ? " FAIL" : "");
    if (a && rv >= 0) {
        unsigned char *p = (unsigned char *)a;
        L(calllog, "   arg[0..7]=");
        int i;
        for (i = 0; i < 8; i++) L(calllog, "%02x ", p[i]);
        L(calllog, "\n");
    }
    return rv;
}
static FILE *w_errf(const char *fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt ? fmt : "(null)", ap);
    va_end(ap);
    if (!symlog) init();
    fprintf(symlog ? symlog : stderr, "ERRF: %s\n", buf);
    fflush(symlog);
    return 0;
}
static long w_syscall(long n, ...)
{
    static long (*r)(long, ...);
    if (!r) r = (void *)next_real(RTLD_DEFAULT, "syscall");
    if (!r) { errno = ENOSYS; return -1; }
    va_list ap;
    va_start(ap, n);
    long a0 = va_arg(ap, long), a1 = va_arg(ap, long), a2 = va_arg(ap, long);
    long a3 = va_arg(ap, long), a4 = va_arg(ap, long), a5 = va_arg(ap, long);
    va_end(ap);
    long rv = r(n, a0, a1, a2, a3, a4, a5);
    const char *np = "";
    if (n == 257 || n == 2) np = "open";
    else if (n == 294) np = "fstat";
    else if (n == 25) np = "mmap";
    else if (n == 54) np = "ioctl";
    else if (n == 0) np = "read";
    else if (n == 262) np = "newfstatat";
    L(calllog, "syscall(%ld %s, a0=%llx a1=%llx a2=%llx) => %lld\n",
      n, np, (unsigned long long)a0, (unsigned long long)a1,
      (unsigned long long)a2, (long long)rv);
    return rv;
}

static void *wrapped_for(const char *name)
{
    if (name_is(name, "openat2")) return (void *)w_openat2;
    if (name_is(name, "openat")) return (void *)w_openat;
    if (name_is(name, "open")) return (void *)w_open;
    if (name_is(name, "faccessat")) return (void *)w_faccessat;
    if (name_is(name, "access")) return (void *)w_access;
    if (name_is(name, "newfstatat")) return (void *)w_newfstatat;
    if (name_is(name, "readlink")) return (void *)w_readlink;
    if (name_is(name, "fopen")) return (void *)w_fopen;
    if (name_is(name, "ioctl")) return (void *)w_ioctl;
    if (name_is(name, "syscall")) return (void *)w_syscall;
    if (name_is(name, "ErrorF")) return (void *)w_errf;
    return 0;
}

void *dlsym(void *handle, const char *name)
{
    if (in_hook) return next_real(handle, name);
    in_hook = 1;
    init();
    void *rv;
    if (name && name[0] && (unsigned char)name[0] < 0x80) {
        L(symlog, "dlsym(%p, \"%s\")\n", handle, name);
        rv = wrapped_for(name) ? wrapped_for(name) : next_real(handle, name);
    } else {
        rv = next_real(handle, name);
    }
    in_hook = 0;
    return rv;
}

/* ---- poor-man's core: dump memory on abort, no signal, no apport ---- */
static void dump_memory(const char *why)
{
    char dir[] = "/gl/crash";
    mkdir(dir, 0755);
    FILE *hd = fopen("/gl/crash/header.txt", "w");
    if (hd) {
        fprintf(hd, "why: %s\npid: %ld\ntime: %ld\n", why, (long)getpid(), (long)time(0));
        fclose(hd);
    }
    FILE *maps = fopen("/proc/self/maps", "r");
    if (!maps) return;
    FILE *mapscopy = fopen("/gl/crash/maps.txt", "w");
    int memfd = open("/proc/self/mem", O_RDONLY);
    if (memfd < 0) { fclose(maps); return; }
    char line[1024];
    int n = 0, skipped = 0;
    while (fgets(line, sizeof line, maps)) {
        if (mapscopy) fputs(line, mapscopy);
        unsigned long s, e;
        char perms[8], p[512];
        unsigned long off, d1, d2;
        unsigned long long inode;
        if (sscanf(line, "%lx-%lx %7s %lx %lx:%lx %llu %511[^\n]",
                   &s, &e, perms, &off, &d1, &d2, &inode, p) != 8) continue;
        char *q = p;
        while (*q == ' ') q++;
        size_t len = e - s;
        int want = 0;
        if (strstr(q, "vvar") || strstr(q, "vdso") || strstr(q, "[vsyscall]"))
            want = 0;
        else if (len > 0)
            want = 1; /* dump everything */
        if (!want) { skipped++; continue; }
        char fn[256];
        snprintf(fn, sizeof fn, "/gl/crash/m%03d_%016lx_%016lx_%06lx.bin", n, s, e, off);
        FILE *f = fopen(fn, "wb");
        if (f) {
            unsigned long a = s;
            while (a < e) {
                size_t chunk = (e - a) > (1u << 20) ? (1u << 20) : (e - a);
                char buf[1 << 20];
                ssize_t r = pread(memfd, buf, chunk, (off_t)a);
                if (r <= 0) { memset(buf, 0, chunk); r = chunk; }
                fwrite(buf, 1, (size_t)r, f);
                a += (unsigned long)r;
            }
            fclose(f);
        }
        fprintf(stderr, "[dump] %016lx-%016lx %s off=%lx (%s)\n", s, e, perms, off, q);
        n++;
    }
    fclose(maps);
    if (mapscopy) fclose(mapscopy);
    close(memfd);
}
void abort(void)
{
    dump_memory("abort intercepted");
    _exit(99);
}

__attribute__((constructor(101))) static void tinit(void)
{
    resolve_real();
    FILE *f = fopen("/gl/dlsym.marker", "w");
    if (f) { fprintf(f, "active real_dlsym=%p\n", (void *)real_dlsym); fclose(f); }
}
