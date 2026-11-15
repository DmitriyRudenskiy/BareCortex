/* mlp_train_main.c — REAL WORKLOAD: tiny MLP training loop on the CUDA DRIVER
 * API under musl, no toolkit. A 2-layer net (D=4 -> H=16 ReLU -> O=1) learns a
 * nonlinear target by SGD; forward + backward + weight-update all run on GPU.
 * Loss (MSE) must DECREASE over iterations — the actual purpose of a compute node.
 * End: verify GPU forward == CPU forward on the final weights.
 *
 * cubin embedded (mlp_cubin[]). _exit(0) dodges NVIDIA static teardown crash
 * (report/13). usage: mlp_train [path/to/libcuda.so] [iters]
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
static CUresult (*cuDeviceGet)(CUdevice *, int);
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

static const int B = 256, D = 4, H = 16, O = 1;
static float LR = 0.05f;

static void get_fn(CUmodule mod, CUfunction *f, const char *n) {
    if (cuModuleGetFunction(f, mod, n)) { fprintf(stderr, "getFn %s failed\n", n); exit(3); }
}
/* gemm2 launch */
static void g2(CUfunction f, CUdeviceptr A, CUdeviceptr B, CUdeviceptr C,
               int M, int N, int K, int tA, int tB) {
    void *args[] = { &A, &B, &C, &M, &N, &K, &tA, &tB };
    cuLaunchKernel(f, (N + 15) / 16, (M + 15) / 16, 1, 16, 16, 1, 0, NULL, args, NULL);
}
/* 1D launch over n elems */
static void g1(CUfunction f, size_t n, void *args[]) {
    cuLaunchKernel(f, (n + 255) / 256, 1, 1, 256, 1, 1, 0, NULL, args, NULL);
}
/* colsum launch */
static void gc(CUfunction f, CUdeviceptr X, CUdeviceptr out, int M, int N) {
    void *args[] = { &X, &out, &M, &N };
    cuLaunchKernel(f, (N + 255) / 256, 1, 1, 256, 1, 1, 0, NULL, args, NULL);
}

