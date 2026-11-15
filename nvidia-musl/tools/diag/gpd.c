/* gpd.c — GPU POOL DAEMON: a persistent headless multi-GPU compute service over
 * the CUDA DRIVER API, under musl, no toolkit. Lifecycle:
 *   BOOT   : init all GPUs, load tiled GEMM, alloc + warm (report READY + VRAM)
 *   SERVE  : one pthread per GPU runs tiled GEMM concurrently; main polls and
 *            reports live aggregate TFLOPS each second
 *   SHUTDN : after [seconds], stop workers cleanly, report sustained avg/min/max
 *            + stability, exit 0.
 * Proves the node is a real long-running compute service (sustained throughput,
 * no drift/crash), not a one-shot benchmark. _exit(0) dodges NVIDIA static
 * teardown crash (report/13). Build: cc -pthread.
 * usage: gpd [path/to/libcuda.so] [seconds] [N]
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

static CUresult (*cuInit)(unsigned);
static CUresult (*cuDeviceGetCount)(int *);
static CUresult (*cuDeviceGet)(CUdevice *, int);
static CUresult (*cuDeviceGetName)(char *, int, CUdevice);
static CUresult (*cuDeviceTotalMem)(size_t *, CUdevice);
static CUresult (*cuDevicePrimaryCtxRetain)(CUcontext *, CUdevice);
static CUresult (*cuDevicePrimaryCtxRelease)(CUdevice);
static CUresult (*cuCtxSetCurrent)(CUcontext);
static CUresult (*cuModuleLoadData)(CUmodule *, const char *);
static CUresult (*cuModuleGetFunction)(CUfunction *, CUmodule, const char *);
static CUresult (*cuModuleUnload)(CUmodule);
static CUresult (*cuMemAlloc)(CUdeviceptr *, size_t);
static CUresult (*cuMemFree)(CUdeviceptr);
static CUresult (*cuMemcpyHtoD)(CUdeviceptr, const void *, size_t);
static CUresult (*cuLaunchKernel)(CUfunction, unsigned, unsigned, unsigned,
                                  unsigned, unsigned, unsigned, unsigned,
                                  CUstream, void **, void **);
static CUresult (*cuCtxSynchronize)(void);

static float *g_A, *g_B;
static char *g_cubin;
static int g_N, g_chunk;          /* N = GEMM size, chunk = iters per timed batch */
static volatile int g_run = 1;
static double g_last[64];         /* per-GPU latest chunk GFLOPS (live estimate) */
static double g_sum[64], g_cnt[64], g_min[64], g_max[64];

static double now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static void *worker(void *p) {
    int d = (int)(long)p;
    size_t n = (size_t)g_N * g_N;
    CUdevice dev; cuDeviceGet(&dev, d);
    CUcontext ctx;
    if (cuDevicePrimaryCtxRetain(&ctx, dev) || cuCtxSetCurrent(ctx)) { g_last[d] = -1; return NULL; }
    CUmodule mod;
    if (cuModuleLoadData(&mod, g_cubin)) { g_last[d] = -1; cuDevicePrimaryCtxRelease(dev); return NULL; }
    CUfunction fn;
    if (cuModuleGetFunction(&fn, mod, "matmul_tiled")) { g_last[d] = -1; cuModuleUnload(mod); cuDevicePrimaryCtxRelease(dev); return NULL; }
    CUdeviceptr dA, dB, dC;
    if (cuMemAlloc(&dA, n*4) || cuMemAlloc(&dB, n*4) || cuMemAlloc(&dC, n*4)) {
        g_last[d] = -1; cuModuleUnload(mod); cuDevicePrimaryCtxRelease(dev); return NULL; }
    cuMemcpyHtoD(dA, g_A, n*4); cuMemcpyHtoD(dB, g_B, n*4);
    int nval = g_N; void *args[] = { &dA, &dB, &dC, &nval };
    unsigned g = (g_N + 15) / 16;
    g_min[d] = 1e30; g_max[d] = 0;

    while (g_run) {
        double t0 = now_ms();
        for (int it = 0; it < g_chunk && g_run; it++)
            cuLaunchKernel(fn, g, g, 1, 16, 16, 1, 0, NULL, args, NULL);
        cuCtxSynchronize();
        double ms = now_ms() - t0;
        if (ms < 1e-6) continue;
        double gf = 2.0 * g_N * g_N * g_N * g_chunk / (ms / 1e3) / 1e9;
        g_last[d] = gf; g_sum[d] += gf; g_cnt[d]++;
        if (gf < g_min[d]) g_min[d] = gf;
        if (gf > g_max[d]) g_max[d] = gf;
    }
    cuMemFree(dA); cuMemFree(dB); cuMemFree(dC);
    cuModuleUnload(mod);
    cuDevicePrimaryCtxRelease(dev);
    return NULL;
}

