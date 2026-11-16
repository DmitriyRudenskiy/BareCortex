/* Functional checks for libnvcompat: dlopen the shim, call every wrapper
 * through dlsym the way the NVIDIA userspace does. Built and run by
 * test_shim_libnvcompat.sh; exits with the number of failed checks. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* mirror of the shim's musl-layout macros (see libnvcompat.c) */
#if __SIZE_WIDTH__ == 64
#define m_kind __u.__i[4]
#else
#define m_kind __u.__i[3]
#endif
#define m_type __u.__i[0]

static int ok, fails;
#define CHECK(cond) \
    do { \
        if (cond) ok++; \
        else { fails++; printf("FAIL: %s (line %d)\n", #cond, __LINE__); } \
    } while (0)

int main(int argc, char **argv) {
    (void)argc;
    setvbuf(stdout, NULL, _IONBF, 0);
    void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!h) { printf("FAIL: dlopen %s: %s\n", argv[1], dlerror()); return 1; }

    /* __isoc23_* : glibc 2.39+ variants, semantics of the plain calls */
    long (*strtol64)(const char *, char **, int) = dlsym(h, "__isoc23_strtol");
    char *end = NULL;
    CHECK(strtol64 && strtol64("42x", &end, 0) == 42 && end[0] == 'x');
    unsigned long long (*strtoull64)(const char *, char **, int) =
        dlsym(h, "__isoc23_strtoull");
    CHECK(strtoull64 &&
          strtoull64("18446744073709551615", NULL, 10) == 18446744073709551615ULL);
    int (*sscanf64)(const char *, const char *, ...) = dlsym(h, "__isoc23_sscanf");
    int n = 0;
    char word[16] = "";
    CHECK(sscanf64 && sscanf64("7 apples", "%d %15s", &n, word) == 2 &&
          n == 7 && !strcmp(word, "apples"));

    /* fcntl64 : vararg forwarder */
    int (*fcntl64)(int, int, ...) = dlsym(h, "fcntl64");
    char path[] = "/tmp/nvcompat-test-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    CHECK(fcntl64 && fcntl64(fd, F_GETFL) >= 0);
    close(fd);
    unlink(path);

    /* alphasort64 / scandir64 : glibc 2.28+ LFS renames */
    int (*alphasort64)(const struct dirent **, const struct dirent **) =
        dlsym(h, "alphasort64");
    struct dirent *da = calloc(1, sizeof *da), *db = calloc(1, sizeof *db);
    strcpy(da->d_name, "a");
    strcpy(db->d_name, "b");
    CHECK(alphasort64 &&
          alphasort64((const struct dirent **)&db,
                      (const struct dirent **)&da) > 0);
    CHECK(alphasort64((const struct dirent **)&da,
                      (const struct dirent **)&da) == 0);
    int (*scandir64)(const char *, struct dirent ***,
                     int (*)(const struct dirent *),
                     int (*)(const struct dirent **, const struct dirent **)) =
        dlsym(h, "scandir64");
    struct dirent **ents = NULL;
    int cnt = scandir64 ? scandir64("/tmp", &ents, NULL, NULL) : -1;
    CHECK(cnt > 0);
    for (int i = 0; i < cnt; i++) free(ents[i]);
    free(ents);

    /* __pthread_key_create : the WEAK gpucomp import whose silent zero
     * made the EGL ICD throw std::system_error (report 14 resolution) */
    int (*key_create)(pthread_key_t *, void (*)(void *)) =
        dlsym(h, "__pthread_key_create");
    pthread_key_t k;
    CHECK(key_create && key_create(&k, NULL) == 0);
    CHECK(pthread_setspecific(k, (void *)(long)0x7b) == 0 &&
          pthread_getspecific(k) == (void *)(long)0x7b);
    pthread_key_delete(k);

    /* dladdr1 : gcompat 1.1.0 lacks it; must delegate to dladdr */
    int (*dladdr1_)(const void *, Dl_info *, void **, unsigned) =
        dlsym(h, "dladdr1");
    Dl_info di;
    CHECK(dladdr1_ && dladdr1_((const void *)main, &di, NULL, 0) &&
          di.dli_fname);

    /* recursive-mutex kind patch: a glibc static recursive initializer left
     * kind=1 in musl's unused slot; first lock must promote the mutex to
     * recursive (re-lock by owner succeeds) and consume the kind */
    int (*mlock)(pthread_mutex_t *) = dlsym(h, "__nvcompat_mutex_lock");
    int (*mtry)(pthread_mutex_t *) = dlsym(h, "__nvcompat_mutex_trylock");
    pthread_mutex_t rec = PTHREAD_MUTEX_INITIALIZER;
    rec.m_kind = 1; /* PTHREAD_MUTEX_RECURSIVE kind */
    CHECK(mlock && mlock(&rec) == 0);
    CHECK(rec.m_type == 1); /* type promoted */
    CHECK(rec.m_kind == 0); /* kind consumed */
    CHECK(mtry && mtry(&rec) == 0); /* recursive: owner may re-take */
    CHECK(pthread_mutex_unlock(&rec) == 0 && pthread_mutex_unlock(&rec) == 0);

    /* negative: kind=0 (plain static initializer) must stay non-recursive */
    pthread_mutex_t plain = PTHREAD_MUTEX_INITIALIZER;
    CHECK(mlock && mlock(&plain) == 0);
    CHECK(mtry(&plain) == EBUSY);
    CHECK(pthread_mutex_unlock(&plain) == 0);

    printf("checks passed: %d, failed: %d\n", ok, fails);
    return fails != 0;
}
