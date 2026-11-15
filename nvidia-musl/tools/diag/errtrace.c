#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <stdarg.h>

static FILE *logf(void) {
    static FILE *f = NULL;
    if (!f) { f = fopen("/gl/strerror.log", "a"); if (f) setvbuf(f, NULL, _IOLBF, 0); }
    return f;
}

/* glibc/musl XSI: int strerror_r(int, char*, size_t) ; GNU: char* strerror_r(int,char*,size_t) */
int strerror_r(int e, char *buf, size_t n) {
    FILE *f = logf();
    if (f) fprintf(f, "strerror_r(%d)\n", e);
    static int (*real)(int, char*, size_t) = NULL;
    if (!real) real = (int(*)(int,char*,size_t))dlsym(RTLD_NEXT, "strerror_r");
    if (!real) { if (buf && n) buf[0]=0; return 0; }
    return real(e, buf, n);
}

char *strerror(int e) {
    FILE *f = logf();
    if (f) fprintf(f, "strerror(%d)\n", e);
    static char *(*real)(int) = NULL;
    if (!real) real = (char*(*)(int))dlsym(RTLD_NEXT, "strerror");
    if (!real) return (char*)"unknown";
    return real(e);
}

/* also catch __strerror_l in case it's called directly (int, locale_t) */
char *__strerror_l(int e, void *loc) {
    FILE *f = logf();
    if (f) fprintf(f, "__strerror_l(%d)\n", e);
    static char *(*real)(int, void*) = NULL;
    if (!real) real = (char*(*)(int,void*))dlsym(RTLD_NEXT, "__strerror_l");
    if (!real) return (char*)"unknown";
    return real(e, loc);
}
