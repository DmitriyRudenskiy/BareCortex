/* tri_driver — CUDA driver-API triangle, MULTI-GPU (no toolkit, dlopens libcuda).
 * Launches the software-raster triangle kernel on EVERY device and verifies each
 * renders the same triangle. First multi-GPU CUDA kernel launch under musl
 * (report/15 part 1 was single-GPU). Proves the headless compute path scales to
 * all GPUs on the box (5x RTX 4060 Ti on host 192.168.0.86).
 *
 * Kernel CUBIN embedded via xxd -i array (tri_kernel_cubin[]), appended at build
 * time. cuModuleLoadData(cubin) needs no JIT (error 221 avoided). _exit(0) skips
 * the NVIDIA static teardown crash under musl (report/13).
 *
 * usage: tri_driver [path/to/libcuda.so] [out.ppm (device0 only)]
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef int CUresult;
typedef int CUdevice;
typedef void *CUcontext;
typedef void *CUmodule;
typedef void *CUfunction;
typedef void *CUstream;
typedef unsigned long long CUdeviceptr;

extern unsigned char tri_kernel_cubin[];
extern unsigned int tri_kernel_cubin_len;

#define W 256
#define H 256
#define EXPECT 9297  /* per-device non-black px (matches host reference) */

int main(int argc, char **argv) {
    const char *lib = argc > 1 ? argv[1] : "libcuda.so.1";
    void *h = dlopen(lib, RTLD_NOW | RTLD_GLOBAL);
    if (!h) { fprintf(stderr, "dlopen %s: %s\n", lib, dlerror()); return 2; }

    CUresult (*cuInit)(unsigned) = dlsym(h, "cuInit");
    CUresult (*cuDeviceGetCount)(int *) = dlsym(h, "cuDeviceGetCount");
    CUresult (*cuDeviceGet)(CUdevice *, int) = dlsym(h, "cuDeviceGet");
    CUresult (*cuDeviceGetName)(char *, int, CUdevice) = dlsym(h, "cuDeviceGetName");
    CUresult (*cuDevicePrimaryCtxRetain)(CUcontext *, CUdevice) = dlsym(h, "cuDevicePrimaryCtxRetain");
    CUresult (*cuDevicePrimaryCtxRelease)(CUdevice) = dlsym(h, "cuDevicePrimaryCtxRelease");
    CUresult (*cuCtxSetCurrent)(CUcontext) = dlsym(h, "cuCtxSetCurrent");
    CUresult (*cuModuleLoadData)(CUmodule *, const char *) = dlsym(h, "cuModuleLoadData");
    CUresult (*cuModuleGetFunction)(CUfunction *, CUmodule, const char *) = dlsym(h, "cuModuleGetFunction");
    CUresult (*cuModuleUnload)(CUmodule) = dlsym(h, "cuModuleUnload");
    CUresult (*cuMemAlloc)(CUdeviceptr *, size_t) = dlsym(h, "cuMemAlloc_v2");
    CUresult (*cuMemFree)(CUdeviceptr) = dlsym(h, "cuMemFree_v2");
    CUresult (*cuMemcpyDtoH)(void *, CUdeviceptr, size_t) = dlsym(h, "cuMemcpyDtoH_v2");
    CUresult (*cuLaunchKernel)(CUfunction, unsigned, unsigned, unsigned,
                               unsigned, unsigned, unsigned, unsigned,
                               CUstream, void **, void **) = dlsym(h, "cuLaunchKernel");
    CUresult (*cuCtxSynchronize)(void) = dlsym(h, "cuCtxSynchronize");
    CUresult (*cuGetErrorString)(int, const char **) = dlsym(h, "cuGetErrorString");
    if (!cuInit || !cuModuleLoadData || !cuLaunchKernel || !cuMemAlloc) {
        fprintf(stderr, "dlsym: missing entry point\n"); return 3;
    }

    if (cuInit(0)) { fprintf(stderr, "cuInit failed\n"); return 1; }
    int ndev = 0; cuDeviceGetCount(&ndev);
    fprintf(stderr, "[tri_driver] %d device(s) — launching triangle on each\n", ndev);

    char *cubin = malloc(tri_kernel_cubin_len + 1);
    memcpy(cubin, tri_kernel_cubin, tri_kernel_cubin_len);
    cubin[tri_kernel_cubin_len] = 0;

    unsigned char *h_fb = malloc((size_t)W*H*4);
    long total = 0, ok = 0;

    for (int d = 0; d < ndev; d++) {
        CUdevice dev; cuDeviceGet(&dev, d);
        char name[128] = "?"; cuDeviceGetName(name, sizeof name, dev);

        CUcontext ctx;
        if (cuDevicePrimaryCtxRetain(&ctx, dev) || cuCtxSetCurrent(ctx)) {
            fprintf(stderr, "  [%d] %s — PRIMARY CTX FAIL (skip)\n", d, name); continue;
        }
        CUmodule mod;
        CUresult mrc = cuModuleLoadData(&mod, cubin);
        if (mrc) { const char *es="?"; if(cuGetErrorString) cuGetErrorString(mrc,&es);
                   fprintf(stderr, "  [%d] %s — ModuleLoadData %d (%s)\n", d, name, mrc, es);
                   cuDevicePrimaryCtxRelease(dev); continue; }
        CUfunction fn;
        if (cuModuleGetFunction(&fn, mod, "tri_kernel")) {
            fprintf(stderr, "  [%d] %s — GetFunction fail\n", d, name);
            cuModuleUnload(mod); cuDevicePrimaryCtxRelease(dev); continue;
        }
        CUdeviceptr d_fb;
        if (cuMemAlloc(&d_fb, (size_t)W*H*4)) {
            fprintf(stderr, "  [%d] %s — MemAlloc fail\n", d, name);
            cuModuleUnload(mod); cuDevicePrimaryCtxRelease(dev); continue;
        }
        void *args[] = { &d_fb };
        unsigned lrc = cuLaunchKernel(fn, (W+15)/16, (H+15)/16, 1, 16, 16, 1, 0, NULL, args, NULL);
        cuCtxSynchronize();
        if (lrc) { fprintf(stderr, "  [%d] %s — LaunchKernel %u\n", d, name, lrc);
                   cuMemFree(d_fb); cuModuleUnload(mod); cuDevicePrimaryCtxRelease(dev); continue; }
        cuMemcpyDtoH(h_fb, d_fb, (size_t)W*H*4);

        long nb = 0;
        for (int i = 0; i < W*H; i++) if (h_fb[i*4] | h_fb[i*4+1] | h_fb[i*4+2]) nb++;
        total += nb;
        int good = (nb == EXPECT);
        if (good) ok++;
        fprintf(stderr, "  [%d] %-22s %ld/%d px %s\n", d, name, nb, W*H, good ? "OK" : "MISMATCH");

        if (d == 0 && argc > 2) {  /* save device0 image */
            FILE *f = fopen(argv[2], "wb");
            if (f) { fprintf(f, "P3\n%d %d\n255\n", W, H);
                     for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
                         int o = (y*W+x)*4; fprintf(f, "%d %d %d\n", h_fb[o], h_fb[o+1], h_fb[o+2]); }
                     fclose(f); fprintf(stderr, "  WROTE %s\n", argv[2]); }
        }
        cuMemFree(d_fb);
        cuModuleUnload(mod);
        cuDevicePrimaryCtxRelease(dev);
    }
    fprintf(stderr, "[tri_driver] RESULT: %ld/%d devices rendered %d px (total %ld)\n",
            ok, ndev, EXPECT, total);
    _exit(0);  /* skip NVIDIA static teardown crash under musl (report/13) */
}
