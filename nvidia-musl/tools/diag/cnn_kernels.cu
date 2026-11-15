/* cnn_kernels.cu — 2D convolution (forward + weight/bias grad) + the MLP/softmax
 * kernels reused for the classification head, for a small CNN trained on GPU.
 * extern "C", no mangling. No #include (nvcc builtins, like matmul.cu).
 * Layouts: in/out per-image contiguous [Cin,H,W] / [Cout,Ho,Wo], batched as
 * [B, ...]. Head: flatten conv out [B, Cout*Ho*Wo] -> gemm2 -> softmax.
 */

/* C[M*N] = op(A)@op(B) */
extern "C" __global__ void gemm2(const float *A, const float *B, float *C,
                                 int M, int N, int K, int tA, int tB) {
    int i = blockIdx.y * blockDim.y + threadIdx.y;
    int j = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= M || j >= N) return;
    float s = 0.f;
    for (int k = 0; k < K; k++) {
        float a = tA ? A[k * M + i] : A[i * K + k];
        float b = tB ? B[j * K + k] : B[k * N + j];
        s += a * b;
    }
    C[i * N + j] = s;
}
/* out[j] = sum_i X[i*N+j] */
extern "C" __global__ void colsum(const float *X, float *out, int M, int N) {
    int j = blockIdx.x * blockDim.x + threadIdx.x;
    if (j >= N) return;
    float s = 0.f;
    for (int i = 0; i < M; i++) s += X[i * N + j];
    out[j] = s;
}
/* W -= lr*G */
extern "C" __global__ void sgd(float *W, const float *G, int n, float lr) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= n) return;
    W[idx] -= lr * G[idx];
}
/* softmax over O per sample */
extern "C" __global__ void softmax_k(const float *z, float *p, int M, int O) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= M) return;
    float mx = z[i*O];
    for (int k = 1; k < O; k++) if (z[i*O+k] > mx) mx = z[i*O+k];
    float s = 0.f;
    for (int k = 0; k < O; k++) { float e = __expf(z[i*O+k]-mx); p[i*O+k]=e; s+=e; }
    for (int k = 0; k < O; k++) p[i*O+k] /= s;
}
/* dY = (p - onehot(tgt))*invB */
extern "C" __global__ void dY_softmax(const float *p, const int *tgt, float *dY,
                                      int M, int O, float invB) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= M*O) return;
    int i = idx/O, k = idx%O;
    float oh = (k == tgt[i]) ? 1.f : 0.f;
    dY[idx] = (p[idx] - oh) * invB;
}

/* conv2d forward: in[B,Cin,H,W], k[Cout,Cin,kw,kh], bias[Cout] -> out[B,Cout,Ho,Wo] */
extern "C" __global__ void conv2d(const float *in, const float *k, const float *bias, float *out,
                                  int B, int Cin, int H, int W, int Cout, int kh, int kw) {
    int Ho = H - kh + 1, Wo = W - kw + 1;
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int n = B * Cout * Ho * Wo;
    if (idx >= n) return;
    int wo = idx % Wo;
    int ho = (idx / Wo) % Ho;
    int co = (idx / (Wo * Ho)) % Cout;
    int b  = idx / (Wo * Ho * Cout);
    float s = bias[co];
    for (int ci = 0; ci < Cin; ci++)
        for (int ky = 0; ky < kw; ky++)
            for (int kx = 0; kx < kh; kx++)
                s += in[((b*Cin + ci)*H + ho + ky)*W + (wo + kx)]
                   * k[((co*Cin + ci)*kw + ky)*kh + kx];
    out[(b*Cout + co)*Ho*Wo + ho*Wo + wo] = s;
}
/* conv weight grad: dk[co,ci,ky,kx] = sum_{b,ho,wo} in * dout */
extern "C" __global__ void conv_wgrad(const float *in, const float *dout, float *dk,
                                      int B, int Cin, int H, int W, int Cout, int kh, int kw) {
    int Ho = H - kh + 1, Wo = W - kw + 1;
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int n = Cout * Cin * kw * kh;
    if (idx >= n) return;
    int kx = idx % kh;
    int ky = (idx / kh) % kw;
    int ci = (idx / (kh*kw)) % Cin;
    int co = idx / (kh*kw*Cin);
    float s = 0.f;
    for (int b = 0; b < B; b++)
        for (int ho = 0; ho < Ho; ho++)
            for (int wo = 0; wo < Wo; wo++)
                s += in[((b*Cin+ci)*H + ho+ky)*W + (wo+kx)]
                   * dout[(b*Cout+co)*Ho*Wo + ho*Wo + wo];
    dk[idx] = s;
}
/* conv bias grad: db[co] = sum_{b,ho,wo} dout */
extern "C" __global__ void conv_bgrad(const float *dout, float *db,
                                      int B, int Cout, int H, int W, int kh, int kw) {
    int Ho = H - kh + 1, Wo = W - kw + 1;
    int co = blockIdx.x * blockDim.x + threadIdx.x;
    if (co >= Cout) return;
    float s = 0.f;
    for (int b = 0; b < B; b++)
        for (int ho = 0; ho < Ho; ho++)
            for (int wo = 0; wo < Wo; wo++)
                s += dout[(b*Cout+co)*Ho*Wo + ho*Wo + wo];
    db[co] = s;
}
