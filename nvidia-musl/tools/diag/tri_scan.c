// tri_scan.c: headless NVIDIA triangle renderer.
// Enumerates ALL EGL devices (card + render-node), tries each as a
// EGL_PLATFORM_DEVICE display, and on the first that yields configs,
// renders a triangle to an FBO and writes a P3 PPM. No X/display needed —
// render nodes give DRM access without a display or DRM master.
//
// EGL via dlopen/dlsym + eglGetProcAddress (no libegl-dev). GL via <GL/gl.h>.
// Build: gcc -O2 -o tri_scan tri_scan.c -lGL
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>

typedef void *EGLDisplay; typedef void *EGLContext; typedef void *EGLSurface;
typedef void *EGLDeviceEXT; typedef unsigned int EGLBoolean;
typedef int EGLint; typedef int EGLenum; typedef unsigned int EGLuint;
typedef void *EGLConfig;
typedef void *(*__eglGetProcAddress)(const char *);

// EGL constants (no egl.h on host)
#define EGL_RENDERABLE_TYPE 0x3002
#define EGL_SURFACE_TYPE 0x200E
#define EGL_RED_SIZE 0x3024
#define EGL_GREEN_SIZE 0x3025
#define EGL_BLUE_SIZE 0x3026
#define EGL_ALPHA_SIZE 0x3021
#define EGL_NONE 0x3038
#define EGL_CONTEXT_MAJOR_VERSION 0x3098
#define EGL_CONTEXT_MINOR_VERSION 0x30FB
#define EGL_CONTEXT_OPENGL_PROFILE_MASK 0x9146
#define EGL_NO_CONTEXT ((EGLContext)0)
#define EGL_NO_SURFACE ((EGLSurface)0)