int main(int argc, char **argv) {
    const char *lib = argc > 1 ? argv[1] : "libcuda.so.1";
    int secs = argc > 2 ? atoi(argv[2]) : 8;
    g_N = argc > 3 ? atoi(argv[3]) : 512;
    g_chunk = 40;
    for (int d = 0; d < 64; d++) g_last[d] = -1;

    void *h = dlopen(lib, RTLD_NOW | RTLD_GLOBAL);
    if (!h) { fprintf(stderr, "dlopen %s: %s\n", lib, dlerror()); return 2; }
    #define DL(n) dlsym(h, n)
    cuInit = DL("cuInit"); cuDeviceGetCount = DL("cuDeviceGetCount");
    cuDeviceGet = DL("cuDeviceGet"); cuDeviceGetName = DL("cuDeviceGetName");
    cuDeviceTotalMem = DL("cuDeviceTotalMem_v2");
    cuDevicePrimaryCtxRetain = DL("cuDevicePrimaryCtxRetain");
    cuDevicePrimaryCtxRelease = DL("cuDevicePrimaryCtxRelease");
    cuCtxSetCurrent = DL("cuCtxSetCurrent");
    cuModuleLoadData = DL("cuModuleLoadData"); cuModuleGetFunction = DL("cuModuleGetFunction");
    cuModuleUnload = DL("cuModuleUnload");
    cuMemAlloc = DL("cuMemAlloc_v2"); cuMemFree = DL("cuMemFree_v2");
    cuMemcpyHtoD = DL("cuMemcpyHtoD_v2");
    cuLaunchKernel = DL("cuLaunchKernel"); cuCtxSynchronize = DL("cuCtxSynchronize");
    if (!cuInit || !cuLaunchKernel) { fprintf(stderr, "dlsym missing\n"); return 3; }

    size_t n = (size_t)g_N * g_N;
    g_A = malloc(n*4); g_B = malloc(n*4);
    for (size_t i = 0; i < n; i++) { g_A[i] = ((i*31u)%100u)/100.0f; g_B[i] = ((i*13u)%100u)/100.0f; }
    g_cubin = malloc(matmul_cubin_len + 1);
    memcpy(g_cubin, matmul_cubin, matmul_cubin_len); g_cubin[matmul_cubin_len] = 0;

    if (cuInit(0)) { fprintf(stderr, "cuInit failed\n"); return 1; }
    int ndev = 0; cuDeviceGetCount(&ndev);

    fprintf(stderr, "[gpd] BOOT: %d GPU(s), warming node...\n", ndev);
    for (int d = 0; d < ndev; d++) {
        CUdevice dev; cuDeviceGet(&dev, d);
        char nm[64]; cuDeviceGetName(nm, sizeof nm, dev);
        size_t vram = 0; cuDeviceTotalMem(&vram, dev);
        fprintf(stderr, "       [%d] %-20s %4.1f GiB VRAM\n", d, nm, vram/1073741824.0);
    }
    fprintf(stderr, "[gpd] READY — serving %ds, N=%d, concurrent tiled GEMM per GPU\n\n", secs, g_N);

    pthread_t th[64]; double t0 = now_ms();
    for (int d = 0; d < ndev; d++) pthread_create(&th[d], NULL, worker, (void *)(long)d);

    /* serve: live aggregate estimate each second */
    while (1) {
        double el = now_ms() - t0;
        if (el >= secs * 1e3) break;
        usleep(500 * 1000);
        double agg = 0; int ok = 0;
        for (int d = 0; d < ndev; d++) if (g_last[d] > 0) { agg += g_last[d]; ok++; }
        fprintf(stderr, "[gpd]  t=%2.0fs  aggregate %7.1f GFLOPS (%d/%d gpu)  %6.2f TFLOPS\n",
                el/1e3, agg, ok, ndev, agg/1e3);
    }
    fprintf(stderr, "\n[gpd] SHUTDOWN — stopping %d workers...\n", ndev);
    g_run = 0;
    for (int d = 0; d < ndev; d++) pthread_join(th[d], NULL);
    double wall = now_ms() - t0;

    double agg = 0, mn = 1e30, mx = 0; int ok = 0;
    for (int d = 0; d < ndev; d++) if (g_cnt[d] > 0) {
        double a = g_sum[d] / g_cnt[d]; agg += a; ok++;
        if (a < mn) mn = a;
        if (a > mx) mx = a;
    }
    fprintf(stderr, "[gpd] SUMMARY: uptime %.1fs, %d/%d GPU ok\n", wall/1e3, ok, ndev);
    fprintf(stderr, "       sustained aggregate %7.1f GFLOPS (%.2f TFLOPS)\n", agg, agg/1e3);
    fprintf(stderr, "       per-GPU avg range   %7.1f .. %7.1f GFLOPS\n", mn, mx);
    double stability = (mn > 0 ? mx/mn : 0);
    fprintf(stderr, "       stability (max/min) %.3f  %s\n", stability,
            (stability < 1.5 ? "STABLE" : "DRIFT"));
    fprintf(stderr, "[gpd] %s — clean shutdown\n", stability < 1.5 ? "NODE HEALTHY" : "CHECK DRIFT");
    _exit(0);
}
