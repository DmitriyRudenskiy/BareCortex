/* tri_driver — CUDA driver-API triangle (no toolkit, dlopens libcuda.so.1).
 * Ports the software-raster triangle to the CUDA DRIVER API so it runs as a
 * musl-native process (matches report/09's proven dlopen pattern). This is the
 * first KERNEL LAUNCH under musl (report/09 stopped at cuMemAlloc).
 *
 * The kernel PTX is embedded via an xxd -i array (tri_kernel_ptx[]), appended
 * to this file at build time. Ends with _exit(0) to skip the NVIDIA static
 * teardown crash under musl (report/13).
 *
 * build (musl, in the lite env): see assemble script. usage:
 *   tri_driver [path/to/libcuda.so.1] [out.ppm]
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

int main(int argc, char **argv) {
    const char *lib = argc > 1 ? argv[1] : "libcuda.so.1";
    void *h = dlopen(lib, RTLD_NOW | RTLD_GLOBAL);
    if (!h) { fprintf(stderr, "dlopen %s: %s\n", lib, dlerror()); return 2; }

    CUresult (*cuInit)(unsigned) = dlsym(h, "cuInit");
    CUresult (*cuDeviceGetCount)(int *) = dlsym(h, "cuDeviceGetCount");
    CUresult (*cuDeviceGet)(CUdevice *, int) = dlsym(h, "cuDeviceGet");
    CUresult (*cuDeviceGetName)(char *, int, CUdevice) = dlsym(h, "cuDeviceGetName");
    CUresult (*cuDevicePrimaryCtxRetain)(CUcontext *, CUdevice) = dlsym(h, "cuDevicePrimaryCtxRetain");
    CUresult (*cuCtxSetCurrent)(CUcontext) = dlsym(h, "cuCtxSetCurrent");
    CUresult (*cuModuleLoadData)(CUmodule *, const char *) = dlsym(h, "cuModuleLoadData");
    CUresult (*cuModuleGetFunction)(CUfunction *, CUmodule, const char *) = dlsym(h, "cuModuleGetFunction");
    CUresult (*cuMemAlloc)(CUdeviceptr *, size_t) = dlsym(h, "cuMemAlloc_v2");
    CUresult (*cuMemFree)(CUdeviceptr) = dlsym(h, "cuMemFree_v2");
    CUresult (*cuMemcpyDtoH)(void *, CUdeviceptr, size_t) = dlsym(h, "cuMemcpyDtoH_v2");
    CUresult (*cuLaunchKernel)(CUfunction, unsigned, unsigned, unsigned,
                               unsigned, unsigned, unsigned, unsigned,
                               CUstream, void **, void **) = dlsym(h, "cuLaunchKernel");
    CUresult (*cuCtxSynchronize)(void) = dlsym(h, "cuCtxSynchronize");
    if (!cuInit || !cuModuleLoadData || !cuModuleGetFunction || !cuLaunchKernel || !cuMemAlloc) {
        fprintf(stderr, "dlsym: missing entry point\n"); return 3;
    }

    if (cuInit(0)) { fprintf(stderr, "cuInit failed\n"); return 1; }
    int ndev = 0; cuDeviceGetCount(&ndev);
    CUdevice dev; cuDeviceGet(&dev, 0);
    char name[128] = "?"; cuDeviceGetName(name, sizeof name, dev);
    fprintf(stderr, "[tri_driver] device0 (%d/%d): %s\n", 0, ndev, name);

    CUcontext ctx;
    if (cuDevicePrimaryCtxRetain(&ctx, dev) || cuCtxSetCurrent(ctx)) {
        fprintf(stderr, "primary ctx unavailable\n"); return 1;
    }

    /* cubin is a raw ELF image (no null term needed); pass a copy */
    char *ptx = malloc(tri_kernel_cubin_len + 1);
    memcpy(ptx, tri_kernel_cubin, tri_kernel_cubin_len);
    ptx[tri_kernel_cubin_len] = 0;
    CUresult (*cuGetErrorString)(int, const char **) = dlsym(h, "cuGetErrorString");
    CUmodule mod;
    CUresult mrc = cuModuleLoadData(&mod, ptx);
    if (mrc) { const char *es="?"; if(cuGetErrorString) cuGetErrorString(mrc,&es);
               fprintf(stderr, "cuModuleLoadData -> %d (%s), binlen=%u\n", mrc, es, tri_kernel_cubin_len); return 1; }
    CUfunction fn;
    if (cuModuleGetFunction(&fn, mod, "tri_kernel")) { fprintf(stderr, "cuModuleGetFunction failed\n"); return 1; }
    fprintf(stderr, "[tri_driver] module loaded, fn resolved — LAUNCHING KERNEL under musl\n");

    CUdeviceptr d_fb;
    if (cuMemAlloc(&d_fb, (size_t)W*H*4)) { fprintf(stderr, "cuMemAlloc failed\n"); return 1; }

    void *args[] = { &d_fb };
    unsigned lrc = cuLaunchKernel(fn, (W+15)/16, (H+15)/16, 1, 16, 16, 1, 0, NULL, args, NULL);
    if (lrc) { fprintf(stderr, "cuLaunchKernel -> %u\n", lrc); return 1; }
    cuCtxSynchronize();
    fprintf(stderr, "[tri_driver] kernel launched + synced (first musl kernel launch)\n");

    unsigned char *h_fb = malloc((size_t)W*H*4);
    if (cuMemcpyDtoH(h_fb, d_fb, (size_t)W*H*4)) { fprintf(stderr, "cuMemcpyDtoH failed\n"); return 1; }
    long nb = 0;
    for (int i = 0; i < W*H; i++) if (h_fb[i*4] | h_fb[i*4+1] | h_fb[i*4+2]) nb++;
    fprintf(stderr, "[tri_driver] rendered %ld/%d non-black px\n", nb, W*H);

    const char *out = argc > 2 ? argv[2] : "tri_driver.ppm";
    FILE *f = fopen(out, "wb");
    if (f) {
        fprintf(f, "P3\n%d %d\n255\n", W, H);
        for (int y = 0; y < H; y++)  /* fb row 0 = top of image */
            for (int x = 0; x < W; x++) { int o = (y*W+x)*4; fprintf(f, "%d %d %d\n", h_fb[o], h_fb[o+1], h_fb[o+2]); }
        fclose(f);
        fprintf(stderr, "WROTE %s\n", out);
    }
    cuMemFree(d_fb);
    _exit(0);  /* skip NVIDIA static teardown crash under musl (report/13) */
}
