extern "C" __global__ void tri_kernel(unsigned char *fb) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    const int W = 256, H = 256;
    if (x >= W || y >= H) return;
    const float ax=64, ay=192, ar=1, ag=0, ab=0;
    const float bx=192, by=192, br=0, bg=1, bb=0;
    const float cx=128, cy=48,  cr=0, cg=0, cb=1;
    const float px=(float)x, py=(float)y;
    const float v0x=bx-ax, v0y=by-ay, v1x=cx-ax, v1y=cy-ay, v2x=px-ax, v2y=py-ay;
    const float d00=v0x*v0x+v0y*v0y, d01=v0x*v1x+v0y*v1y, d11=v1x*v1x+v1y*v1y;
    const float d20=v2x*v0x+v2y*v0y, d21=v2x*v1x+v2y*v1y;
    const float denom=d00*d11-d01*d01;
    float v, w;
    if (denom != 0.0f) { v=(d11*d20-d01*d21)/denom; w=(d00*d21-d01*d20)/denom; }
    else { v = w = 1.0f; }
    const float u = 1.0f - v - w;
    unsigned char *p = fb + (size_t)(y*W + x)*4;
    if (u >= -1e-4f && v >= -1e-4f && w >= -1e-4f) {
        p[0] = (unsigned char)(255.0f * (u*ar + v*br + w*cr));
        p[1] = (unsigned char)(255.0f * (u*ag + v*bg + w*cg));
        p[2] = (unsigned char)(255.0f * (u*ab + v*bb + w*cb));
        p[3] = 255;
    } else { p[0]=p[1]=p[2]=0; p[3]=255; }
}