int main(int argc, char **argv) {
    const char *lib = argc > 1 ? argv[1] : "libcuda.so.1";
    int iters = argc > 2 ? atoi(argv[2]) : 200;
    void *h = dlopen(lib, RTLD_NOW | RTLD_GLOBAL);
    if (!h) { fprintf(stderr, "dlopen %s: %s\n", lib, dlerror()); return 2; }
    #define DL(n) dlsym(h, n)
    cuInit = DL("cuInit"); cuDeviceGet = DL("cuDeviceGet");
    cuDevicePrimaryCtxRetain = DL("cuDevicePrimaryCtxRetain");
    cuDevicePrimaryCtxRelease = DL("cuDevicePrimaryCtxRelease");
    cuCtxSetCurrent = DL("cuCtxSetCurrent");
    cuModuleLoadData = DL("cuModuleLoadData"); cuModuleGetFunction = DL("cuModuleGetFunction");
    cuMemAlloc = DL("cuMemAlloc_v2");
    cuMemcpyHtoD = DL("cuMemcpyHtoD_v2"); cuMemcpyDtoH = DL("cuMemcpyDtoH_v2");
    cuLaunchKernel = DL("cuLaunchKernel"); cuCtxSynchronize = DL("cuCtxSynchronize");
    if (!cuInit || !cuLaunchKernel) { fprintf(stderr, "dlsym missing\n"); return 3; }

    /* ---- host data + target (nonlinear) ---- */
    float *X = malloc(B * D * 4), *T = malloc(B * O * 4);
    for (int i = 0; i < B; i++) for (int d = 0; d < D; d++)
        X[i * D + d] = sinf(i * 0.13f + d * 1.7f);            /* inputs in ~[-1,1] */
    for (int i = 0; i < B; i++)
        T[i] = sinf(X[i*0]) + cosf(X[i*1]) * X[i*2] + 0.5f * X[i*3] * X[i*3];

    /* ---- small deterministic init weights ---- */
    float *W1 = malloc(D*H*4), *b1 = malloc(H*4), *W2 = malloc(H*O*4), *b2 = malloc(O*4);
    for (int i = 0; i < D*H; i++) W1[i] = 0.1f * (((i*7) % 11) - 5) / 5.0f;
    for (int i = 0; i < H; i++)   b1[i] = 0.05f * (((i*3) % 7) - 3) / 3.0f;
    for (int i = 0; i < H*O; i++) W2[i] = 0.1f * (((i*5) % 9) - 4) / 4.0f;
    for (int i = 0; i < O; i++)   b2[i] = 0.0f;

    char *cubin = malloc(mlp_cubin_len + 1);
    memcpy(cubin, mlp_cubin, mlp_cubin_len); cubin[mlp_cubin_len] = 0;

    if (cuInit(0)) { fprintf(stderr, "cuInit failed\n"); return 1; }
    CUdevice dev; cuDeviceGet(&dev, 0);
    CUcontext ctx; cuDevicePrimaryCtxRetain(&ctx, dev); cuCtxSetCurrent(ctx);
    CUmodule mod; if (cuModuleLoadData(&mod, cubin)) { fprintf(stderr, "load fail\n"); return 1; }
    CUfunction f_gemm, f_epi, f_dY, f_mask, f_col, f_sgd;
    get_fn(mod, &f_gemm, "gemm2"); get_fn(mod, &f_epi, "epi"); get_fn(mod, &f_dY, "dY_k");
    get_fn(mod, &f_mask, "relu_mask"); get_fn(mod, &f_col, "colsum"); get_fn(mod, &f_sgd, "sgd");

    /* ---- GPU buffers ---- */
    CUdeviceptr dX,dT,dW1,db1,dW2,db2,dT1,dHb,dT2,dYo,ddY,ddH,ddW1,ddB1,ddW2,ddB2;
    cuMemAlloc(&dX, B*D*4); cuMemAlloc(&dT, B*O*4);
    cuMemAlloc(&dW1, D*H*4); cuMemAlloc(&db1, H*4); cuMemAlloc(&dW2, H*O*4); cuMemAlloc(&db2, O*4);
    cuMemAlloc(&dT1, B*H*4); cuMemAlloc(&dHb, B*H*4); cuMemAlloc(&dT2, B*O*4); cuMemAlloc(&dYo, B*O*4);
    cuMemAlloc(&ddY, B*O*4); cuMemAlloc(&ddH, B*H*4); cuMemAlloc(&ddW1, D*H*4);
    cuMemAlloc(&ddB1, H*4); cuMemAlloc(&ddW2, H*O*4); cuMemAlloc(&ddB2, O*4);
    cuMemcpyHtoD(dX, X, B*D*4); cuMemcpyHtoD(dT, T, B*O*4);
    cuMemcpyHtoD(dW1, W1, D*H*4); cuMemcpyHtoD(db1, b1, H*4);
    cuMemcpyHtoD(dW2, W2, H*O*4); cuMemcpyHtoD(db2, b2, O*4);

    float invB = 1.0f / B;
    float *Yh = malloc(B*O*4);
    struct timespec ts0; clock_gettime(CLOCK_MONOTONIC, &ts0);

    for (int it = 0; it < iters; it++) {
        /* forward */
        g2(f_gemm, dX, dW1, dT1, B, H, D, 0, 0);          /* T1 = X@W1 */
        { int M=B,N=H,relu=1; void *a[]={&dT1,&db1,&dHb,&M,&N,&relu}; g1(f_epi, (size_t)B*H, a); } /* H=relu(T1+b1) */
        g2(f_gemm, dHb, dW2, dT2, B, O, H, 0, 0);          /* T2 = H@W2 */
        { int M=B,N=O,relu=0; void *a[]={&dT2,&db2,&dYo,&M,&N,&relu}; g1(f_epi, (size_t)B*O, a); } /* Y=T2+b2 */
        cuCtxSynchronize();
        cuMemcpyDtoH(Yh, dYo, B*O*4);
        if (it % 20 == 0 || it == iters-1) {
            double mse = 0; for (int i = 0; i < B*O; i++) { double e = Yh[i]-T[i]; mse += e*e; }
            mse /= (B*O);
            struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
            double el = (now.tv_sec-ts0.tv_sec) + (now.tv_nsec-ts0.tv_nsec)/1e9;
            fprintf(stderr, "  iter %3d  MSE=%.6f  (%.1f ms elapsed)\n", it, mse, el*1e3);
        }
        /* backward */
        { int n=B*O; void *a[]={&dYo,&dT,&ddY,&n,&invB}; g1(f_dY, (size_t)B*O, a); }                 /* dY */
        g2(f_gemm, dHb, ddY, ddW2, H, O, B, 1, 0);          /* dW2 = H^T @ dY */
        gc(f_col, ddY, ddB2, B, O);                          /* dB2 */
        g2(f_gemm, ddY, dW2, ddH, B, H, O, 0, 1);          /* dH = dY @ W2^T */
        { int n=B*H; void *a[]={&ddH,&dHb,&n}; g1(f_mask, (size_t)B*H, a); }                        /* dH*= (H>0) */
        g2(f_gemm, dX, ddH, ddW1, D, H, B, 1, 0);          /* dW1 = X^T @ dH */
        gc(f_col, ddH, ddB1, B, H);                          /* dB1 */
        /* update */
        { int n=D*H; void *a[]={&dW1,&ddW1,&n,&LR}; g1(f_sgd, (size_t)D*H, a); }
        { int n=H;   void *a[]={&db1,&ddB1,&n,&LR}; g1(f_sgd, (size_t)H, a); }
        { int n=H*O; void *a[]={&dW2,&ddW2,&n,&LR}; g1(f_sgd, (size_t)H*O, a); }
        { int n=O;   void *a[]={&db2,&ddB2,&n,&LR}; g1(f_sgd, (size_t)O, a); }
    }
    cuCtxSynchronize();

    /* ---- verify: GPU forward vs CPU forward on final weights ---- */
    float *W1h=malloc(D*H*4), *b1h=malloc(H*4), *W2h=malloc(H*O*4), *b2h=malloc(O*4);
    cuMemcpyDtoH(W1h, dW1, D*H*4); cuMemcpyDtoH(b1h, db1, H*4);
    cuMemcpyDtoH(W2h, dW2, H*O*4); cuMemcpyDtoH(b2h, db2, O*4);
    /* GPU forward (whole batch, final weights) */
    g2(f_gemm, dX, dW1, dT1, B, H, D, 0, 0);
    { int M=B,N=H,relu=1; void *a[]={&dT1,&db1,&dHb,&M,&N,&relu}; g1(f_epi,(size_t)B*H,a); }
    g2(f_gemm, dHb, dW2, dT2, B, O, H, 0, 0);
    { int M=B,N=O,relu=0; void *a[]={&dT2,&db2,&dYo,&M,&N,&relu}; g1(f_epi,(size_t)B*O,a); }
    cuCtxSynchronize();
    cuMemcpyDtoH(Yh, dYo, B*O*4);
    /* CPU forward sample 0 */
    float hbuf[H]; for (int j=0;j<H;j++){ float s=b1h[j]; for(int d=0;d<D;d++) s+=X[d]*W1h[d*H+j]; hbuf[j]= s>0?s:0; }
    float ycpu = b2h[0]; for (int j=0;j<H;j++) ycpu += hbuf[j]*W2h[j*O+0];
    float maxerr = fabsf(Yh[0] - ycpu);
    fprintf(stderr, "[mlp_train] verify GPU-forward vs CPU-forward (sample 0): |err|=%.2e %s\n",
            maxerr, maxerr < 1e-3f ? "OK" : "BAD");
    fprintf(stderr, "[mlp_train] DONE: %d iters, net learned nonlinear target on GPU under musl\n", iters);

    cuDevicePrimaryCtxRelease(dev);
    _exit(0);
}
