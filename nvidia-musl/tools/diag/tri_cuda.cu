// tri_cuda.cu: GPU software-rasterized triangle, HEADLESS (no display needed).
// This is the realistic path for a headless NVIDIA compute node (e.g. 5090D):
// OpenGL/EGL needs an X/Wayland display, but CUDA renders to a buffer on-device.
// Each CUDA thread = one pixel; barycentric triangle test + per-vertex color
// blend. Output: P3 PPM. Build: nvcc -O2 -o tri_cuda tri_cuda.cu
#include <cstdio>
#include <cstdlib>
#include <cuda_runtime.h>

#define W 256
#define H 256
#define CHK(x) do { cudaError_t e = (x); if (e) { \
    fprintf(stderr, "CUDA error %s:%d: %s\n", __FILE__, __LINE__, cudaGetErrorString(e)); \
    exit(1); } } while (0)

__global__ void tri_kernel(unsigned char *fb) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= W || y >= H) return;
    // triangle vertices (pixel coords, y-down) + per-vertex RGB
    const float ax=64,  ay=192, ar=1, ag=0, ab=0;  // red
    const float bx=192, by=192, br=0, bg=1, bb=0;  // green
    const float cx=128, cy=48,  cr=0, cg=0, cb=1;  // blue
    const float px=(float)x, py=(float)y;
    // barycentric via dot products: v0=A->B, v1=A->C, v2=A->P
    const float v0x=bx-ax, v0y=by-ay;
    const float v1x=cx-ax, v1y=cy-ay;
    const float v2x=px-ax, v2y=py-ay;
    const float d00=v0x*v0x+v0y*v0y;
    const float d01=v0x*v1x+v0y*v1y;
    const float d11=v1x*v1x+v1y*v1y;
    const float d20=v2x*v0x+v2y*v0y;
    const float d21=v2x*v1x+v2y*v1y;
    const float denom=d00*d11-d01*d01;
    float v, w;
    if (denom != 0.0f) { v=(d11*d20-d01*d21)/denom; w=(d00*d21-d01*d20)/denom; }
    else { v = w = 1.0f; }
    const float u = 1.0f - v - w;   // u=A, v=B, w=C
    unsigned char *p = fb + (size_t)(y*W + x)*4;
    if (u >= -1e-4f && v >= -1e-4f && w >= -1e-4f) {
        p[0] = (unsigned char)(255.0f * (u*ar + v*br + w*cr));
        p[1] = (unsigned char)(255.0f * (u*ag + v*bg + w*cg));
        p[2] = (unsigned char)(255.0f * (u*ab + v*bb + w*cb));
        p[3] = 255;
    } else {
        p[0] = p[1] = p[2] = 0; p[3] = 255;
    }
}

int main(int argc, char **argv) {
    const char *out = (argc > 1) ? argv[1] : "tri_cuda.ppm";
    int dev = 0;
    CHK(cudaSetDevice(dev));
    cudaDeviceProp prop;
    CHK(cudaGetDeviceProperties(&prop, dev));
    fprintf(stderr, "[cuda] device %d: %s (sm_%d%d)\n", dev, prop.name, prop.major, prop.minor);

    unsigned char *d_fb;
    CHK(cudaMalloc(&d_fb, (size_t)W*H*4));
    dim3 block(16, 16), grid((W+15)/16, (H+15)/16);
    tri_kernel<<<grid, block>>>(d_fb);
    CHK(cudaGetLastError());
    CHK(cudaDeviceSynchronize());

    unsigned char *h_fb = (unsigned char*)malloc((size_t)W*H*4);
    CHK(cudaMemcpy(h_fb, d_fb, (size_t)W*H*4, cudaMemcpyDeviceToHost));
    long nb = 0;
    for (int i = 0; i < W*H; i++) if (h_fb[i*4] | h_fb[i*4+1] | h_fb[i*4+2]) nb++;
    fprintf(stderr, "[cuda] rendered %ld/%d non-black px\n", nb, W*H);

    FILE *f = fopen(out, "wb");
    if (!f) { perror("fopen"); return 1; }
    fprintf(f, "P3\n%d %d\n255\n", W, H);
    for (int y = H-1; y >= 0; y--)
        for (int x = 0; x < W; x++) {
            int o = (y*W + x)*4;
            fprintf(f, "%d %d %d\n", h_fb[o], h_fb[o+1], h_fb[o+2]);
        }
    fclose(f);
    fprintf(stderr, "WROTE %s — GPU-rendered triangle\n", out);
    CHK(cudaFree(d_fb));
    free(h_fb);
    return 0;
}
