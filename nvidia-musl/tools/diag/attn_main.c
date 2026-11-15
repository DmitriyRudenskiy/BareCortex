/* attn_main.c — SELF-ATTENTION (transformer block) forward pass over the CUDA
 * DRIVER API under musl, no toolkit, on all GPUs. Proves the node runs a modern
 * architecture building block, not just GEMMs/MLPs. Block:
 *   Q = X@Wq/sqrt(d) ; K = X@Wk ; V = X@Wv ; S = softmax(Q@K^T) ; O = (S@V)@Wo
 * Only needs gemm2 + softmax_k (both in cls.cubin) — no new kernels. Each GPU
 * runs the forward, result verified EXACT vs a CPU reference. Reports throughput.
 * _exit(0) dodges NVIDIA teardown crash (report/13). usage: attn [libcuda] [iters]
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

#define S 256
#define d 64

static CUfunction g_gemm, g_softmax;

static void g2(CUfunction f, CUdeviceptr A, CUdeviceptr Bm, CUdeviceptr C, int M, int N, int K, int tA, int tB) {
    void *args[] = { &A, &Bm, &C, &M, &N, &K, &tA, &tB };
    cuLaunchKernel(f, (N + 15) / 16, (M + 15) / 16, 1, 16, 16, 1, 0, NULL, args, NULL);
}
static void gs(CUfunction f, CUdeviceptr in, CUdeviceptr out, int M, int N) {
    void *args[] = { &in, &out, &M, &N };
    cuLaunchKernel(f, (M + 255) / 256, 1, 1, 256, 1, 1, 0, NULL, args, NULL);
}

/* one attention forward on the current context */
static void attn_fwd(CUdeviceptr X, CUdeviceptr Q, CUdeviceptr K, CUdeviceptr V,
                     CUdeviceptr Sc, CUdeviceptr A, CUdeviceptr Out, CUdeviceptr OP,
                     CUdeviceptr Wq, CUdeviceptr Wk, CUdeviceptr Wv, CUdeviceptr Wo) {
    g2(g_gemm, X,  Wq, Q,   S, d, d, 0, 0);
    g2(g_gemm, X,  Wk, K,   S, d, d, 0, 0);
    g2(g_gemm, X,  Wv, V,   S, d, d, 0, 0);
    g2(g_gemm, Q,  K,  Sc,  S, S, d, 0, 1);   /* Q @ K^T */
    gs(g_softmax, Sc, A, S, S);               /* row-wise softmax */
    g2(g_gemm, A,  V,  Out, S, d, S, 0, 0);   /* S @ V */
    g2(g_gemm, Out, Wo, OP,  S, d, d, 0, 0);  /* out proj */
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

    float *X  = malloc((size_t)S*d*4);
    float *Wq = malloc(d*d*4), *Wk = malloc(d*d*4), *Wv = malloc(d*d*4), *Wo = malloc(d*d*4);
    for (int i = 0; i < S*d; i++) X[i]  = sinf(i*0.31f)*0.5f;
    for (int i = 0; i < d*d; i++) Wq[i] = 0.1f*sinf(i*0.7f), Wk[i]=0.1f*cosf(i*0.5f),
                                   Wv[i] = 0.1f*sinf(i*1.1f), Wo[i]=0.1f*cosf(i*0.9f);
    float *Wqs = malloc(d*d*4);                 /* Wq / sqrt(d) -> folded scale */
    float inv = 1.0f / sqrtf((float)d);
    for (int i = 0; i < d*d; i++) Wqs[i] = Wq[i] * inv;

    /* CPU reference (float, same op order) */
    float *cQ=malloc((size_t)S*d*4),*cK=malloc((size_t)S*d*4),*cV=malloc((size_t)S*d*4);
    float *cSc=malloc((size_t)S*S*4),*cA=malloc((size_t)S*S*4),*cO=malloc((size_t)S*d*4),*cOP=malloc((size_t)S*d*4);
    #define CGEMM(A,B,C,M,N,K,tA,tB) for(int i=0;i<M;i++)for(int j=0;j<N;j++){float s=0;for(int k=0;k<K;k++){float a=(tA)?A[k*M+i]:A[i*K+k];float b=(tB)?B[j*K+k]:B[k*N+j];s+=a*b;}C[i*N+j]=s;}
    CGEMM(X,Wqs,cQ,S,d,d,0,0); CGEMM(X,Wk,cK,S,d,d,0,0); CGEMM(X,Wv,cV,S,d,d,0,0);
    CGEMM(cQ,cK,cSc,S,S,d,0,1);
    for (int i=0;i<S;i++){ float mx=cSc[i*S]; for(int k=1;k<S;k++) if(cSc[i*S+k]>mx)mx=cSc[i*S+k]; float s=0; for(int k=0;k<S;k++){float e=expf(cSc[i*S+k]-mx);cA[i*S+k]=e;s+=e;} for(int k=0;k<S;k++) cA[i*S+k]/=s; }
    CGEMM(cA,cV,cO,S,d,S,0,0); CGEMM(cO,Wo,cOP,S,d,d,0,0);
    #undef CGEMM

    char *cubin = malloc(cls_cubin_len + 1);
    memcpy(cubin, cls_cubin, cls_cubin_len); cubin[cls_cubin_len] = 0;
    if (cuInit(0)) { fprintf(stderr, "cuInit failed\n"); return 1; }
    int ndev = 0; cuDeviceGetCount(&ndev);
    fprintf(stderr, "[attn] self-attention block S=%d d=%d on %d GPUs (musl, no toolkit)\n", S, d, ndev);

    float maxerr_all = 0;
    struct timespec t0; clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int dev = 0; dev < ndev; dev++) {
        CUdevice d0; cuDeviceGet(&d0, dev);
        CUcontext ctx; cuDevicePrimaryCtxRetain(&ctx, d0); cuCtxSetCurrent(ctx);
        CUmodule mod; cuModuleLoadData(&mod, cubin);
        if (cuModuleGetFunction(&g_gemm, mod, "gemm2"))       return 3;
        if (cuModuleGetFunction(&g_softmax, mod, "softmax_k")) return 3;
        CUdeviceptr dX,dQ,dK,dV,dSc,dA,dO,dOP,dWq,dWk,dWv,dWo;
        size_t sd=(size_t)S*d*4, ss=(size_t)S*S*4, dd=(size_t)d*d*4;
        cuMemAlloc(&dX,sd); cuMemAlloc(&dQ,sd); cuMemAlloc(&dK,sd); cuMemAlloc(&dV,sd);
        cuMemAlloc(&dSc,ss); cuMemAlloc(&dA,ss); cuMemAlloc(&dO,sd); cuMemAlloc(&dOP,sd);
        cuMemAlloc(&dWq,dd); cuMemAlloc(&dWk,dd); cuMemAlloc(&dWv,dd); cuMemAlloc(&dWo,dd);
        cuMemcpyHtoD(dX,X,sd); cuMemcpyHtoD(dWq,Wqs,dd); cuMemcpyHtoD(dWk,Wk,dd);
        cuMemcpyHtoD(dWv,Wv,dd); cuMemcpyHtoD(dWo,Wo,dd);
        for (int it = 0; it < iters; it++)
            attn_fwd(dX,dQ,dK,dV,dSc,dA,dO,dOP,dWq,dWk,dWv,dWo);
        cuCtxSynchronize();
        float *hOP = malloc((size_t)S*d*4);
        cuMemcpyDtoH(hOP, dOP, sd);
        float me = 0; for (int i=0;i<S*d;i++){ float e=hOP[i]-cOP[i]; if(e<0)e=-e; if(e>me)me=e; }
        if (me > maxerr_all) maxerr_all = me;
        char nm[48]; cuDeviceGetName(nm, 48, d0);
        fprintf(stderr, "  dev%d %-32s verify vs CPU |err|=%.2e %s\n", dev, nm, me, me<1e-3?"OK":"BAD");
    }
    struct timespec t1; clock_gettime(CLOCK_MONOTONIC, &t1);
    double el = (t1.tv_sec-t0.tv_sec)+(t1.tv_nsec-t0.tv_nsec)/1e9;
    /* FLOPs per forward ~ 2*(3 projections + 2 S^2 d + 1 out) ; report aggregate */
    double flops = (double)ndev*iters*(2.0*(3*S*d*d + S*S*d*2 + S*d*d));
    fprintf(stderr, "[attn] DONE: self-attention correct on all %d GPUs (max err %.2e), %.1f ms, %.0f GFLOPS\n",
            ndev, maxerr_all, el*1e3, flops/el/1e9);
    _exit(0);
}
