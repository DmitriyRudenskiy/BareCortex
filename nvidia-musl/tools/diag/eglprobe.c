/* Headless EGL probe v2: get extension entry points via eglGetProcAddress,
 * enumerate devices, create a device display, eglInitialize. No X. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>

typedef void *EGLDisplay;
typedef void *EGLDeviceEXT;
typedef void *(*EGLPFN)(const char *);
typedef int EGLint;
typedef unsigned char EGLBoolean;
typedef const char *(*EGLSTRING)(EGLDisplay, EGLint);
typedef EGLBoolean (*EGLINIT)(EGLDisplay, EGLint *, EGLint *);
typedef EGLint (*EGLERR)(void);
typedef EGLint (*EGLQDEV)(EGLint, EGLDeviceEXT *, EGLint *);
typedef EGLDisplay (*EGLGPD)(EGLint, void *, const EGLint *);

#define EGL_PLATFORM_DEVICE_EXT 0x313F
#define EGL_VENDOR 0x3053
#define EGL_VERSION 0x3054
#define EGL_EXTENSIONS 0x3055

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);
  void *p = dlopen("/egl/libEGL.so.1.1.0", RTLD_NOW | RTLD_GLOBAL);
  if (!p) { printf("LOAD_ERR: %s\n", dlerror()); fflush(stdout); _exit(1); }
  printf("LOADED\n");
  EGLPFN egpa = (EGLPFN)dlsym(p, "eglGetProcAddress");
  EGLSTRING qstr = (EGLSTRING)dlsym(p, "eglQueryString");
  EGLINIT init = (EGLINIT)dlsym(p, "eglInitialize");
  EGLERR geterr = (EGLERR)dlsym(p, "eglGetError");
  if (!egpa || !init || !geterr) { printf("SYMS_MISSING core\n"); fflush(stdout); _exit(1); }
  printf("CORE_OK\n");

  EGLQDEV qdev = (EGLQDEV)egpa("eglQueryDevicesEXT");
  EGLGPD gpd = (EGLGPD)egpa("eglGetPlatformDisplayEXT");
  printf("eglQueryDevicesEXT=%p eglGetPlatformDisplayEXT=%p\n", (void*)qdev, (void*)gpd);
  if (!qdev || !gpd) { printf("NO_DEVICE_EXT err=0x%x\n", geterr()); fflush(stdout); _exit(1); }

  EGLDeviceEXT devs[8];
  EGLint num = 0;
  EGLint rc = qdev(8, devs, &num);
  printf("queryDevices rc=0x%x num=%d\n", rc, num);
  if (num < 1) { printf("NO_DEVICES err=0x%x\n", geterr()); fflush(stdout); _exit(1); }

  EGLDisplay dpy = gpd(EGL_PLATFORM_DEVICE_EXT, devs[0], NULL);
  if (!dpy) { printf("NO_DISPLAY err=0x%x\n", geterr()); fflush(stdout); _exit(1); }
  printf("DEVICE_DISPLAY_OK\n");

  EGLint maj = 0, min = 0;
  EGLBoolean ok = init(dpy, &maj, &min);
  printf("init ok=%d %d.%d err=0x%x\n", ok, maj, min, geterr());
  if (ok && qstr) {
    printf("vendor: %s\n", qstr(dpy, EGL_VENDOR));
    printf("version: %s\n", qstr(dpy, EGL_VERSION));
    const char *ext = qstr(dpy, EGL_EXTENSIONS);
    if (ext) printf("ext: %.160s\n", ext);
  }
  fflush(stdout);
  _exit(ok ? 0 : 1);
}
