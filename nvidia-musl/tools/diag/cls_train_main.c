/* cls_train_main.c — MULTI-CLASS CLASSIFIER trained on GPU under musl, no toolkit.
 * A REAL model with a REAL metric: 2-input XOR (linearly inseparable) -> 2 classes
 * via 2->16(ReLU)->2(softmax). Softmax + cross-entropy; the headline number is
 * ACCURACY (fraction of samples classified correctly), not just loss. Reuses the
 * MLP kernels + softmax/dY_softmax (cls.cubin). _exit(0) dodges NVIDIA teardown
 * crash (report/13). usage: cls_train [path/to/libcuda.so] [iters]
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

typedef int CUresult;
typedef int CUdevice;
typedef void *CUcontext;
typedef void *CUmodule;
typedef void *CUfunction;
typedef void *CUstream;
typedef unsigned long long CUdeviceptr;

extern unsigned char cls_cubin[];
extern unsigned int cls_cubin_len;

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

#define B 1024
#define D 2
#define H 16
#define O 2
static float LR = 0.1f;

static CUcontext g_ctx; static CUmodule g_mod;
static CUfunction g_gemm, g_epi, g_mask, g_col, g_sgd, g_softmax, g_dy;
static CUdeviceptr g_X, g_Tgt, g_W1, g_b1, g_W2, g_b2, g_T1, g_Hb, g_T2, g_Logit, g_Prob, g_DY, g_DH, g_dW1, g_dB1, g_dW2, g_dB2;

static void g2(CUfunction f, CUdeviceptr A, CUdeviceptr Bm, CUdeviceptr C, int M, int N, int K, int tA, int tB) {
    void *args[] = { &A, &Bm, &C, &M, &N, &K, &tA, &tB };
    cuLaunchKernel(f, (N + 15) / 16, (M + 15) / 16, 1, 16, 16, 1, 0, NULL, args, NULL);
}
static void g1(CUfunction f, size_t n, void *args[]) {
    cuLaunchKernel(f, (n + 255) / 256, 1, 1, 256, 1, 1, 0, NULL, args, NULL);
}
static void gc(CUfunction f, CUdeviceptr X, CUdeviceptr out, int M, int N) {
    void *args[] = { &X, &out, &M, &N };
    cuLaunchKernel(f, (N + 255) / 256, 1, 1, 256, 1, 1, 0, NULL, args, NULL);
}

int main(int argc, char **argv) {
    const char *lib = argc > 1 ? argv[1] : "libcuda.so.1";
    int iters = argc > 2 ? atoi(argv[2]) : 300;
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

    /* XOR 2-class dataset */
    float *X = malloc(B * D * 4);
    int *Tgt = malloc(B * 4);
    for (int i = 0; i < B; i++) {
        X[i*D+0] = sinf(i * 0.37f) * 2.0f;   /* in [-2,2] */
        X[i*D+1] = cosf(i * 0.53f) * 2.0f;
        int s0 = X[i*D+0] > 0.f, s1 = X[i*D+1] > 0.f;
        Tgt[i] = s0 ^ s1;                    /* XOR */
    }
    float *W1 = malloc(D*H*4), *b1 = malloc(H*4), *W2 = malloc(H*O*4), *b2 = malloc(O*4);
    for (int i = 0; i < D*H; i++) W1[i] = 0.2f * (((i*7)%11)-5)/5.0f;
    for (int i = 0; i < H; i++)   b1[i] = 0.1f * (((i*3)%7)-3)/3.0f;
    for (int i = 0; i < H*O; i++) W2[i] = 0.2f * (((i*5)%9)-4)/4.0f;
    for (int i = 0; i < O; i++)   b2[i] = 0.0f;
    char *cubin = malloc(cls_cubin_len + 1);
    memcpy(cubin, cls_cubin, cls_cubin_len); cubin[cls_cubin_len] = 0;

    if (cuInit(0)) { fprintf(stderr, "cuInit failed\n"); return 1; }
    int ndev = 0; cuDeviceGetCount(&ndev);
    if (!ndev) { fprintf(stderr, "no GPUs\n"); return 1; }
    CUdevice dev; cuDeviceGet(&dev, 0);
    char name[64]; cuDeviceGetName(name, sizeof name, dev);
    cuDevicePrimaryCtxRetain(&g_ctx, dev); cuCtxSetCurrent(g_ctx);
    cuModuleLoadData(&g_mod, cubin);
    if (cuModuleGetFunction(&g_gemm,    g_mod, "gemm2"))       return 3;
    if (cuModuleGetFunction(&g_epi,     g_mod, "epi"))         return 3;
    if (cuModuleGetFunction(&g_mask,    g_mod, "relu_mask"))   return 3;
    if (cuModuleGetFunction(&g_col,     g_mod, "colsum"))      return 3;
    if (cuModuleGetFunction(&g_sgd,     g_mod, "sgd"))         return 3;
    if (cuModuleGetFunction(&g_softmax, g_mod, "softmax_k"))   return 3;
    if (cuModuleGetFunction(&g_dy,      g_mod, "dY_softmax"))  return 3;

    cuMemAlloc(&g_X, (size_t)B*D*4); cuMemAlloc(&g_Tgt, (size_t)B*4);
    cuMemAlloc(&g_W1, D*H*4); cuMemAlloc(&g_b1, H*4);
    cuMemAlloc(&g_W2, H*O*4); cuMemAlloc(&g_b2, O*4);
    cuMemAlloc(&g_T1, (size_t)B*H*4); cuMemAlloc(&g_Hb, (size_t)B*H*4);
    cuMemAlloc(&g_T2, (size_t)B*O*4); cuMemAlloc(&g_Logit, (size_t)B*O*4);
    cuMemAlloc(&g_Prob, (size_t)B*O*4); cuMemAlloc(&g_DY, (size_t)B*O*4);
    cuMemAlloc(&g_DH, (size_t)B*H*4);
    cuMemAlloc(&g_dW1, D*H*4); cuMemAlloc(&g_dB1, H*4);
    cuMemAlloc(&g_dW2, H*O*4); cuMemAlloc(&g_dB2, O*4);
    cuMemcpyHtoD(g_X, X, (size_t)B*D*4);
    cuMemcpyHtoD(g_Tgt, Tgt, (size_t)B*4);
    cuMemcpyHtoD(g_W1, W1, D*H*4); cuMemcpyHtoD(g_b1, b1, H*4);
    cuMemcpyHtoD(g_W2, W2, H*O*4); cuMemcpyHtoD(g_b2, b2, O*4);
    fprintf(stderr, "[cls] %s — XOR classifier 2->16->2(softmax), B=%d, iters=%d\n", name, B, iters);

    float invB = 1.0f / B;
    float *hProb = malloc((size_t)B*O*4);
    for (int it = 0; it < iters; it++) {
        /* forward: X@W1 -> relu+b1 -> H ; H@W2 -> +b2 -> logits ; softmax -> prob */
        g2(g_gemm, g_X, g_W1, g_T1, B, H, D, 0, 0);
        { int M=B,N=H,relu=1; void *a[]={&g_T1,&g_b1,&g_Hb,&M,&N,&relu}; g1(g_epi,(size_t)B*H,a); }
        g2(g_gemm, g_Hb, g_W2, g_T2, B, O, H, 0, 0);
        { int M=B,N=O,relu=0; void *a[]={&g_T2,&g_b2,&g_Logit,&M,&N,&relu}; g1(g_epi,(size_t)B*O,a); }
        { int M=B,N=O; void *a[]={&g_Logit,&g_Prob,&M,&N}; g1(g_softmax,(size_t)B,a); }
        cuCtxSynchronize();
        /* accuracy + loss on host */
        cuMemcpyDtoH(hProb, g_Prob, (size_t)B*O*4);
        int correct = 0; double ce = 0;
        for (int i = 0; i < B; i++) {
            int pred = hProb[i*O+0] >= hProb[i*O+1] ? 0 : 1;
            if (pred == Tgt[i]) correct++;
            double pt = (double)(Tgt[i]==0 ? hProb[i*O+0] : hProb[i*O+1]);
            if (pt > 1.0) pt = 1.0;
            if (pt < 1e-12) pt = 1e-12;
            ce += -log(pt);
        }
        /* backward: dY=(p-onehot)/B ; dW2 ; dB2 ; dH ; mask ; dW1 ; dB1 */
        { int M=B,N=O; void *a[]={&g_Prob,&g_Tgt,&g_DY,&M,&N,&invB}; g1(g_dy,(size_t)B*O,a); }
        g2(g_gemm, g_Hb, g_DY, g_dW2, H, O, B, 1, 0);
        gc(g_col, g_DY, g_dB2, B, O);
        g2(g_gemm, g_DY, g_W2, g_DH, B, H, O, 0, 1);
        { int n=B*H; void *a[]={&g_DH,&g_Hb,&n}; g1(g_mask,(size_t)B*H,a); }
        g2(g_gemm, g_X, g_DH, g_dW1, D, H, B, 1, 0);
        gc(g_col, g_DH, g_dB1, B, H);
        { int n=D*H; void *a[]={&g_W1,&g_dW1,&n,&LR}; g1(g_sgd,(size_t)D*H,a); }
        { int n=H;   void *a[]={&g_b1,&g_dB1,&n,&LR}; g1(g_sgd,(size_t)H,a); }
        { int n=H*O; void *a[]={&g_W2,&g_dW2,&n,&LR}; g1(g_sgd,(size_t)H*O,a); }
        { int n=O;   void *a[]={&g_b2,&g_dB2,&n,&LR}; g1(g_sgd,(size_t)O,a); }
        if (it % 30 == 0 || it == iters-1)
            fprintf(stderr, "  iter %3d  acc=%d/%d (%.1f%%)  CE=%.4f\n",
                    it, correct, B, 100.0*correct/B, ce/B);
    }
    cuDevicePrimaryCtxRelease(dev);
    fprintf(stderr, "[cls] DONE: XOR classifier trained on GPU under musl (accuracy metric)\n");
    _exit(0);
}
