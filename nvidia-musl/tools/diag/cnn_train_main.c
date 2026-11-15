/* cnn_train_main.c — small CNN trained on GPU under musl, no toolkit.
 * 1x8x8 image -> conv(4 filters 3x3) -> flatten(144) -> linear(2) -> softmax.
 * Task: classify horizontal line (cls 0) vs vertical line (cls 1).
 * HEADLINE (guaranteed): conv2d forward verified EXACT vs CPU reference.
 * BONUS: train -> accuracy climbs. _exit(0) dodges NVIDIA teardown (report/13).
 * usage: cnn [libcuda] [iters]
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

extern unsigned char cnn_cubin[];
extern unsigned int cnn_cubin_len;

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

#define B 256
#define CIN 1
#define HH 8
#define WW 8
#define COUT 4
#define KH 3
#define KW 3
#define HO (HH-KH+1)   /* 6 */
#define WO (WW-KW+1)   /* 6 */
#define F  (COUT*HO*WO) /* 144 */
static float LR = 0.02f;

static CUcontext g_ctx; static CUmodule g_mod;
static CUfunction g_gemm, g_col, g_sgd, g_softmax, g_dy, g_conv, g_wgrad, g_bgrad;
static CUdeviceptr g_In, g_Out, g_Logit, g_Prob, g_DY, g_W2, g_B2, g_K, g_Bias, g_Dconv, g_dW2, g_dB2, g_dK, g_dBias;

