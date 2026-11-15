/* matmul.cu — naive GEMM kernel (each thread = one C[i][j]).
 * Proves the musl CUDA compute path produces CORRECT numeric results
 * (not just a render). Verified against a CPU reference. Tiling/pipelining
 * is a later perf step; correctness first.
 */
#include <cuda_runtime.h>

extern "C" __global__ void matmul(const float *A, const float *B, float *C, int N) {
    int col = blockIdx.x * blockDim.x + threadIdx.x;
    int row = blockIdx.y * blockDim.y + threadIdx.y;
    if (row < N && col < N) {
        float sum = 0.0f;
        for (int k = 0; k < N; k++)
            sum += A[row * N + k] * B[k * N + col];
        C[row * N + col] = sum;
    }
}
