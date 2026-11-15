/* cls_kernels.cu — MLP kernels (from mlp_kernels) + softmax + softmax-cross-entropy
 * grad, for a MULTI-CLASS CLASSIFIER trained on GPU. extern "C", no mangling.
 * Adds: softmax_k (per-sample over O classes), dY_softmax (dY=(p-onehot)/B).
 * No #include: nvcc provides CUDA builtins (__expf intrinsic) like matmul.cu.
 */

/* C[M*N] = op(A) @ op(B) ; tA/tB transpose */
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
/* out = (relu? ReLU : ident)( X + bias[j] ) */
extern "C" __global__ void epi(const float *X, const float *bias, float *out,
                               int M, int N, int relu) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= M * N) return;
    float v = X[idx] + bias[idx % N];
    if (relu && v < 0.f) v = 0.f;
    out[idx] = v;
}
/* dH *= (H > 0) */
extern "C" __global__ void relu_mask(float *dH, const float *H, int n) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= n) return;
    if (H[idx] <= 0.f) dH[idx] = 0.f;
}
/* out[j] = sum_i X[i*N + j] */
extern "C" __global__ void colsum(const float *X, float *out, int M, int N) {
    int j = blockIdx.x * blockDim.x + threadIdx.x;
    if (j >= N) return;
    float s = 0.f;
    for (int i = 0; i < M; i++) s += X[i * N + j];
    out[j] = s;
}
/* W -= lr * G */
extern "C" __global__ void sgd(float *W, const float *G, int n, float lr) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= n) return;
    W[idx] -= lr * G[idx];
}
/* softmax over O classes per sample; z,p M*O */
extern "C" __global__ void softmax_k(const float *z, float *p, int M, int O) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= M) return;
    float mx = z[i * O];
    for (int k = 1; k < O; k++) if (z[i*O+k] > mx) mx = z[i*O+k];
    float s = 0.f;
    for (int k = 0; k < O; k++) { float e = __expf(z[i*O+k] - mx); p[i*O+k] = e; s += e; }
    for (int k = 0; k < O; k++) p[i*O+k] /= s;
}
/* dY = (p - onehot(tgt)) * invB ; p M*O, tgt M (class idx), dY M*O */
extern "C" __global__ void dY_softmax(const float *p, const int *tgt, float *dY,
                                      int M, int O, float invB) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= M * O) return;
    int i = idx / O, k = idx % O;
    float onehot = (k == tgt[i]) ? 1.f : 0.f;
    dY[idx] = (p[idx] - onehot) * invB;
}
