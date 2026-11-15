/* mlp_kernels.cu — kernels for a tiny MLP training loop (forward + backward + SGD).
 * Compiled by nvcc (host glibc) into a cubin, embedded into a musl driver.
 * All plain extern "C" (no mangling). Non-tiled (matrices are tiny); correctness
 * + convergence are the point, not peak perf. No #include: nvcc provides the
 * CUDA builtins (__global__, threadIdx, ...) like matmul.cu.
 *
 * Layouts: row-major. gemm2 supports transpose via tA/tB:
 *   op(A) is MxK, stored  tA? KxM : MxK ;  op(B) is KxN, stored  tB? NxK : KxN.
 */

/* C[M*N] = op(A) @ op(B) */
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

/* out = (relu? ReLU : ident)( X + bias[j] ) ; X,out M*N, bias N */
extern "C" __global__ void epi(const float *X, const float *bias, float *out,
                               int M, int N, int relu) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= M * N) return;
    float v = X[idx] + bias[idx % N];
    if (relu && v < 0.f) v = 0.f;
    out[idx] = v;
}

/* dY = 2*(Y - T) * invB  (MSE grad wrt output) ; n elems */
extern "C" __global__ void dY_k(const float *Y, const float *T, float *dY,
                                int n, float invB) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= n) return;
    dY[idx] = 2.f * (Y[idx] - T[idx]) * invB;
}

/* dH *= (H > 0)  (ReLU backward mask) ; n elems */
extern "C" __global__ void relu_mask(float *dH, const float *H, int n) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= n) return;
    if (H[idx] <= 0.f) dH[idx] = 0.f;
}

/* out[j] = sum_i X[i*N + j] ; X M*N, out N */
extern "C" __global__ void colsum(const float *X, float *out, int M, int N) {
    int j = blockIdx.x * blockDim.x + threadIdx.x;
    if (j >= N) return;
    float s = 0.f;
    for (int i = 0; i < M; i++) s += X[i * N + j];
    out[j] = s;
}

/* W -= lr * G  (SGD) ; n elems */
extern "C" __global__ void sgd(float *W, const float *G, int n, float lr) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= n) return;
    W[idx] -= lr * G[idx];
}
