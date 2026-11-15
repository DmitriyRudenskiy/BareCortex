/* matmul_mt — CONCURRENT multi-GPU GEMM over the CUDA DRIVER API, no toolkit.
 * One pthread per device; each runs the tiled GEMM on its own GPU (thread-local
 * primary context), all concurrently. Reports per-GPU + AGGREGATE GFLOPS — the
 * real capability of a multi-GPU headless compute node (~N x single GPU).
 *
 * cubin embedded via xxd -i (matmul_cubin[], has matmul_tiled). _exit(0) dodges
 * the NVIDIA static teardown crash under musl (report/13). Build needs -pthread.
 * usage: matmul_mt [path/to/libcuda.so] [N] [iters]
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

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

/* globals (dlsym'd once in main before threads spawn) */
static CUresult (*g_cuDeviceGet)(CUdevice *, int);
static CUresult (*g_cuDeviceGetName)(char *, int, CUdevice);
static CUresult (*g_cuDevicePrimaryCtxRetain)(CUcontext *, CUdevice);
static CUresult (*g_cuDevicePrimaryCtxRelease)(CUdevice);
static CUresult (*g_cuCtxSetCurrent)(CUcontext);
static CUresult (*g_cuModuleLoadData)(CUmodule *, const char *);
static CUresult (*g_cuModuleGetFunction)(CUfunction *, CUmodule, const char *);
static CUresult (*g_cuModuleUnload)(CUmodule);
static CUresult (*g_cuMemAlloc)(CUdeviceptr *, size_t);
static CUresult (*g_cuMemFree)(CUdeviceptr);
static CUresult (*g_cuMemcpyHtoD)(CUdeviceptr, const void *, size_t);
static CUresult (*g_cuMemcpyDtoH)(void *, CUdeviceptr, size_t);
static CUresult (*g_cuLaunchKernel)(CUfunction, unsigned, unsigned, unsigned,
                                    unsigned, unsigned, unsigned, unsigned,
                                    CUstream, void **, void **);
static CUresult (*g_cuCtxSynchronize)(void);

static float *g_A, *g_B;
static char *g_cubin;
static float *g_C0, *g_Cref;
static int g_N, g_iters, g_verify;
static double *g_gflops;

static double now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static void *worker(void *p) {
    int d = (int)(long)p;
    size_t n = (size_t)g_N * g_N;
    CUdevice dev; g_cuDeviceGet(&dev, d);
    char name[128] = "?"; g_cuDeviceGetName(name, sizeof name, dev);
    CUcontext ctx;
    if (g_cuDevicePrimaryCtxRetain(&ctx, dev) || g_cuCtxSetCurrent(ctx)) {
        fprintf(stderr, "  [%d] %s — CTX FAIL\n", d, name); g_gflops[d] = 0; return NULL; }
    CUmodule mod;
    if (g_cuModuleLoadData(&mod, g_cubin)) { fprintf(stderr, "  [%d] %s — Load fail\n", d, name);
        g_gflops[d] = 0; g_cuDevicePrimaryCtxRelease(dev); return NULL; }
    CUfunction fn;
    if (g_cuModuleGetFunction(&fn, mod, "matmul_tiled")) { fprintf(stderr, "  [%d] %s — GetFn fail\n", d, name);
        g_gflops[d] = 0; g_cuModuleUnload(mod); g_cuDevicePrimaryCtxRelease(dev); return NULL; }
    CUdeviceptr dA, dB, dC;
    if (g_cuMemAlloc(&dA, n*4) || g_cuMemAlloc(&dB, n*4) || g_cuMemAlloc(&dC, n*4)) {
        fprintf(stderr, "  [%d] %s — Alloc fail\n", d, name);
        g_gflops[d] = 0; g_cuModuleUnload(mod); g_cuDevicePrimaryCtxRelease(dev); return NULL; }
    g_cuMemcpyHtoD(dA, g_A, n*4); g_cuMemcpyHtoD(dB, g_B, n*4);

    int nval = g_N; void *args[] = { &dA, &dB, &dC, &nval };
    unsigned g = (g_N + 15) / 16;
    g_cuLaunchKernel(fn, g, g, 1, 16, 16, 1, 0, NULL, args, NULL); g_cuCtxSynchronize(); /* warmup */

    double t0 = now_ms();
    for (int it = 0; it < g_iters; it++)
        g_cuLaunchKernel(fn, g, g, 1, 16, 16, 1, 0, NULL, args, NULL);
    g_cuCtxSynchronize();
    double ms = now_ms() - t0;
    g_gflops[d] = 2.0 * g_N * g_N * g_N * g_iters / (ms / 1e3) / 1e9;

    if (d == 0 && g_verify) g_cuMemcpyDtoH(g_C0, dC, n*4);

    fprintf(stderr, "  [%d] %-20s %8.1f GFLOPS  (%.0f ms / %d iters)\n",
            d, name, g_gflops[d], ms, g_iters);
    g_cuMemFree(dA); g_cuMemFree(dB); g_cuMemFree(dC);
    g_cuModuleUnload(mod);
    g_cuDevicePrimaryCtxRelease(dev);
    return NULL;
}

