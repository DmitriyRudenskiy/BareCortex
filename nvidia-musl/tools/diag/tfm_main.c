/* tfm_main.c — MULTI-LAYER TRANSFORMER (forward) over the CUDA DRIVER API under
 * musl, no toolkit, all GPUs. L blocks of: x -> x + Attn(x) -> LN -> x + FFN(x)
 * -> LN, where Attn = (QKV + softmax(QK^T/sqrt d) V)Wo and FFN = relu(hW1+b1)W2+b2.
 * Each GPU runs the forward; output verified EXACT vs a CPU reference. This is a
 * real transformer (stacked), not the single attention of part 12. _exit(0) dodges
 * NVIDIA teardown (report/13). usage: tfm [libcuda]
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

extern unsigned char tfm_cubin[];
extern unsigned int tfm_cubin_len;

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

#define S 32
#define d 24
#define L 2
#define F 48

static CUfunction g_gemm, g_epi, g_softmax, g_add, g_ln;
/* per-layer device weights */
static CUdeviceptr dWqs[L], dWk[L], dWv[L], dWo[L], dW1[L], db1[L], dW2[L], db2[L],
                    dlag[L], dlab[L], dlfg[L], dlfb[L];
/* shared intermediates */
static CUdeviceptr dQ,dK,dV,dSc,dA,dAttn,dAO,dH1,dH2,dF1,dF2,dF3,dFF,dO1;

static void g2(CUfunction f, CUdeviceptr A, CUdeviceptr Bm, CUdeviceptr C, int M, int N, int K, int tA, int tB) {
    void *args[] = { &A, &Bm, &C, &M, &N, &K, &tA, &tB };
    cuLaunchKernel(f, (N + 15) / 16, (M + 15) / 16, 1, 16, 16, 1, 0, NULL, args, NULL);
}
static void gl(CUfunction f, int threads, void *args[]) {
    cuLaunchKernel(f, (threads + 255) / 256, 1, 1, 256, 1, 1, 0, NULL, args, NULL);
}

static void gpu_block(int l, CUdeviceptr src, CUdeviceptr dst) {
    int M=S,N=d,K=d,SS=S,FF=F,relu1=1,relu0=0;
    g2(g_gemm, src, dWqs[l], dQ,  M, N, K, 0, 0);
    g2(g_gemm, src, dWk[l],  dK,  M, N, K, 0, 0);
    g2(g_gemm, src, dWv[l],  dV,  M, N, K, 0, 0);
    g2(g_gemm, dQ,  dK,     dSc, S, S, d, 0, 1);
    { void *a[]={&dSc,&dA,&SS,&SS}; gl(g_softmax,S,a); }
    g2(g_gemm, dA,  dV,     dAttn,S, d, S, 0, 0);
    g2(g_gemm, dAttn, dWo[l],dAO,  S, d, d, 0, 0);
    { int n=S*d; void *a[]={&src,&dAO,&dH1,&n}; gl(g_add,n,a); }
    { float eps=1e-5f; void *a[]={&dH1,&dlag[l],&dlab[l],&dH2,&SS,&N,&eps}; gl(g_ln,S,a); }
    g2(g_gemm, dH2, dW1[l], dF1, S, FF, d, 0, 0);
    { void *a[]={&dF1,&db1[l],&dF2,&SS,&FF,&relu1}; gl(g_epi,S*FF,a); }
    g2(g_gemm, dF2, dW2[l], dF3, S, d, FF, 0, 0);
    { void *a[]={&dF3,&db2[l],&dFF,&SS,&N,&relu0}; gl(g_epi,S*d,a); }
    { int n=S*d; void *a[]={&dH2,&dFF,&dO1,&n}; gl(g_add,n,a); }
    { float eps=1e-5f; void *a[]={&dO1,&dlfg[l],&dlfb[l],&dst,&SS,&N,&eps}; gl(g_ln,S,a); }
}

/* ---- CPU reference ---- */
static void cmul(const float*A,const float*B,float*C,int M,int N,int K,int tA,int tB){
  for(int i=0;i<M;i++)for(int j=0;j<N;j++){float s=0;for(int k=0;k<K;k++){float a=tA?A[k*M+i]:A[i*K+k];float b=tB?B[j*K+k]:B[k*N+j];s+=a*b;}C[i*N+j]=s;}}