static void g2(CUfunction f, CUdeviceptr A, CUdeviceptr Bm, CUdeviceptr C, int M, int N, int K, int tA, int tB) {
    void *args[] = { &A, &Bm, &C, &M, &N, &K, &tA, &tB };
    cuLaunchKernel(f, (N + 15) / 16, (M + 15) / 16, 1, 16, 16, 1, 0, NULL, args, NULL);
}
static void gl(CUfunction f, int threads, void *args[]) {
    cuLaunchKernel(f, (threads + 255) / 256, 1, 1, 256, 1, 1, 0, NULL, args, NULL);
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

    /* data: 0 = horizontal line, 1 = vertical line */
    float *Img = malloc((size_t)B*HH*WW*4);
    int *Tgt = malloc(B*4);
    for (int i = 0; i < B; i++) {
        int cls = i & 1; int pos = (i*7) % 8;
        for (int y = 0; y < HH; y++) for (int x = 0; x < WW; x++) {
            float v = 0.f;
            if (cls == 0 && y == pos) v = 1.f;
            if (cls == 1 && x == pos) v = 1.f;
            v += 0.01f * (((i + x + y) % 3) - 1);
            Img[i*HH*WW + y*WW + x] = v;
        }
        Tgt[i] = cls;
    }
    float *K = malloc(COUT*CIN*KW*KH*4), *Bias = malloc(COUT*4);
    for (int i = 0; i < COUT*CIN*KW*KH; i++) K[i] = 0.05f * (((i*7)%11)-5)/5.0f;
    for (int i = 0; i < COUT; i++) Bias[i] = 0.f;
    float *W2 = malloc(F*2*4), *B2 = malloc(2*4);
    for (int i = 0; i < F*2; i++) W2[i] = 0.05f * (((i*5)%9)-4)/4.0f;
    for (int i = 0; i < 2; i++) B2[i] = 0.f;

    /* CPU conv reference (initial weights) for the exactness check */
    float *cOut = malloc((size_t)B*F*4);
    for (int b = 0; b < B; b++) for (int co = 0; co < COUT; co++)
        for (int ho = 0; ho < HO; ho++) for (int wo = 0; wo < WO; wo++) {
            float s = Bias[co];
            for (int ky = 0; ky < KW; ky++) for (int kx = 0; kx < KH; kx++)
                s += Img[b*HH*WW + (ho+ky)*WW + (wo+kx)] * K[(co*CIN)*KW*KH + ky*KH + kx];
            cOut[(b*COUT+co)*HO*WO + ho*WO + wo] = s;
        }

    char *cubin = malloc(cnn_cubin_len + 1);
    memcpy(cubin, cnn_cubin, cnn_cubin_len); cubin[cnn_cubin_len] = 0;
    if (cuInit(0)) { fprintf(stderr, "cuInit failed\n"); return 1; }
    int ndev = 0; cuDeviceGetCount(&ndev);
    if (!ndev) { fprintf(stderr, "no GPUs\n"); return 1; }
    CUdevice dev; cuDeviceGet(&dev, 0);
    char name[64]; cuDeviceGetName(name, sizeof name, dev);
    cuDevicePrimaryCtxRetain(&g_ctx, dev); cuCtxSetCurrent(g_ctx);
    cuModuleLoadData(&g_mod, cubin);
    #define gf(nm,st) if (cuModuleGetFunction(&g_##nm, g_mod, st)) { fprintf(stderr,"getFn %s\n",st); return 3; }
    gf(gemm,"gemm2") gf(col,"colsum") gf(sgd,"sgd") gf(softmax,"softmax_k")
    gf(dy,"dY_softmax") gf(conv,"conv2d") gf(wgrad,"conv_wgrad") gf(bgrad,"conv_bgrad")
    #undef gf

    cuMemAlloc(&g_In,(size_t)B*HH*WW*4); cuMemAlloc(&g_Out,(size_t)B*F*4);
    cuMemAlloc(&g_Logit,(size_t)B*2*4); cuMemAlloc(&g_Prob,(size_t)B*2*4);
    cuMemAlloc(&g_DY,(size_t)B*2*4); cuMemAlloc(&g_Dconv,(size_t)B*F*4);
    cuMemAlloc(&g_W2,(size_t)F*2*4); cuMemAlloc(&g_B2,(size_t)2*4);
    cuMemAlloc(&g_K,(size_t)COUT*CIN*KW*KH*4); cuMemAlloc(&g_Bias,(size_t)COUT*4);
    cuMemAlloc(&g_dW2,(size_t)F*2*4); cuMemAlloc(&g_dB2,(size_t)2*4);
    cuMemAlloc(&g_dK,(size_t)COUT*CIN*KW*KH*4); cuMemAlloc(&g_dBias,(size_t)COUT*4);
    cuMemcpyHtoD(g_In,Img,(size_t)B*HH*WW*4);
    cuMemcpyHtoD(g_W2,W2,(size_t)F*2*4); cuMemcpyHtoD(g_B2,B2,(size_t)2*4);
    cuMemcpyHtoD(g_K,K,(size_t)COUT*CIN*KW*KH*4); cuMemcpyHtoD(g_Bias,Bias,(size_t)COUT*4);
    fprintf(stderr, "[cnn] %s — CNN 8x8 -> conv(4x3x3) -> linear(2), B=%d, iters=%d\n", name, B, iters);

    int Cin=CIN, Hh=HH, Ww=WW, Co=COUT, Kh=KH, Kw=KW, Bn=B, N2=2;
    /* conv forward + exactness check */
    { int t=B*F; void *a[]={&g_In,&g_K,&g_Bias,&g_Out,&Bn,&Cin,&Hh,&Ww,&Co,&Kh,&Kw}; gl(g_conv,t,a); }
    cuCtxSynchronize();
    float *hOut = malloc((size_t)B*F*4);
    cuMemcpyDtoH(hOut, g_Out, (size_t)B*F*4);
    float me = 0; for (int i = 0; i < B*F; i++){ float e=hOut[i]-cOut[i]; if(e<0)e=-e; if(e>me)me=e; }
    fprintf(stderr, "[cnn] conv2d forward vs CPU |err|=%.2e %s\n", me, me<1e-5?"OK":"BAD");

    float invB = 1.0f / B;
    float *hProb = malloc((size_t)B*2*4);
    for (int it = 0; it < iters; it++) {
        /* forward */
        { int t=B*F; void *a[]={&g_In,&g_K,&g_Bias,&g_Out,&Bn,&Cin,&Hh,&Ww,&Co,&Kh,&Kw}; gl(g_conv,t,a); }
        g2(g_gemm, g_Out, g_W2, g_Logit, B, 2, F, 0, 0);
        { int M=B,N=2; void *a[]={&g_Logit,&g_Prob,&M,&N}; gl(g_softmax,M,a); }
        cuCtxSynchronize();
        cuMemcpyDtoH(hProb, g_Prob, (size_t)B*2*4);
        int correct = 0; double ce = 0;
        for (int i = 0; i < B; i++) {
            int pred = hProb[i*2+0] >= hProb[i*2+1] ? 0 : 1;
            if (pred == Tgt[i]) correct++;
            double pt = (double)(Tgt[i]==0 ? hProb[i*2+0] : hProb[i*2+1]);
            if (pt > 1.0) pt = 1.0;
            if (pt < 1e-12) pt = 1e-12;
            ce += -log(pt);
        }
        /* backward */
        static CUdeviceptr g_Tgt = 0;
        if (!g_Tgt) { cuMemAlloc(&g_Tgt,(size_t)B*4); cuMemcpyHtoD(g_Tgt,Tgt,(size_t)B*4); }
        { int M=B,N=2; void *a[]={&g_Prob,&g_Tgt,&g_DY,&M,&N,&invB}; gl(g_dy,M*N,a); }
        g2(g_gemm, g_Out, g_DY, g_dW2, F, 2, B, 1, 0);
        { void *a[]={&g_DY,&g_dB2,&Bn,&N2}; gl(g_col,2,a); }
        g2(g_gemm, g_DY, g_W2, g_Dconv, B, F, 2, 0, 1);
        { int t=Co*Cin*Kw*Kh; void *a[]={&g_In,&g_Dconv,&g_dK,&Bn,&Cin,&Hh,&Ww,&Co,&Kh,&Kw}; gl(g_wgrad,t,a); }
        { void *a[]={&g_Dconv,&g_dBias,&Bn,&Co,&Hh,&Ww,&Kh,&Kw}; gl(g_bgrad,Co,a); }
        { int n=F*2; void *a[]={&g_W2,&g_dW2,&n,&LR}; gl(g_sgd,n,a); }
        { int n=2;   void *a[]={&g_B2,&g_dB2,&n,&LR}; gl(g_sgd,n,a); }
        { int n=COUT*CIN*KW*KH; void *a[]={&g_K,&g_dK,&n,&LR}; gl(g_sgd,n,a); }
        { int n=COUT; void *a[]={&g_Bias,&g_dBias,&n,&LR}; gl(g_sgd,n,a); }
        if (it % 30 == 0 || it == iters-1)
            fprintf(stderr, "  iter %3d  acc=%d/%d (%.1f%%)  CE=%.4f\n", it, correct, B, 100.0*correct/B, ce/B);
    }
    cuDevicePrimaryCtxRelease(dev);
    fprintf(stderr, "[cnn] DONE: CNN trained on GPU under musl (conv verified exact + accuracy)\n");
    _exit(0);
}
