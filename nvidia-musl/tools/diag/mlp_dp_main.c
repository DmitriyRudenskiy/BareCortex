/* mlp_dp_main.c — MULTI-GPU DATA-PARALLEL MLP TRAINING over the CUDA DRIVER API,
 * under musl, no toolkit. The production pattern for a multi-GPU training node:
 *   - total batch split into N shards, one per GPU
 *   - each GPU runs forward + backward on its shard -> LOCAL gradients
 *   - ALL-REDUCE: gradients averaged across all GPUs (host-mediated, no NCCL)
 *   - every GPU applies the SAME averaged gradient -> weights stay in sync
 * Loss (avg MSE across shards) must converge. End: verify GPU0 forward == CPU
 * forward on the final synced weights. Reuses the part-8 MLP kernels (mlp.cubin).
 * _exit(0) dodges NVIDIA static teardown crash (report/13).
 * usage: mlp_dp [path/to/libcuda.so] [iters]
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>

typedef int CUresult;
typedef int CUdevice;
typedef void *CUcontext;
typedef void *CUmodule;
typedef void *CUfunction;
typedef void *CUstream;
typedef unsigned long long CUdeviceptr;

extern unsigned char mlp_cubin[];
extern unsigned int mlp_cubin_len;

static CUresult (*cuInit)(unsigned);
static CUresult (*cuDeviceGetCount)(int *);
static CUresult (*cuDeviceGet)(CUdevice *, int);
static CUresult (*cuDeviceGetName)(char *, int, CUdevice);
static CUresult (*cuDevicePrimaryCtxRetain)(CUcontext *, CUdevice);
static CUresult (*cuDevicePrimaryCtxRelease)(CUdevice);
static CUresult (*cuCtxSetCurrent)(CUcontext);
static CUresult (*cuModuleLoadData)(CUmodule *, const char *);
static CUresult (*cuModuleGetFunction)(CUfunction *, CUmodule, const char *);
static CUresult (*cuMemAlloc)(CUdeviceptr *, size_t);
static CUresult (*cuMemcpyHtoD)(CUdeviceptr, const void *, size_t);
static CUresult (*cuMemcpyDtoH)(void *, CUdeviceptr, size_t);
static CUresult (*cuLaunchKernel)(CUfunction, unsigned, unsigned, unsigned,
                                  unsigned, unsigned, unsigned, unsigned,
                                  CUstream, void **, void **);
static CUresult (*cuCtxSynchronize)(void);

#define NSHARD 5
#define SHARD  256
#define BTOT   (NSHARD * SHARD)
#define D 4
#define H 16
#define O 1
static float LR = 0.05f;

typedef struct {
    CUdevice dev; CUcontext ctx; CUmodule mod;
    CUfunction f_gemm, f_epi, f_dY, f_mask, f_col, f_sgd;
    CUdeviceptr dXs, dTs, dW1, db1, dW2, db2, dT1, dHb, dT2, dYo,
                ddY, ddH, ddW1, ddB1, ddW2, ddB2;
} GPU;
static GPU G[NSHARD];

static void g2(CUfunction f, CUdeviceptr A, CUdeviceptr B, CUdeviceptr C, int M, int N, int K, int tA, int tB) {
    void *args[] = { &A, &B, &C, &M, &N, &K, &tA, &tB };
    cuLaunchKernel(f, (N + 15) / 16, (M + 15) / 16, 1, 16, 16, 1, 0, NULL, args, NULL);
}
static void g1(CUfunction f, size_t n, void *args[]) {
    cuLaunchKernel(f, (n + 255) / 256, 1, 1, 256, 1, 1, 0, NULL, args, NULL);
}
static void gc(CUfunction f, CUdeviceptr X, CUdeviceptr out, int M, int N) {
    void *args[] = { &X, &out, &M, &N };
    cuLaunchKernel(f, (N + 255) / 256, 1, 1, 256, 1, 1, 0, NULL, args, NULL);
}

/* forward on GPU d: X@W1 -> relu+b1 -> H ; H@W2 -> +b2 -> Y */
static void fwd(GPU *g) {
    g2(g->f_gemm, g->dXs, g->dW1, g->dT1, SHARD, H, D, 0, 0);
    { int M=SHARD,N=H,relu=1; void *a[]={&g->dT1,&g->db1,&g->dHb,&M,&N,&relu}; g1(g->f_epi,(size_t)SHARD*H,a); }
    g2(g->f_gemm, g->dHb, g->dW2, g->dT2, SHARD, O, H, 0, 0);
    { int M=SHARD,N=O,relu=0; void *a[]={&g->dT2,&g->db2,&g->dYo,&M,&N,&relu}; g1(g->f_epi,(size_t)SHARD*O,a); }
}
/* backward on GPU d: local grads into ddW1,ddB1,ddW2,ddB2 */
static void bwd(GPU *g, float invB) {
    { int n=SHARD*O; void *a[]={&g->dYo,&g->dTs,&g->ddY,&n,&invB}; g1(g->f_dY,(size_t)SHARD*O,a); }
    g2(g->f_gemm, g->dHb, g->ddY, g->ddW2, H, O, SHARD, 1, 0);
    gc(g->f_col, g->ddY, g->ddB2, SHARD, O);
    g2(g->f_gemm, g->ddY, g->dW2, g->ddH, SHARD, H, O, 0, 1);
    { int n=SHARD*H; void *a[]={&g->ddH,&g->dHb,&n}; g1(g->f_mask,(size_t)SHARD*H,a); }
    g2(g->f_gemm, g->dXs, g->ddH, g->ddW1, D, H, SHARD, 1, 0);
    gc(g->f_col, g->ddH, g->ddB1, SHARD, H);
}
static void upd(GPU *g) {
    { int n=D*H; void *a[]={&g->dW1,&g->ddW1,&n,&LR}; g1(g->f_sgd,(size_t)D*H,a); }
    { int n=H;   void *a[]={&g->db1,&g->ddB1,&n,&LR}; g1(g->f_sgd,(size_t)H,a); }
    { int n=H*O; void *a[]={&g->dW2,&g->ddW2,&n,&LR}; g1(g->f_sgd,(size_t)H*O,a); }
    { int n=O;   void *a[]={&g->db2,&g->ddB2,&n,&LR}; g1(g->f_sgd,(size_t)O,a); }
}