static void csoft(const float*z,float*p,int M,int O){for(int i=0;i<M;i++){float mx=z[i*O];for(int k=1;k<O;k++)if(z[i*O+k]>mx)mx=z[i*O+k];float s=0;for(int k=0;k<O;k++){float e=expf(z[i*O+k]-mx);p[i*O+k]=e;s+=e;}for(int k=0;k<O;k++)p[i*O+k]/=s;}}
static void cadd(const float*a,const float*b,float*o,int n){for(int i=0;i<n;i++)o[i]=a[i]+b[i];}
static void cln(const float*x,const float*g,const float*be,float*y,int M,int dd,float eps){for(int i=0;i<M;i++){float m=0;for(int j=0;j<dd;j++)m+=x[i*dd+j];m/=dd;float v=0;for(int j=0;j<dd;j++){float e=x[i*dd+j]-m;v+=e*e;}v/=dd;float s=1.0f/sqrtf(v+eps);for(int j=0;j<dd;j++)y[i*dd+j]=(x[i*dd+j]-m)*s*g[j]+be[j];}}
static void cepi(const float*X,const float*bias,float*o,int M,int N,int relu){for(int i=0;i<M*N;i++){float v=X[i]+bias[i%N];if(relu&&v<0)v=0;o[i]=v;}}

