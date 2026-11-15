/* gdb-free crash capture via manual frame-pointer walk (no backtrace() needed).
 * SIGABRT handler walks %rbp chain, dumps return addresses + /proc/self/maps. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

static void handler(int sig) {
    unsigned long rbp;
    __asm__ volatile ("mov %%rbp, %0" : "=r"(rbp));
    int f = open("/gl/bt.log", O_WRONLY|O_CREAT|O_TRUNC, 0644);
    char buf[80];
    int n = 0;
    for (int i = 0; i < 60 && rbp && (rbp & 0xf) == 0; i++) {
        unsigned long *fp = (unsigned long *)rbp;
        unsigned long old_rbp = fp[0];
        unsigned long ret_addr = fp[1];
        if (ret_addr == 0) break;
        int len = snprintf(buf, sizeof buf, "frame[%d]=0x%lx\n", n++, ret_addr);
        if (f >= 0) write(f, buf, len);
        if (old_rbp <= rbp) break;      /* sanity: must grow upward */
        rbp = old_rbp;
    }
    if (f >= 0) {
        int len = snprintf(buf, sizeof buf, "--- sig=%d frames=%d ---\n--- maps ---\n", sig, n);
        write(f, buf, len);
    }
    int m = open("/proc/self/maps", O_RDONLY);
    if (m >= 0) { char b[16384]; ssize_t r; while ((r = read(m, b, sizeof b)) > 0) if (f>=0) write(f, b, r); }
    _exit(128 + sig);
}

int main(void) {
    signal(SIGABRT, handler);
    signal(SIGSEGV, handler);
    signal(SIGFPE, handler);
    signal(SIGBUS, handler);
    void *p = dlopen("/egl/libEGL.so.1.1.0", RTLD_NOW | RTLD_GLOBAL);
    if (!p) { _exit(1); }
    void *(*egpa)(const char *) = dlsym(p, "eglGetProcAddress");
    volatile void *a = egpa("eglQueryString"); /* triggers ICD init -> throw */
    (void)a;
    _exit(0);
}