int main(int argc, char **argv) {
    const char *lib = argc > 1 ? argv[1] : "libcuda.so.1";
    int iters = argc > 2 ? atoi(argv[2]) : 200;
    void *h = dlopen(lib, RTLD_NOW | RTLD_GLOBAL);
    if (!h) { fprintf(stderr, "dlopen %s: %s\n", lib, dlerror()); return 2; }
    #define DL(n) dlsym(h, n)
    cuInit = DL("cuInit"); cuDeviceGetCount = DL("cuDeviceGetCount"); cuDeviceGet = DL("cuDeviceGet");
    cuDeviceGetName = DL("cuDeviceGetName");
    cuDevicePrimaryCtxRetain = DL("cuDevicePrimaryCtxRetain");
    cuDevicePrimaryCtxRelease = DL("cuDevicePrimaryCtxRelease");
    cuCtxSetCurrent = DL("cuCtxSetCurrent");
    cuModuleLoadData = DL("cuModuleLoadData"); cuModuleGetFunction = DL("cuModuleGetFunction");
    cuMemAlloc = DL("cuMemAlloc_v2");
    cuMemcpyHtoD = DL("cuMemcpyHtoD_v2"); cuMemcpyDtoH = DL("cuMemcpyDtoH_v2");
    cuLaunchKernel = DL("cuLaunchKernel"); cuCtxSynchronize = DL("cuCtxSynchronize");
    if (!cuInit || !cuLaunchKernel) { fprintf(stderr, "dlsym missing\n"); return 3; }

    /* data + nonlinear target */
    float *X = malloc(BTOT * D * 4), *T = malloc(BTOT * O * 4);
    for (int i = 0; i < BTOT; i++) for (int d = 0; d < D; d++)
        X[i * D + d] = sinf(i * 0.13f + d * 1.7f);
    for (int i = 0; i < BTOT; i++)
        T[i] = sinf(X[i*0]) + cosf(X[i*1]) * X[i*2] + 0.5f * X[i*3] * X[i*3];
    float *W1 = malloc(D*H*4), *b1 = malloc(H*4), *W2 = malloc(H*O*4), *b2 = malloc(O*4);
    for (int i = 0; i < D*H; i++) W1[i] = 0.1f * (((i*7)%11)-5)/5.0f;
    for (int i = 0; i < H; i++)   b1[i] = 0.05f * (((i*3)%7)-3)/3.0f;
    for (int i = 0; i < H*O; i++) W2[i] = 0.1f * (((i*5)%9)-4)/4.0f;
    for (int i = 0; i < O; i++)   b2[i] = 0.0f;
    char *cubin = malloc(mlp_cubin_len + 1);
    memcpy(cubin, mlp_cubin, mlp_cubin_len); cubin[mlp_cubin_len] = 0;

    if (cuInit(0)) { fprintf(stderr, "cuInit failed\n"); return 1; }
    int ndev = 0; cuDeviceGetCount(&ndev);
    if (ndev > NSHARD) ndev = NSHARD;
    fprintf(stderr, "[mlp_dp] %d-GPU data-parallel, %d samples/step (%d shards x %d), iters=%d\n",
            ndev, ndev*SHARD, ndev, SHARD, iters);

    for (int d = 0; d < ndev; d++) {
        CUdevice dev; cuDeviceGet(&dev, d);
        G[d].dev = dev;
        cuDevicePrimaryCtxRetain(&G[d].ctx, dev); cuCtxSetCurrent(G[d].ctx);
        cuModuleLoadData(&G[d].mod, cubin);
        if (cuModuleGetFunction(&G[d].f_gemm, G[d].mod, "gemm2"))     return 3;
        if (cuModuleGetFunction(&G[d].f_epi,  G[d].mod, "epi"))       return 3;
        if (cuModuleGetFunction(&G[d].f_dY,   G[d].mod, "dY_k"))      return 3;
        if (cuModuleGetFunction(&G[d].f_mask, G[d].mod, "relu_mask")) return 3;
        if (cuModuleGetFunction(&G[d].f_col,  G[d].mod, "colsum"))    return 3;
        if (cuModuleGetFunction(&G[d].f_sgd,  G[d].mod, "sgd"))       return 3;
        G[d].dXs = 0;
        size_t s4 = (size_t)SHARD*D*4, o4 = (size_t)SHARD*O*4, h4 = (size_t)SHARD*H*4;
        cuMemAlloc(&G[d].dXs, s4); cuMemAlloc(&G[d].dTs, o4);
        cuMemAlloc(&G[d].dW1, D*H*4); cuMemAlloc(&G[d].db1, H*4);
        cuMemAlloc(&G[d].dW2, H*O*4); cuMemAlloc(&G[d].db2, O*4);
        cuMemAlloc(&G[d].dT1, h4); cuMemAlloc(&G[d].dHb, h4);
        cuMemAlloc(&G[d].dT2, o4); cuMemAlloc(&G[d].dYo, o4);
        cuMemAlloc(&G[d].ddY, o4); cuMemAlloc(&G[d].ddH, h4);
        cuMemAlloc(&G[d].ddW1, D*H*4); cuMemAlloc(&G[d].ddB1, H*4);
        cuMemAlloc(&G[d].ddW2, H*O*4); cuMemAlloc(&G[d].ddB2, O*4);
        /* shard d of the data */
        cuMemcpyHtoD(G[d].dXs, X + d*SHARD*D, s4);
        cuMemcpyHtoD(G[d].dTs, T + d*SHARD*O, o4);
        /* same initial weights on every GPU */
        cuMemcpyHtoD(G[d].dW1, W1, D*H*4); cuMemcpyHtoD(G[d].db1, b1, H*4);
        cuMemcpyHtoD(G[d].dW2, W2, H*O*4); cuMemcpyHtoD(G[d].db2, b2, O*4);
    }

    float invB = 1.0f / SHARD;   /* local batch = shard size */
    float hY[SHARD*O], hW1[D*H], hB1[H], hW2[H*O], hB2[O];
    float aW1[D*H]={0}, aB1[H]={0}, aW2[H*O]={0}, aB2[O]={0};
    struct timespec ts0; clock_gettime(CLOCK_MONOTONIC, &ts0);

    for (int it = 0; it < iters; it++) {
        memset(aW1,0,sizeof aW1); memset(aB1,0,sizeof aB1);
        memset(aW2,0,sizeof aW2); memset(aB2,0,sizeof aB2);
        double mse_sum = 0;
        for (int d = 0; d < ndev; d++) {
            cuCtxSetCurrent(G[d].ctx);
            fwd(&G[d]);
            bwd(&G[d], invB);
            cuCtxSynchronize();
            cuMemcpyDtoH(hY, G[d].dYo, (size_t)SHARD*O*4);
            for (int i = 0; i < SHARD*O; i++) { float e=hY[i]-T[d*SHARD+ i]; mse_sum += e*e; }
            cuMemcpyDtoH(hW1, G[d].ddW1, D*H*4); cuMemcpyDtoH(hB1, G[d].ddB1, H*4);
            cuMemcpyDtoH(hW2, G[d].ddW2, H*O*4); cuMemcpyDtoH(hB2, G[d].ddB2, O*4);
            for (int i=0;i<D*H;i++) aW1[i]+=hW1[i];
            for (int i=0;i<H;i++)   aB1[i]+=hB1[i];
            for (int i=0;i<H*O;i++) aW2[i]+=hW2[i];
            for (int i=0;i<O;i++)   aB2[i]+=hB2[i];
        }
        for (int i=0;i<D*H;i++) aW1[i]/=ndev;
        for (int i=0;i<H;i++)   aB1[i]/=ndev;
        for (int i=0;i<H*O;i++) aW2[i]/=ndev;
        for (int i=0;i<O;i++)   aB2[i]/=ndev;
        /* broadcast averaged grads + uniform update on every GPU */
        for (int d = 0; d < ndev; d++) {
            cuCtxSetCurrent(G[d].ctx);
            cuMemcpyHtoD(G[d].ddW1, aW1, D*H*4); cuMemcpyHtoD(G[d].ddB1, aB1, H*4);
            cuMemcpyHtoD(G[d].ddW2, aW2, H*O*4); cuMemcpyHtoD(G[d].ddB2, aB2, O*4);
            upd(&G[d]);
        }
        if (it % 20 == 0 || it == iters-1) {
            double mse = mse_sum / (ndev*SHARD*O);
            struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
            double el = (now.tv_sec-ts0.tv_sec)+(now.tv_nsec-ts0.tv_nsec)/1e9;
            fprintf(stderr, "  iter %3d  avg-MSE=%.6f  (%d GPU, %.1f ms elapsed)\n", it, mse, ndev, el*1e3);
        }
    }

    /* verify: GPU0 forward == CPU forward on final synced weights */
    cuCtxSetCurrent(G[0].ctx);
    float *W1h=malloc(D*H*4),*b1h=malloc(H*4),*W2h=malloc(H*O*4),*b2h=malloc(O*4);
    cuMemcpyDtoH(W1h,G[0].dW1,D*H*4); cuMemcpyDtoH(b1h,G[0].db1,H*4);
    cuMemcpyDtoH(W2h,G[0].dW2,H*O*4); cuMemcpyDtoH(b2h,G[0].db2,O*4);
    fwd(&G[0]); cuCtxSynchronize();
    cuMemcpyDtoH(hY, G[0].dYo, (size_t)SHARD*O*4);
    float hb[H]; for (int j=0;j<H;j++){ float s=b1h[j]; for(int d=0;d<D;d++) s+=X[d]*W1h[d*H+j]; hb[j]= s>0?s:0; }
    float ycpu = b2h[0]; for (int j=0;j<H;j++) ycpu += hb[j]*W2h[j];
    float maxerr = fabsf(hY[0]-ycpu);
    /* weight-sync check: GPU0 W1 == GPU1 W1 */
    float W1o[D*H]; cuCtxSetCurrent(G[1].ctx); cuMemcpyDtoH(W1o, G[1].dW1, D*H*4);
    float syncerr = 0; for (int i=0;i<D*H;i++){ float e=W1h[i]-W1o[i]; if(e<0)e=-e; if(e>syncerr)syncerr=e; }
    fprintf(stderr, "[mlp_dp] verify GPU0-fwd vs CPU-fwd |err|=%.2e %s ; GPU0==GPU1 weight-sync |err|=%.2e %s\n",
            maxerr, maxerr<1e-3?"OK":"BAD", syncerr, syncerr<1e-6?"OK":"BAD");
    fprintf(stderr, "[mlp_dp] DONE: %d-GPU data-parallel training converged under musl\n", ndev);
    for (int d = 0; d < ndev; d++) cuDevicePrimaryCtxRelease(G[d].dev);
    _exit(0);
}
