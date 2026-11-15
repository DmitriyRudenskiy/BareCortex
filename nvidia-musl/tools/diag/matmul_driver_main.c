/* matmul_driver — GEMM over the CUDA DRIVER API, MULTI-GPU, no toolkit.
 * Generates deterministic A,B, computes a CPU reference, then runs matmul on
 * every device and verifies each GPU's C matches the CPU reference (max abs err).
 * This proves the headless musl compute path yields CORRECT numeric results —
 * the actual point of a compute target (vs. the triangle smoke test).
 *
 * cubin embedded via xxd -i (matmul_cubin[]). cuModuleLoadData(cubin) = no JIT.
 * _exit(0) dodges the NVIDIA static teardown crash under musl (report/13).
 * usage: matmul_driver [path/to/libcuda.so]
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

extern unsigned char matmul_cubin[];
extern unsigned int matmul_cubin_len;

#define N 256
#define TOL 1.0f  /* abs tolerance for float 256-term sums */

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
    CUresult (*cuMemcpyHtoD)(CUdeviceptr, const void *, size_t) = dlsym(h, "cuMemcpyHtoD_v2");
    CUresult (*cuMemcpyDtoH)(void *, CUdeviceptr, size_t) = dlsym(h, "cuMemcpyDtoH_v2");
    CUresult (*cuLaunchKernel)(CUfunction, unsigned, unsigned, unsigned,
                               unsigned, unsigned, unsigned, unsigned,
                               CUstream, void **, void **) = dlsym(h, "cuLaunchKernel");
    CUresult (*cuCtxSynchronize)(void) = dlsym(h, "cuCtxSynchronize");
    CUresult (*cuGetErrorString)(int, const char **) = dlsym(h, "cuGetErrorString");
    if (!cuInit || !cuModuleLoadData || !cuLaunchKernel || !cuMemAlloc || !cuMemcpyHtoD) {
        fprintf(stderr, "dlsym: missing entry point\n"); return 3;
    }

    /* deterministic A,B + CPU reference */
    size_t n = (size_t)N * N;
    float *A = malloc(n * 4), *B = malloc(n * 4), *Cref = malloc(n * 4);
    for (size_t i = 0; i < n; i++) A[i] = ((i * 31u) % 100u) / 100.0f;
    for (size_t i = 0; i < n; i++) B[i] = ((i * 13u) % 100u) / 100.0f;
    for (int r = 0; r < N; r++)
        for (int c = 0; c < N; c++) {
            float s = 0.0f;
            for (int k = 0; k < N; k++) s += A[r * N + k] * B[k * N + c];
            Cref[r * N + c] = s;
        }

    if (cuInit(0)) { fprintf(stderr, "cuInit failed\n"); return 1; }
    int ndev = 0; cuDeviceGetCount(&ndev);
    fprintf(stderr, "[matmul] N=%d, %d device(s) — GEMM on each, verify vs CPU\n", N, ndev);

    char *cubin = malloc(matmul_cubin_len + 1);
    memcpy(cubin, matmul_cubin, matmul_cubin_len);
    cubin[matmul_cubin_len] = 0;

    float *C = malloc(n * 4);
    long ok = 0;

    for (int d = 0; d < ndev; d++) {
        CUdevice dev; cuDeviceGet(&dev, d);
        char name[128] = "?"; cuDeviceGetName(name, sizeof name, dev);

        CUcontext ctx;
        if (cuDevicePrimaryCtxRetain(&ctx, dev) || cuCtxSetCurrent(ctx)) {
            fprintf(stderr, "  [%d] %s — CTX FAIL\n", d, name); continue;
        }
        CUmodule mod;
        CUresult mrc = cuModuleLoadData(&mod, cubin);
        if (mrc) { const char *es="?"; if(cuGetErrorString) cuGetErrorString(mrc,&es);
                   fprintf(stderr, "  [%d] %s — ModuleLoadData %d (%s)\n", d, name, mrc, es);
                   cuDevicePrimaryCtxRelease(dev); continue; }
        CUfunction fn;
        if (cuModuleGetFunction(&fn, mod, "matmul")) {
            fprintf(stderr, "  [%d] %s — GetFunction fail\n", d, name);
            cuModuleUnload(mod); cuDevicePrimaryCtxRelease(dev); continue;
        }
        CUdeviceptr dA, dB, dC;
        if (cuMemAlloc(&dA, n*4) || cuMemAlloc(&dB, n*4) || cuMemAlloc(&dC, n*4)) {
            fprintf(stderr, "  [%d] %s — MemAlloc fail\n", d, name);
            cuModuleUnload(mod); cuDevicePrimaryCtxRelease(dev); continue;
        }
        cuMemcpyHtoD(dA, A, n*4);
        cuMemcpyHtoD(dB, B, n*4);

        int nval = N;
        void *args[] = { &dA, &dB, &dC, &nval };
        unsigned lrc = cuLaunchKernel(fn, (N+15)/16, (N+15)/16, 1, 16, 16, 1, 0, NULL, args, NULL);
        cuCtxSynchronize();
        if (lrc) { fprintf(stderr, "  [%d] %s — LaunchKernel %u\n", d, name, lrc);
                   cuMemFree(dA); cuMemFree(dB); cuMemFree(dC);
                   cuModuleUnload(mod); cuDevicePrimaryCtxRelease(dev); continue; }
        cuMemcpyDtoH(C, dC, n*4);

        float maxerr = 0.0f;
        for (size_t i = 0; i < n; i++) { float e = C[i] - Cref[i]; if (e < 0) e = -e; if (e > maxerr) maxerr = e; }
        int good = (maxerr < TOL);
        if (good) ok++;
        fprintf(stderr, "  [%d] %-22s max|err|=%.5f %s\n", d, name, maxerr, good ? "OK" : "MISMATCH");

        cuMemFree(dA); cuMemFree(dB); cuMemFree(dC);
        cuModuleUnload(mod);
        cuDevicePrimaryCtxRelease(dev);
    }
    fprintf(stderr, "[matmul] RESULT: %ld/%d devices CORRECT (tol %.1f)\n", ok, ndev, TOL);
    _exit(0);  /* skip NVIDIA static teardown crash under musl (report/13) */
}
