/* matmul_driver — GEMM over the CUDA DRIVER API, MULTI-GPU, no toolkit, TIMED.
 * Same cubin + same driver-API launch code; the ONLY variable is the host libc
 * (build this as musl vs glibc to measure whether the musl host slows compute).
 * Times `iters` pipelined GEMMs per device -> GFLOPS. Also verifies vs a CPU
 * reference when N is small (correctness alongside perf).
 *
 * cubin embedded via xxd -i (matmul_cubin[]). _exit(0) dodges the NVIDIA static
 * teardown crash under musl (report/13).
 * usage: matmul_driver [path/to/libcuda.so] [N] [iters]
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef int CUresult;
typedef int CUdevice;
typedef void *CUcontext;
typedef void *CUmodule;
typedef void *CUfunction;
typedef void *CUstream;
typedef unsigned long long CUdeviceptr;

extern unsigned char matmul_cubin[];
extern unsigned int matmul_cubin_len;

#define TOL 1.0f

static double now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

int main(int argc, char **argv) {
    const char *lib = argc > 1 ? argv[1] : "libcuda.so.1";
    int N = argc > 2 ? atoi(argv[2]) : 512;
    int iters = argc > 3 ? atoi(argv[3]) : 50;
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
    CUresult (*cuMemcpyHtoD)(CUdeviceptr, const void *, size_t) = dlsym(h, "cuMemcpyHtoD_v2");
    CUresult (*cuMemcpyDtoH)(void *, CUdeviceptr, size_t) = dlsym(h, "cuMemcpyDtoH_v2");
    CUresult (*cuLaunchKernel)(CUfunction, unsigned, unsigned, unsigned,
                               unsigned, unsigned, unsigned, unsigned,
                               CUstream, void **, void **) = dlsym(h, "cuLaunchKernel");
    CUresult (*cuCtxSynchronize)(void) = dlsym(h, "cuCtxSynchronize");
    if (!cuInit || !cuModuleLoadData || !cuLaunchKernel || !cuMemAlloc || !cuMemcpyHtoD) {
        fprintf(stderr, "dlsym: missing entry point\n"); return 3;
    }

    size_t n = (size_t)N * N;
    int verify = (N <= 512);
    float *A = malloc(n * 4), *B = malloc(n * 4), *C = malloc(n * 4), *Cref = verify ? malloc(n * 4) : NULL;
    for (size_t i = 0; i < n; i++) A[i] = ((i * 31u) % 100u) / 100.0f;
    for (size_t i = 0; i < n; i++) B[i] = ((i * 13u) % 100u) / 100.0f;
    if (verify)
        for (int r = 0; r < N; r++) for (int c = 0; c < N; c++) {
            float s = 0.0f; for (int k = 0; k < N; k++) s += A[r * N + k] * B[k * N + c];
            Cref[r * N + c] = s;
        }

    if (cuInit(0)) { fprintf(stderr, "cuInit failed\n"); return 1; }
    int ndev = 0; cuDeviceGetCount(&ndev);
    fprintf(stderr, "[matmul] N=%d iters=%d verify=%d, %d device(s)\n", N, iters, verify, ndev);

    char *cubin = malloc(matmul_cubin_len + 1);
    memcpy(cubin, matmul_cubin, matmul_cubin_len);
    cubin[matmul_cubin_len] = 0;

    double best = 0; long ok = 0;

    for (int d = 0; d < ndev; d++) {
        CUdevice dev; cuDeviceGet(&dev, d);
        char name[128] = "?"; cuDeviceGetName(name, sizeof name, dev);
        CUcontext ctx;
        if (cuDevicePrimaryCtxRetain(&ctx, dev) || cuCtxSetCurrent(ctx)) {
            fprintf(stderr, "  [%d] %s — CTX FAIL\n", d, name); continue;
        }
        CUmodule mod;
        if (cuModuleLoadData(&mod, cubin)) { fprintf(stderr, "  [%d] %s — Load fail\n", d, name);
                                             cuDevicePrimaryCtxRelease(dev); continue; }
        CUfunction fn;
        if (cuModuleGetFunction(&fn, mod, "matmul")) { fprintf(stderr, "  [%d] %s — GetFn fail\n", d, name);
            cuModuleUnload(mod); cuDevicePrimaryCtxRelease(dev); continue; }
        CUdeviceptr dA, dB, dC;
        if (cuMemAlloc(&dA, n*4) || cuMemAlloc(&dB, n*4) || cuMemAlloc(&dC, n*4)) {
            fprintf(stderr, "  [%d] %s — Alloc fail\n", d, name);
            cuModuleUnload(mod); cuDevicePrimaryCtxRelease(dev); continue; }
        cuMemcpyHtoD(dA, A, n*4); cuMemcpyHtoD(dB, B, n*4);

        int nval = N; void *args[] = { &dA, &dB, &dC, &nval };
        unsigned g = (N + 15) / 16;
        CUfunction fn_t;
        if (cuModuleGetFunction(&fn_t, mod, "matmul_tiled")) fn_t = fn;  /* fallback */

        /* time naive */
        cuLaunchKernel(fn, g, g, 1, 16, 16, 1, 0, NULL, args, NULL); cuCtxSynchronize();
        double t0 = now_ms();
        for (int it = 0; it < iters; it++) cuLaunchKernel(fn, g, g, 1, 16, 16, 1, 0, NULL, args, NULL);
        cuCtxSynchronize();
        double ms_n = now_ms() - t0;
        double gf_n = 2.0 * N * N * N * iters / (ms_n / 1e3) / 1e9;

        /* time tiled + verify (C holds the last tiled result) */
        cuLaunchKernel(fn_t, g, g, 1, 16, 16, 1, 0, NULL, args, NULL); cuCtxSynchronize();
        t0 = now_ms();
        for (int it = 0; it < iters; it++) cuLaunchKernel(fn_t, g, g, 1, 16, 16, 1, 0, NULL, args, NULL);
        cuCtxSynchronize();
        double ms_t = now_ms() - t0;
        double gf_t = 2.0 * N * N * N * iters / (ms_t / 1e3) / 1e9;

        float maxerr = 0.0f; char vstr[40] = "-";
        if (verify) {
            cuMemcpyDtoH(C, dC, n*4);
            for (size_t i = 0; i < n; i++) { float e = C[i]-Cref[i]; if (e<0) e=-e; if (e>maxerr) maxerr=e; }
            snprintf(vstr, sizeof vstr, "tiled maxerr=%.5f %s", maxerr, maxerr < TOL ? "OK" : "BAD");
            if (maxerr < TOL) ok++;
        }
        if (gf_t > best) best = gf_t;
        fprintf(stderr, "  [%d] %-20s naive %8.1f | tiled %8.1f GFLOPS  (%.1fx)  %s\n",
                d, name, gf_n, gf_t, (ms_t > 0 ? ms_n / ms_t : 0), vstr);
        cuMemFree(dA); cuMemFree(dB); cuMemFree(dC);
        cuModuleUnload(mod);
        cuDevicePrimaryCtxRelease(dev);
    }
    fprintf(stderr, "[matmul] BEST(tiled) %8.1f GFLOPS ; %ld/%d correct%s\n",
            best, verify ? ok : 0, verify ? ndev : 0, verify ? "" : " (timing only)");
    _exit(0);
}
