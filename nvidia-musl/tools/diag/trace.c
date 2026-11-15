/* trace.so: log file access + ioctls via LD_PRELOAD (no ptrace). */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>

static FILE *logf;
static void init(void)
{
    static int inited;
    if (!inited) { logf = fopen("/gl/trace.log", "w"); inited = 1; }
}
static void L(const char *fmt, ...)
{
    va_list ap;
    if (!logf) return;
    va_start(ap, fmt);
    vfprintf(logf, fmt, ap);
    va_end(ap);
    fflush(logf);
}

int open(const char *p, int fl, ...)
{
    static int (*real)(const char *, int, ...) = 0;
    if (!real) real = dlsym(RTLD_NEXT, "open");
    int r = real(p, fl);
    L("open(\"%s\",0x%x) => %d%s\n", p, fl, r, r < 0 ? " FAIL" : "");
    return r;
}
int openat(int d, const char *p, int fl, ...)
{
    static int (*real)(int, const char *, int, ...) = 0;
    if (!real) real = dlsym(RTLD_NEXT, "openat");
    int r = real(d, p, fl);
    L("openat(%d,\"%s\",0x%x) => %d%s\n", d, p, fl, r, r < 0 ? " FAIL" : "");
    return r;
}
int openat2(int d, const char *p, void *how, unsigned fl)
{
    static int (*real)(int, const char *, void *, unsigned) = 0;
    if (!real) real = dlsym(RTLD_NEXT, "openat2");
    if (!real) { errno = ENOSYS; return -1; }
    int r = real(d, p, how, fl);
    L("openat2(%d,\"%s\",0x%x) => %d%s\n", d, p, fl, r, r < 0 ? " FAIL" : "");
    return r;
}
int access(const char *p, int m)
{
    static int (*real)(const char *, int) = 0;
    if (!real) real = dlsym(RTLD_NEXT, "access");
    int r = real(p, m);
    L("access(\"%s\",0x%x) => %d%s\n", p, m, r, r < 0 ? " FAIL" : "");
    return r;
}
int faccessat(int d, const char *p, int m, int fl)
{
    static int (*real)(int, const char *, int, int) = 0;
    if (!real) real = dlsym(RTLD_NEXT, "faccessat");
    int r = real(d, p, m, fl);
    L("faccessat(%d,\"%s\",0x%x) => %d%s\n", d, p, m, r, r < 0 ? " FAIL" : "");
    return r;
}
int stat(const char *p, struct stat *b)
{
    static int (*real)(const char *, struct stat *) = 0;
    if (!real) real = dlsym(RTLD_NEXT, "stat");
    int r = real(p, b);
    if (r < 0) L("stat(\"%s\") FAIL\n", p);
    return r;
}
int newfstatat(int d, const char *p, struct stat *b, int fl)
{
    static int (*real)(int, const char *, struct stat *, int) = 0;
    if (!real) real = dlsym(RTLD_NEXT, "newfstatat");
    int r = real(d, p, b, fl);
    if (r < 0) L("newfstatat(%d,\"%s\") FAIL\n", d, p);
    return r;
}
FILE *fopen(const char *p, const char *m)
{
    static FILE *(*real)(const char *, const char *) = 0;
    if (!real) real = dlsym(RTLD_NEXT, "fopen");
    FILE *r = real(p, m);
    L("fopen(\"%s\",\"%s\") => %p%s\n", p, m, (void *)r, r ? "" : " FAIL");
    return r;
}
ssize_t readlink(const char *p, char *b, size_t n)
{
    static ssize_t (*real)(const char *, char *, size_t) = 0;
    if (!real) real = dlsym(RTLD_NEXT, "readlink");
    ssize_t r = real(p, b, n);
    L("readlink(\"%s\") => %zd%s\n", p, r, r > 0 ? b : "");
    return r;
}
int ioctl(int d, int r, ...)
{
    static int (*real)(int, int, ...) = 0;
    if (!real) real = dlsym(RTLD_NEXT, "ioctl");
    va_list ap;
    va_start(ap, r);
    void *a = va_arg(ap, void *);
    va_end(ap);
    int rv = real(d, r, a);
    if (d >= 3) {
        L("ioctl(fd=%d, 0x%x) => %d%s\n", d, r, rv, rv < 0 ? " FAIL" : "");
        if (a && rv >= 0) {
            unsigned char *p = (unsigned char *)a;
            L("   arg[0..7]=");
            for (int i = 0; i < 8; i++) L("%02x ", p[i]);
            L("\n");
        }
    }
    return rv;
}

__attribute__((constructor)) static void tinit(void)
{
    FILE *f = fopen("/gl/trace.marker", "w");
    if (f) { fprintf(f, "preload active\n"); fclose(f); }
    fprintf(stderr, "TRACED-LOADED\n");
}