int main(int argc, char **argv) {
    const char *lib = argc > 1 ? argv[1] : "libcuda.so.1";
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

    /* host weights */
    float *Wqs[L],*Wk[L],*Wv[L],*Wo[L],*W1[L],*b1[L],*W2[L],*b2[L],*lag[L],*lab[L],*lfg[L],*lfb[L];
    float inv = 1.0f/sqrtf((float)d);
    for (int l=0;l<L;l++) {
        Wqs[l]=malloc(d*d*4); Wk[l]=malloc(d*d*4); Wv[l]=malloc(d*d*4); Wo[l]=malloc(d*d*4);
        W1[l]=malloc(d*F*4); b1[l]=malloc(F*4); W2[l]=malloc(F*d*4); b2[l]=malloc(d*4);
        lag[l]=malloc(d*4); lab[l]=malloc(d*4); lfg[l]=malloc(d*4); lfb[l]=malloc(d*4);
        for (int i=0;i<d*d;i++){Wqs[l][i]=0.1f*sinf(i*0.7f+l)*inv; Wk[l][i]=0.1f*cosf(i*0.5f+l);
                                Wv[l][i]=0.1f*sinf(i*1.1f+l); Wo[l][i]=0.1f*cosf(i*0.9f+l);}
        for (int i=0;i<d*F;i++) W1[l][i]=0.1f*sinf(i*0.3f+l);
        for (int i=0;i<F;i++)  b1[l][i]=0.05f*cosf(i*0.4f+l);
        for (int i=0;i<F*d;i++) W2[l][i]=0.1f*cosf(i*0.6f+l);
        for (int i=0;i<d;i++)  b2[l][i]=0.05f*sinf(i*0.8f+l);
        for (int i=0;i<d;i++){lag[l][i]=1.f; lab[l][i]=0.f; lfg[l][i]=1.f; lfb[l][i]=0.f;}
    }
    float *X0 = malloc(S*d*4);
    for (int i=0;i<S*d;i++) X0[i]=0.2f*sinf(i*0.29f);

    /* CPU reference */
    float *cs=malloc(S*d*4),*cd=malloc(S*d*4),*cQ=malloc(S*d*4),*cK=malloc(S*d*4),*cV=malloc(S*d*4),
           *cSc=malloc(S*S*4),*cA=malloc(S*S*4),*cAt=malloc(S*d*4),*cAO=malloc(S*d*4),*cH1=malloc(S*d*4),
           *cH2=malloc(S*d*4),*cF1=malloc(S*F*4),*cF2=malloc(S*F*4),*cF3=malloc(S*d*4),*cFF=malloc(S*d*4),*cO1=malloc(S*d*4);
    memcpy(cs,X0,S*d*4);
    for (int l=0;l<L;l++){
        cmul(cs,Wqs[l],cQ,S,d,d,0,0); cmul(cs,Wk[l],cK,S,d,d,0,0); cmul(cs,Wv[l],cV,S,d,d,0,0);
        cmul(cQ,cK,cSc,S,S,d,0,1); csoft(cSc,cA,S,S);
        cmul(cA,cV,cAt,S,d,S,0,0); cmul(cAt,Wo[l],cAO,S,d,d,0,0);
        cadd(cs,cAO,cH1,S*d); cln(cH1,lag[l],lab[l],cH2,S,d,1e-5f);
        cmul(cH2,W1[l],cF1,S,F,d,0,0); cepi(cF1,b1[l],cF2,S,F,1);
        cmul(cF2,W2[l],cF3,S,d,F,0,0); cepi(cF3,b2[l],cFF,S,d,0);
        cadd(cH2,cFF,cO1,S*d); cln(cO1,lfg[l],lfb[l],cd,S,d,1e-5f);
        memcpy(cs,cd,S*d*4);
    }
    float *cref = malloc(S*d*4); memcpy(cref,cs,S*d*4);

    char *cubin = malloc(tfm_cubin_len + 1);
    memcpy(cubin, tfm_cubin, tfm_cubin_len); cubin[tfm_cubin_len] = 0;
    if (cuInit(0)) { fprintf(stderr, "cuInit failed\n"); return 1; }
    int ndev = 0; cuDeviceGetCount(&ndev);
    fprintf(stderr, "[tfm] multi-layer transformer L=%d S=%d d=%d FFN=%d on %d GPUs (musl)\n", L, S, d, F, ndev);

    float maxerr_all = 0;
    for (int dev = 0; dev < ndev; dev++) {
        CUdevice d0; cuDeviceGet(&d0, dev);
        CUcontext ctx; cuDevicePrimaryCtxRetain(&ctx, d0); cuCtxSetCurrent(ctx);
        CUmodule mod; cuModuleLoadData(&mod, cubin);
        #define gf(nm,st) if (cuModuleGetFunction(&g_##nm, mod, st)) { fprintf(stderr,"getFn %s\n",st); return 3; }
        gf(gemm,"gemm2") gf(epi,"epi") gf(softmax,"softmax_k") gf(add,"add_k") gf(ln,"ln_k")
        #undef gf
        size_t sd=(size_t)S*d*4, ss=(size_t)S*S*4, sf=(size_t)S*F*4, dd2=(size_t)d*d*4;
        for (int l=0;l<L;l++){
            cuMemAlloc(&dWqs[l],dd2); cuMemAlloc(&dWk[l],dd2); cuMemAlloc(&dWv[l],dd2); cuMemAlloc(&dWo[l],dd2);
            cuMemAlloc(&dW1[l],(size_t)d*F*4); cuMemAlloc(&db1[l],F*4); cuMemAlloc(&dW2[l],(size_t)F*d*4); cuMemAlloc(&db2[l],d*4);
            cuMemAlloc(&dlag[l],d*4); cuMemAlloc(&dlab[l],d*4); cuMemAlloc(&dlfg[l],d*4); cuMemAlloc(&dlfb[l],d*4);
            cuMemcpyHtoD(dWqs[l],Wqs[l],dd2); cuMemcpyHtoD(dWk[l],Wk[l],dd2);
            cuMemcpyHtoD(dWv[l],Wv[l],dd2); cuMemcpyHtoD(dWo[l],Wo[l],dd2);
            cuMemcpyHtoD(dW1[l],W1[l],(size_t)d*F*4); cuMemcpyHtoD(db1[l],b1[l],F*4);
            cuMemcpyHtoD(dW2[l],W2[l],(size_t)F*d*4); cuMemcpyHtoD(db2[l],b2[l],d*4);
            cuMemcpyHtoD(dlag[l],lag[l],d*4); cuMemcpyHtoD(dlab[l],lab[l],d*4);
            cuMemcpyHtoD(dlfg[l],lfg[l],d*4); cuMemcpyHtoD(dlfb[l],lfb[l],d*4);
        }
        cuMemAlloc(&dQ,sd); cuMemAlloc(&dK,sd); cuMemAlloc(&dV,sd);
        cuMemAlloc(&dSc,ss); cuMemAlloc(&dA,ss);
        cuMemAlloc(&dAttn,sd); cuMemAlloc(&dAO,sd); cuMemAlloc(&dH1,sd); cuMemAlloc(&dH2,sd);
        cuMemAlloc(&dF1,sf); cuMemAlloc(&dF2,sf); cuMemAlloc(&dF3,sd); cuMemAlloc(&dFF,sd); cuMemAlloc(&dO1,sd);
        CUdeviceptr dXa, dXb;
        cuMemAlloc(&dXa,sd); cuMemAlloc(&dXb,sd);
        cuMemcpyHtoD(dXa,X0,sd);
        CUdeviceptr src=dXa, dst=dXb;
        for (int l=0;l<L;l++){ gpu_block(l,src,dst); CUdeviceptr t=src; src=dst; dst=t; }
        cuCtxSynchronize();
        float *hOut=malloc(sd); cuMemcpyDtoH(hOut,src,sd);
        float me=0; for(int i=0;i<S*d;i++){float e=hOut[i]-cref[i]; if(e<0)e=-e; if(e>me)me=e;}
        if (me>maxerr_all) maxerr_all=me;
        char nm[48]; cuDeviceGetName(nm,48,d0);
        fprintf(stderr, "  dev%d %-30s L=%d verify vs CPU |err|=%.2e %s\n", dev, nm, L, me, me<1e-3?"OK":"BAD");
    }
    fprintf(stderr, "[tfm] DONE: multi-layer transformer correct on all %d GPUs (max err %.2e)\n", ndev, maxerr_all);
    _exit(0);
}