int main(int argc, char **argv) {
    const char *lib = argc > 1 ? argv[1] : "libcuda.so.1";
    g_N = argc > 2 ? atoi(argv[2]) : 512;
    g_iters = argc > 3 ? atoi(argv[3]) : 50;
    void *h = dlopen(lib, RTLD_NOW | RTLD_GLOBAL);
    if (!h) { fprintf(stderr, "dlopen %s: %s\n", lib, dlerror()); return 2; }

    CUresult (*cuInit)(unsigned); CUresult (*cuDeviceGetCount)(int *);
    cuInit = dlsym(h, "cuInit"); cuDeviceGetCount = dlsym(h, "cuDeviceGetCount");
    g_cuDeviceGet = dlsym(h, "cuDeviceGet");
    g_cuDeviceGetName = dlsym(h, "cuDeviceGetName");
    g_cuDevicePrimaryCtxRetain = dlsym(h, "cuDevicePrimaryCtxRetain");
    g_cuDevicePrimaryCtxRelease = dlsym(h, "cuDevicePrimaryCtxRelease");
    g_cuCtxSetCurrent = dlsym(h, "cuCtxSetCurrent");
    g_cuModuleLoadData = dlsym(h, "cuModuleLoadData");
    g_cuModuleGetFunction = dlsym(h, "cuModuleGetFunction");
    g_cuModuleUnload = dlsym(h, "cuModuleUnload");
    g_cuMemAlloc = dlsym(h, "cuMemAlloc_v2");
    g_cuMemFree = dlsym(h, "cuMemFree_v2");
    g_cuMemcpyHtoD = dlsym(h, "cuMemcpyHtoD_v2");
    g_cuMemcpyDtoH = dlsym(h, "cuMemcpyDtoH_v2");
    g_cuLaunchKernel = dlsym(h, "cuLaunchKernel");
    g_cuCtxSynchronize = dlsym(h, "cuCtxSynchronize");
    if (!cuInit || !g_cuLaunchKernel || !g_cuModuleLoadData) { fprintf(stderr, "dlsym missing\n"); return 3; }

    size_t n = (size_t)g_N * g_N;
    g_verify = (g_N <= 512);
    g_A = malloc(n * 4); g_B = malloc(n * 4); g_C0 = malloc(n * 4);
    g_Cref = g_verify ? malloc(n * 4) : NULL;
    for (size_t i = 0; i < n; i++) g_A[i] = ((i * 31u) % 100u) / 100.0f;
    for (size_t i = 0; i < n; i++) g_B[i] = ((i * 13u) % 100u) / 100.0f;
    if (g_verify)
        for (int r = 0; r < g_N; r++) for (int c = 0; c < g_N; c++) {
            float s = 0.0f; for (int k = 0; k < g_N; k++) s += g_A[r*g_N+k] * g_B[k*g_N+c];
            g_Cref[r*g_N+c] = s;
        }

    g_cubin = malloc(matmul_cubin_len + 1);
    memcpy(g_cubin, matmul_cubin, matmul_cubin_len);
    g_cubin[matmul_cubin_len] = 0;

    if (cuInit(0)) { fprintf(stderr, "cuInit failed\n"); return 1; }
    int ndev = 0; cuDeviceGetCount(&ndev);
    fprintf(stderr, "[matmul_mt] N=%d iters=%d, %d device(s) CONCURRENT (tiled GEMM)\n", g_N, g_iters, ndev);

    g_gflops = calloc(ndev, sizeof(double));
    pthread_t th[64]; int nsp = ndev > 64 ? 64 : ndev;
    double wall0 = now_ms();
    for (int d = 0; d < nsp; d++) pthread_create(&th[d], NULL, worker, (void *)(long)d);
    for (int d = 0; d < nsp; d++) pthread_join(th[d], NULL);
    double wall = now_ms() - wall0;

    double agg = 0; for (int d = 0; d < nsp; d++) agg += g_gflops[d];

    float maxerr = 0.0f; char vstr[40] = "-";
    if (g_verify) {
        for (size_t i = 0; i < n; i++) { float e = g_C0[i]-g_Cref[i]; if (e<0) e=-e; if (e>maxerr) maxerr=e; }
        snprintf(vstr, sizeof vstr, "dev0 maxerr=%.5f %s", maxerr, maxerr < TOL ? "OK" : "BAD");
    }
    fprintf(stderr, "[matmul_mt] AGGREGATE %8.1f GFLOPS across %d GPUs in %.0f ms wall  %s\n",
            agg, nsp, wall, vstr);
    _exit(0);
}
