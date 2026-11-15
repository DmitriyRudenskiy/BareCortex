/* bt.so: interpose abort() and dump candidate return addresses found by
 * brute-force stack scan (driver code is -fomit-frame-pointer, so FP-based
 * unwinding is garbage). Filters stack words against the .text/.plt ranges
 * of the NVIDIA modules. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <stdint.h>
#include <unistd.h>

struct region { const char *name; unsigned long start, end; };
/* vaddr ranges (readelf -S), gpucomp/eglcore/ICD */
static struct region texts[] = {
    { "gpucomp.plt",  0x2ab020, 0x2abe30 },
    { "gpucomp.text", 0x2abea0, 0x2abea0UL + 0x30ec161 },
    { "eglcore.plt",  0x242020, 0x242ce0 },
    { "eglcore.text", 0x242d80, 0x242d80UL + 0xcc2811 },
    { "icd.plt",      0x34020,  0x347b0 },
    { "icd.text",     0x347e0,  0x347e0UL + 0x95ce4 },
};
static const char *modname[] = { "libnvidia-gpucomp", "libnvidia-eglcore", "libEGL_nvidia" };

struct module { unsigned long start, end; int mi; };
static struct module mods[128];
static int nmods;

static int match(const char *name)
{
    if (strstr(name, "nvidia-gpucomp")) return 0;
    if (strstr(name, "nvidia-eglcore")) return 1;
    if (strstr(name, "EGL_nvidia")) return 2;
    return -1;
}

static void load_maps(void)
{
    FILE *f = fopen("/proc/self/maps", "r");
    char line[1024];
    nmods = 0;
    if (!f) return;
    while (fgets(line, sizeof line, f) && nmods < 128) {
        unsigned long s, e;
        char *p = line + 13;
        char *slash;
        if (sscanf(line, "%lx-%lx", &s, &e) != 2) continue;
        while (*p == ' ') p++;
        slash = strchr(p, '/');
        if (!slash) continue;
        int mi = match(slash);
        if (mi < 0) continue;
        mods[nmods].start = s;
        mods[nmods].end = e;
        mods[nmods].mi = mi;
        nmods++;
    }
    fclose(f);
}

void abort(void)
{
    static void (*real_abort)(void);
    unsigned long rsp;
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));
    fprintf(stderr, "BT: abort interposed, rsp=%p\n", (void *)rsp);
    for (int i = 0; i < 48; i++)
        fprintf(stderr, "RAW sp+%03x: %p\n", i * 8, (void *)*((unsigned long *)rsp + i));
    load_maps();
    unsigned long prev = 0;
    int shown = 0;
    for (unsigned long sp = rsp; sp < rsp + 0x200000 && shown < 200; sp += 8) {
        unsigned long v = *(unsigned long *)sp;
        for (int m = 0; m < nmods; m++) {
            if (v < mods[m].start || v >= mods[m].end) continue;
            unsigned long off = v - mods[m].start;
            const char *rn = 0;
            for (int r = 0; r < 2; r++) {
                struct region *t = &texts[mods[m].mi * 2 + r];
                if (off >= t->start && off < t->end) { rn = t->name; break; }
            }
            if (!rn) continue;
            if (v == prev) continue;
            prev = v;
            fprintf(stderr, "BT: sp+%05lx %s + 0x%lx [%s] (vaddr=%p)\n",
                    sp - rsp, modname[mods[m].mi], off, rn, (void *)v);
            shown++;
            break;
        }
    }
    if (!real_abort) real_abort = (void (*)(void))dlsym(RTLD_NEXT, "abort");
    if (real_abort) { real_abort(); }
    _exit(134);
}
