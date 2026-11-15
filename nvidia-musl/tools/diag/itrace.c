/* Interposer: log the CPU/system-probe functions the driver calls during ICD init,
 * so we see which one returns a bad value on the musl shim right before the throw. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/utsname.h>
#include <sched.h>
#include <sys/resource.h>
#include <stdarg.h>
#include <fcntl.h>

static int logfd = -1;
static void L(const char *fmt, ...) {
    if (logfd < 0) logfd = open("/gl/itrace.log", 1|0100|02000, 0644);
    if (logfd < 0) return;
    char b[256]; va_list ap; va_start(ap, fmt);
    int n = vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    if (n > 0) { char nl='\n'; write(logfd, b, n); write(logfd, &nl, 1); }
}

long sysconf(int name) {
    static long (*real)(int) = NULL;
    if (!real) real = dlsym(RTLD_NEXT, "sysconf");
    long r = real ? real(name) : -1;
    L("sysconf(name=%d) = %ld", name, r);
    return r;
}
int uname(struct utsname *b) {
    static int (*real)(struct utsname*) = NULL;
    if (!real) real = dlsym(RTLD_NEXT, "uname");
    int r = real ? real(b) : -1;
    L("uname() = %d sysname=%s release=%s machine=%s", r, b->sysname, b->release, b->machine);
    return r;
}
int sched_getaffinity(pid_t pid, size_t cpusetsize, cpu_set_t *mask) {
    static int (*real)(pid_t, size_t, cpu_set_t*) = NULL;
    if (!real) real = dlsym(RTLD_NEXT, "sched_getaffinity");
    int r = real ? real(pid, cpusetsize, mask) : -1;
    L("sched_getaffinity(pid=%d) = %d", pid, r);
    return r;
}
unsigned int getauxval(unsigned int type) {
    static unsigned int (*real)(unsigned int) = NULL;
    if (!real) real = dlsym(RTLD_NEXT, "getauxval");
    unsigned int r = real ? real(type) : 0;
    L("getauxval(%u) = 0x%x", type, r);
    return r;
}
int prctl(int option, unsigned long a2, unsigned long a3, unsigned long a4, unsigned long a5) {
    static int (*real)(int, unsigned long, unsigned long, unsigned long, unsigned long) = NULL;
    if (!real) real = dlsym(RTLD_NEXT, "prctl");
    int r = real ? real(option, a2, a3, a4, a5) : -1;
    L("prctl(opt=%d,a2=%lu) = %d", option, a2, r);
    return r;
}
int getrlimit(int resource, struct rlimit *rlim) {
    static int (*real)(int, struct rlimit*) = NULL;
    if (!real) real = dlsym(RTLD_NEXT, "getrlimit");
    int r = real ? real(resource, rlim) : -1;
    L("getrlimit(%d) = %d", resource, r);
    return r;
}
int __cxa_thread_atexit(void *fn, void *arg, void *d) {
    L("__cxa_thread_atexit called");
    return -1;
}

/* GD TLS: void *__tls_get_addr(struct tls_index *ti) */
struct __tls_index { unsigned int tls_offset; unsigned int tls_align; void *tdfunc; void *create_destroy; unsigned int modid; unsigned int flag; };
void *__tls_get_addr(struct __tls_index *ti) {
    static void *(*real)(struct __tls_index*) = NULL;
    if (!real) real = dlsym(RTLD_NEXT, "__tls_get_addr");
    void *r = real ? real(ti) : NULL;
    L("__tls_get_addr(ti=%p modid=%u) = %p", ti, ti?ti->modid:0, r);
    return r;
}
