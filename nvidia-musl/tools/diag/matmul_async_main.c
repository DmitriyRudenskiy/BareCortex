/* matmul_async — ASYNC STREAM pipelined GEMM over the CUDA DRIVER API, no toolkit.
 * Demonstrates copy/compute OVERLAP: 2 streams (copy + compute) + double-buffered
 * A/B so HtoD(batch i+1) runs while kernel(batch i) runs. Benchmarks:
 *   sync     : K batches, HtoD+kernel serialized per batch
 *   pipelined: K batches, HtoD(i+1) overlaps kernel(i)
 * Reports wall time + speedup for K batches on device 0. Correctness checked once.
 *
 * cubin embedded (matmul_cubin[], matmul_tiled). _exit(0) dodges NVIDIA static
 * teardown crash under musl (report/13).
 * usage: matmul_async [path/to/libcuda.so] [N] [batches]
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
typedef void *CUevent;
typedef unsigned long long CUdeviceptr;

extern unsigned char matmul_cubin[];
extern unsigned int matmul_cubin_len;

#define TOL 1.0f

static CUresult (*cuInit)(unsigned);
static CUresult (*cuDeviceGet)(CUdevice *, int);
static CUresult (*cuDevicePrimaryCtxRetain)(CUcontext *, CUdevice);
static CUresult (*cuDevicePrimaryCtxRelease)(CUdevice);
static CUresult (*cuCtxSetCurrent)(CUcontext);
static CUresult (*cuModuleLoadData)(CUmodule *, const char *);
static CUresult (*cuModuleGetFunction)(CUfunction *, CUmodule, const char *);
static CUresult (*cuModuleUnload)(CUmodule);
static CUresult (*cuMemAlloc)(CUdeviceptr *, size_t);
static CUresult (*cuMemFree)(CUdeviceptr);
static CUresult (*cuMemcpyHtoD)(CUdeviceptr, const void *, size_t);
static CUresult (*cuMemcpyHtoDAsync)(CUdeviceptr, const void *, size_t, CUstream);
static CUresult (*cuMemcpyDtoH)(void *, CUdeviceptr, size_t);
static CUresult (*cuLaunchKernel)(CUfunction, unsigned, unsigned, unsigned,
                                  unsigned, unsigned, unsigned, unsigned,
                                  CUstream, void **, void **);
static CUresult (*cuCtxSynchronize)(void);
static CUresult (*cuStreamCreate)(CUstream *, unsigned);
static CUresult (*cuStreamDestroy)(CUstream);
static CUresult (*cuStreamSynchronize)(CUstream);
static CUresult (*cuEventCreate)(CUevent *, unsigned);
static CUresult (*cuEventDestroy)(CUevent);
static CUresult (*cuEventRecord)(CUevent, CUstream);
static CUresult (*cuStreamWaitEvent)(CUstream, CUevent, unsigned);

static double now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

int main(int argc, char **argv) {
    const char *lib = argc > 1 ? argv[1] : "libcuda.so.1";
    int N = argc > 2 ? atoi(argv[2]) : 512;
    int K = argc > 3 ? atoi(argv[3]) : 64;
    size_t n = (size_t)N * N;
    void *h = dlopen(lib, RTLD_NOW | RTLD_GLOBAL);
    if (!h) { fprintf(stderr, "dlopen %s: %s\n", lib, dlerror()); return 2; }
    #define D(n) dlsym(h, n)
    cuInit = D("cuInit"); cuDeviceGet = D("cuDeviceGet");
    cuDevicePrimaryCtxRetain = D("cuDevicePrimaryCtxRetain");
    cuDevicePrimaryCtxRelease = D("cuDevicePrimaryCtxRelease");
    cuCtxSetCurrent = D("cuCtxSetCurrent");
    cuModuleLoadData = D("cuModuleLoadData"); cuModuleGetFunction = D("cuModuleGetFunction");
    cuModuleUnload = D("cuModuleUnload");
    cuMemAlloc = D("cuMemAlloc_v2"); cuMemFree = D("cuMemFree_v2");
    cuMemcpyHtoD = D("cuMemcpyHtoD_v2"); cuMemcpyHtoDAsync = D("cuMemcpyHtoDAsync");
    cuMemcpyDtoH = D("cuMemcpyDtoH_v2");
    cuLaunchKernel = D("cuLaunchKernel"); cuCtxSynchronize = D("cuCtxSynchronize");
    cuStreamCreate = D("cuStreamCreate"); cuStreamDestroy = D("cuStreamDestroy");
    cuStreamSynchronize = D("cuStreamSynchronize");
    cuEventCreate = D("cuEventCreate"); cuEventDestroy = D("cuEventDestroy");
    cuEventRecord = D("cuEventRecord"); cuStreamWaitEvent = D("cuStreamWaitEvent");
    if (!cuInit || !cuLaunchKernel || !cuMemcpyHtoDAsync || !cuStreamCreate) {
        fprintf(stderr, "dlsym missing (stream/event/async?)\n"); return 3;
    }

    float *A = malloc(n*4), *B = malloc(n*4), *C = malloc(n*4), *Cref = malloc(n*4);
    for (size_t i = 0; i < n; i++) A[i] = ((i * 31u) % 100u) / 100.0f;
    for (size_t i = 0; i < n; i++) B[i] = ((i * 13u) % 100u) / 100.0f;
    for (int r = 0; r < N; r++) for (int c = 0; c < N; c++) {
        float s = 0.0f; for (int k = 0; k < N; k++) s += A[r*N+k] * B[k*N+c];
        Cref[r*N+c] = s;
    }
    char *cubin = malloc(matmul_cubin_len + 1);
    memcpy(cubin, matmul_cubin, matmul_cubin_len); cubin[matmul_cubin_len] = 0;

    if (cuInit(0)) { fprintf(stderr, "cuInit failed\n"); return 1; }
    CUdevice dev; cuDeviceGet(&dev, 0);
    CUcontext ctx; cuDevicePrimaryCtxRetain(&ctx, dev); cuCtxSetCurrent(ctx);
    CUmodule mod; cuModuleLoadData(&mod, cubin);
    CUfunction fn; cuModuleGetFunction(&fn, mod, "matmul_tiled");

    /* double-buffered A/B + single C */
    CUdeviceptr dA[2], dB[2], dC;
    for (int b = 0; b < 2; b++) { cuMemAlloc(&dA[b], n*4); cuMemAlloc(&dB[b], n*4); }
    cuMemAlloc(&dC, n*4);

    unsigned g = (N + 15) / 16;
    int nval = N;
    void *args0[] = { &dA[0], &dB[0], &dC, &nval };
    void *args1[] = { &dA[1], &dB[1], &dC, &nval };

    /* warmup */
    cuMemcpyHtoD(dA[0], A, n*4); cuMemcpyHtoD(dB[0], B, n*4);
    cuLaunchKernel(fn, g, g, 1, 16, 16, 1, 0, 0, args0, NULL); cuCtxSynchronize();

    /* ---- SYNC baseline: HtoD + kernel serialized per batch ---- */
    double t0 = now_ms();
    for (int i = 0; i < K; i++) {
        int b = i & 1;
        cuMemcpyHtoD(dA[b], A, n*4); cuMemcpyHtoD(dB[b], B, n*4);
        cuLaunchKernel(fn, g, g, 1, 16, 16, 1, 0, 0, b ? args1 : args0, NULL);
        cuCtxSynchronize();  /* force per-batch serialization */
    }
    double sync_ms = now_ms() - t0;

    /* ---- ASYNC pipelined: HtoD(i+1) overlaps kernel(i) ---- */
    CUstream s_copy, s_comp;
    cuStreamCreate(&s_copy, 0); cuStreamCreate(&s_comp, 0);
    CUevent *ev = calloc(K, sizeof(CUevent));
    for (int i = 0; i < K; i++) cuEventCreate(&ev[i], 0);

    cuMemcpyHtoDAsync(dA[0], A, n*4, s_copy);
    cuMemcpyHtoDAsync(dB[0], B, n*4, s_copy);
    cuEventRecord(ev[0], s_copy);
    t0 = now_ms();
    for (int i = 0; i < K; i++) {
        cuStreamWaitEvent(s_comp, ev[i], 0);
        cuLaunchKernel(fn, g, g, 1, 16, 16, 1, 0, s_comp, (i & 1) ? args1 : args0, NULL);
        if (i + 1 < K) {
            int b = (i + 1) & 1;
            cuMemcpyHtoDAsync(dA[b], A, n*4, s_copy);
            cuMemcpyHtoDAsync(dB[b], B, n*4, s_copy);
            cuEventRecord(ev[i + 1], s_copy);
        }
    }
    cuStreamSynchronize(s_comp); cuStreamSynchronize(s_copy);
    double async_ms = now_ms() - t0;

    /* verify (dC holds last kernel result) */
    cuMemcpyDtoH(C, dC, n*4);
    float maxerr = 0.0f;
    for (size_t i = 0; i < n; i++) { float e = C[i]-Cref[i]; if (e<0) e=-e; if (e>maxerr) maxerr=e; }
    char vstr[40]; snprintf(vstr, sizeof vstr, "maxerr=%.5f %s", maxerr, maxerr<TOL ? "OK" : "BAD");

    double gf_sync = 2.0*N*N*N*K / (sync_ms/1e3) / 1e9;
    double gf_async = 2.0*N*N*N*K / (async_ms/1e3) / 1e9;
    fprintf(stderr, "[matmul_async] N=%d K=%d batches, device0\n", N, K);
    fprintf(stderr, "  sync      %8.1f ms  %8.1f GFLOPS\n", sync_ms, gf_sync);
    fprintf(stderr, "  pipelined %8.1f ms  %8.1f GFLOPS   (%.2fx overlap)  %s\n",
            async_ms, gf_async, (async_ms>0 ? sync_ms/async_ms : 0), vstr);

    for (int i = 0; i < K; i++) cuEventDestroy(ev[i]);
    cuStreamDestroy(s_copy); cuStreamDestroy(s_comp);
    cuMemFree(dC); for (int b=0;b<2;b++){ cuMemFree(dA[b]); cuMemFree(dB[b]); }
    cuModuleUnload(mod);
    cuDevicePrimaryCtxRelease(dev);
    _exit(0);
}
