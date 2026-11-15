/* Focused: which eglGetProcAddress call triggers the ICD-init crash.
 * Logs each step via raw write() to a file (no stdio buffering loss). */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

static int lf = -1;
static void logstep(const char *s) {
    if (lf < 0) lf = open("/gl/egpa.log", O_WRONLY|O_CREAT|O_APPEND, 0644);
    if (lf >= 0) { write(lf, s, __builtin_strlen(s)); write(lf, "\n", 1); }
}

int main(void) {
    logstep("START");
    void *p = dlopen("/egl/libEGL.so.1.1.0", RTLD_NOW | RTLD_GLOBAL);
    if (!p) { char b[256]; const char *e=dlerror(); snprintf(b,sizeof b,"DLOPEN_FAIL: %s", e?e:"?"); logstep(b); _exit(1); }
    logstep("DLOPEN_OK");

    void *(*egpa)(const char *) = dlsym(p, "eglGetProcAddress");
    logstep("EGPA_SYM_OK");

    void *a = egpa("eglQueryString");
    logstep("A_egpa_eglQueryString_done");

    void *b = egpa("eglGetError");
    logstep("B_egpa_eglGetError_done");

    void *c = egpa("eglGetPlatformDisplayEXT");
    logstep("C_egpa_eglGetPlatformDisplayEXT_done");

    void *d = egpa("eglQueryDevicesEXT");
    logstep("D_egpa_eglQueryDevicesEXT_done");

    void *e = egpa("eglCreateContext");
    logstep("E_egpa_eglCreateContext_done");

    logstep("ALL_OK");
    _exit(0);
}
