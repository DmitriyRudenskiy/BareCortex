/* tfm_kernels.cu — kernels for a MULTI-LAYER TRANSFORMER (forward): gemm2, epi
 * (linear+bias, optional relu), softmax_k (reused) + add_k (residual) + ln_k
 * (LayerNorm). extern "C", no mangling. No #include (nvcc builtins).
 */
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
/* out = (relu? ReLU : ident)(X + bias[j]) */
extern "C" __global__ void epi(const float *X, const float *bias, float *out,
                               int M, int N, int relu) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= M * N) return;
    float v = X[idx] + bias[idx % N];
    if (relu && v < 0.f) v = 0.f;
    out[idx] = v;
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
/* out = a + b (residual) */
extern "C" __global__ void add_k(const float *a, const float *b, float *out, int n) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= n) return;
    out[idx] = a[idx] + b[idx];
}
/* LayerNorm per row of d: y = (x-mean)/sqrt(var+eps) * gamma + beta */
extern "C" __global__ void ln_k(const float *x, const float *gamma, const float *beta,
                                float *y, int M, int d, float eps) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= M) return;
    float m = 0.f;
    for (int j = 0; j < d; j++) m += x[i*d+j];
    m /= d;
    float v = 0.f;
    for (int j = 0; j < d; j++) { float e = x[i*d+j]-m; v += e*e; }
    v /= d;
    float s = 1.0f / sqrtf(v + eps);
    for (int j = 0; j < d; j++) y[i*d+j] = (x[i*d+j]-m) * s * gamma[j] + beta[j];
}