static __eglGetProcAddress _eglGetProcAddress;
static int (*_eglInitialize)(EGLDisplay, EGLint *, EGLint *);
static EGLDisplay (*_eglGetPlatformDisplayEXT)(EGLenum, void *, const EGLint *);
static EGLint (*_eglQueryDevicesEXT)(EGLint, EGLDeviceEXT *, EGLint *);
static EGLBoolean (*_eglBindAPI)(EGLenum);
static EGLContext (*_eglCreateContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint *);
static EGLBoolean (*_eglMakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
static EGLint (*_eglGetError)(void);
static EGLConfig (*_eglChooseConfig)(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *);

#define W 256
#define H 256

static void gl_check(const char *what) {
    GLenum e = glGetError();
    if (e) fprintf(stderr, "[gl] %s -> 0x%04x\n", what, e);
}

int main(int argc, char **argv) {
    const char *out = (argc > 1) ? argv[1] : "tri_scan.ppm";
    void *egl = dlopen("libEGL.so.1", RTLD_LAZY | RTLD_GLOBAL);
    if (!egl) { fprintf(stderr, "dlopen libEGL: %s\n", dlerror()); return 2; }
    _eglGetProcAddress = (__typeof(_eglGetProcAddress))dlsym(egl, "eglGetProcAddress");
    _eglInitialize = (__typeof(_eglInitialize))dlsym(egl, "eglInitialize");
    _eglChooseConfig = (__typeof(_eglChooseConfig))dlsym(egl, "eglChooseConfig");
    _eglGetError = (__typeof(_eglGetError))dlsym(egl, "eglGetError");
    if (!_eglGetProcAddress || !_eglInitialize || !_eglChooseConfig || !_eglGetError) {
        fprintf(stderr, "missing base EGL entry point: %s\n", dlerror()); return 2;
    }
    _eglBindAPI = (__typeof(_eglBindAPI))_eglGetProcAddress("eglBindAPI");
    _eglCreateContext = (__typeof(_eglCreateContext))_eglGetProcAddress("eglCreateContext");
    _eglMakeCurrent = (__typeof(_eglMakeCurrent))_eglGetProcAddress("eglMakeCurrent");
    _eglQueryDevicesEXT = (__typeof(_eglQueryDevicesEXT))_eglGetProcAddress("eglQueryDevicesEXT");
    _eglGetPlatformDisplayEXT = (__typeof(_eglGetPlatformDisplayEXT))_eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (!_eglQueryDevicesEXT || !_eglGetPlatformDisplayEXT) {
        fprintf(stderr, "no device platform entry points\n"); return 2;
    }
    EGLDeviceEXT devs[64]; EGLint ndev = 0;
    if (!_eglQueryDevicesEXT(64, devs, &ndev)) { fprintf(stderr, "eglQueryDevicesEXT %d\n", _eglGetError()); return 2; }
    fprintf(stderr, "[scan] %d devices, trying each as platform display\n", ndev);

    EGLint attr[] = {EGL_RENDERABLE_TYPE, 0x3002, EGL_SURFACE_TYPE, 0x0000,
                     EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig cfgs[8];

    for (EGLint i = 0; i < ndev; i++) {
        EGLDisplay dpy = _eglGetPlatformDisplayEXT(0x313F /*EGL_PLATFORM_DEVICE_EXT*/, devs[i], NULL);
        if (!dpy) { fprintf(stderr, "  dev[%d]: no display\n", i); continue; }
        EGLint maj = 0, min = 0;
        if (!_eglInitialize(dpy, &maj, &min)) {
            fprintf(stderr, "  dev[%d]: eglInitialize failed %d\n", i, _eglGetError());
            continue;
        }
        _eglBindAPI(0x3002 /*EGL_OPENGL_API*/);
        EGLint ncfg = 0;
        _eglChooseConfig(dpy, attr, cfgs, 8, &ncfg);
        fprintf(stderr, "  dev[%d]: EGL %d.%d, %d configs\n", i, maj, min, ncfg);
        if (ncfg == 0) continue;

        EGLint cattr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
                          EGL_CONTEXT_OPENGL_PROFILE_MASK, 0x0002 /*core*/, EGL_NONE};
        EGLContext ctx = _eglCreateContext(dpy, cfgs[0], EGL_NO_CONTEXT, cattr);
        if (!ctx) { fprintf(stderr, "  dev[%d]: no ctx %d\n", i, _eglGetError()); continue; }
        if (!_eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
            fprintf(stderr, "  dev[%d]: makecurrent %d\n", i, _eglGetError()); continue;
        }
        fprintf(stderr, "  dev[%d]: context current, renderer: %s\n", i, (const char *)glGetString(GL_RENDERER));
        fprintf(stderr, "  dev[%d]: version: %s\n", i, (const char *)glGetString(GL_VERSION));

        // FBO + render a triangle (red on black).
        unsigned int fbo, rbo;
        glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glGenRenderbuffers(1, &rbo); glBindRenderbuffer(GL_RENDERBUFFER, rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, W, H);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            fprintf(stderr, "  FBO incomplete\n"); return 3;
        }
        glViewport(0, 0, W, H); glClear(GL_COLOR_BUFFER_BIT); glClearColor(0.f, 0.f, 0.f, 1.f); glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_TRIANGLES);
        glColor3f(1, 0, 0); glVertex2f(-0.5f, -0.5f);
        glColor3f(0, 1, 0); glVertex2f(0.5f, -0.5f);
        glColor3f(0, 0, 1); glVertex2f(0.0f, 0.5f);
        glEnd();
        gl_check("render");
        unsigned char px[W * H * 4];
        glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
        gl_check("readpixels");

        // Count non-black pixels.
        long nonblack = 0;
        for (int p = 0; p < W * H; p++) if (px[p * 4] | px[p * 4 + 1] | px[p * 4 + 2]) nonblack++;
        fprintf(stderr, "  dev[%d]: rendered, %ld/%d non-black px\n", i, nonblack, W * H);

        // Write P3 PPM.
        FILE *f = fopen(out, "wb");
        if (!f) { perror("fopen"); return 3; }
        fprintf(f, "P3\n%d %d\n255\n", W, H);
        for (int y = H - 1; y >= 0; y--)
            for (int x = 0; x < W; x++) {
                int o = (y * W + x) * 4;
                fprintf(f, "%d %d %d\n", px[o], px[o + 1], px[o + 2]);
            }
        fclose(f);
        fprintf(stderr, "WROTE %s (device %d) — triangle rendered\n", out, i);
        return 0;
    }
    fprintf(stderr, "[scan] no device produced configs\n");
    return 1;
}
