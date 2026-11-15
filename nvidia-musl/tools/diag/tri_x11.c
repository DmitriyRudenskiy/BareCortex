/* Minimal OpenGL triangle on X11 + EGL (NVIDIA). glibc build.
 * EGL via dlopen/dlsym (no libegl-dev needed). X11 + GL via headers.
 * Renders a colored triangle for N frames then exits. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <X11/Xlib.h>
#include <GL/gl.h>

/* ---- EGL minimal types/constants (header-free) ---- */
typedef void*EGLDisplay;typedef void*EGLConfig;typedef void*EGLContext;typedef void*EGLSurface;
typedef unsigned int EGLBoolean;typedef int EGLint;typedef void*EGLNativeDisplayType;
typedef unsigned long EGLNativeWindowType;
#define EGL_DEFAULT_DISPLAY ((EGLNativeDisplayType)0)
#define EGL_NONE 0x3000
#define EGL_OPENGL_BIT 0x0001
#define EGL_OPENGL_API 0x30A2
#define EGL_WINDOW_BIT 0x0001
#define EGL_SURFACE_TYPE 0x3033
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_RED_SIZE 0x0032
#define EGL_GREEN_SIZE 0x0033
#define EGL_BLUE_SIZE 0x0034
#define EGL_DEPTH_SIZE 0x0022

typedef EGLDisplay(*fGetDisplay)(EGLNativeDisplayType);
typedef EGLBoolean(*fInitialize)(EGLDisplay,EGLint*,EGLint*);
typedef EGLBoolean(*fBindAPI)(EGLenum);
typedef EGLBoolean(*fChooseConfig)(EGLDisplay,const EGLint*,EGLConfig*,EGLint,EGLint*);
typedef EGLSurface(*fCreateWindowSurface)(EGLDisplay,EGLConfig,EGLNativeWindowType,const EGLint*);
typedef EGLContext(*fCreateContext)(EGLDisplay,EGLConfig,EGLContext,const EGLint*);
typedef EGLBoolean(*fMakeCurrent)(EGLDisplay,EGLSurface,EGLSurface,EGLContext);
typedef EGLBoolean(*fSwapBuffers)(EGLDisplay,EGLSurface);
typedef const char*(*fQueryString)(EGLDisplay,EGLint);
typedef EGLint(*fGetError)(void);

static fGetDisplay eglGetDisplay;
static fInitialize eglInitialize;
static fBindAPI eglBindAPI;
static fChooseConfig eglChooseConfig;
static fCreateWindowSurface eglCreateWindowSurface;
static fCreateContext eglCreateContext;
static fMakeCurrent eglMakeCurrent;
static fSwapBuffers eglSwapBuffers;
static fQueryString eglQueryString;
static fGetError eglGetError;

#define Q(s) if(!(s)){fprintf(stderr,"EGL FAIL at %s (err=0x%x)\n", #s, eglGetError?eglGetError():0); exit(3);}

int main(int argc,char**argv){
  int frames=60; if(argc>1) frames=atoi(argv[1]);
  /* EGL via dlopen */
  void*eh=dlopen("libEGL.so.1",RTLD_NOW|RTLD_GLOBAL);
  if(!eh){fprintf(stderr,"dlopen libEGL: %s\n",dlerror());return 2;}
  eglGetDisplay=(fGetDisplay)dlsym(eh,"eglGetDisplay");
  eglInitialize=(fInitialize)dlsym(eh,"eglInitialize");
  eglBindAPI=(fBindAPI)dlsym(eh,"eglBindAPI");
  eglChooseConfig=(fChooseConfig)dlsym(eh,"eglChooseConfig");
  eglCreateWindowSurface=(fCreateWindowSurface)dlsym(eh,"eglCreateWindowSurface");
  eglCreateContext=(fCreateContext)dlsym(eh,"eglCreateContext");
  eglMakeCurrent=(fMakeCurrent)dlsym(eh,"eglMakeCurrent");
  eglSwapBuffers=(fSwapBuffers)dlsym(eh,"eglSwapBuffers");
  eglQueryString=(fQueryString)dlsym(eh,"eglQueryString");
  eglGetError=(fGetError)dlsym(eh,"eglGetError");
  if(!eglGetDisplay||!eglInitialize||!eglChooseConfig||!eglCreateContext){fprintf(stderr,"EGL dlsym missing\n");return 2;}
  fprintf(stderr,"EGL loaded: %s\n",eglQueryString(eglGetDisplay(EGL_DEFAULT_DISPLAY),0x3053)?:"?");

  /* X11 window */
  Display*dpy=XOpenDisplay(NULL);
  if(!dpy){fprintf(stderr,"no X display\n");return 4;}
  int scr=DefaultScreen(dpy);
  Window root=RootWindow(dpy,scr);
  int W=320,H=240;
  XSetWindowAttributes swa; swa.background_pixel=BlackPixel(dpy,scr); swa.event_mask=ExposureMask|StructureNotifyMask|KeyPressMask;
  Window win=XCreateWindow(dpy,root,0,0,W,H,0,DefaultDepth(dpy,scr),InputOutput,DefaultVisual(dpy,scr),CWBackPixel|CWEventMask,&swa);
  XStoreName(dpy,win,"nv triangle");
  XMapWindow(dpy,win);
  XEvent ev; while(XPending(dpy)) XNextEvent(dpy,&ev);

  /* EGL init */
  EGLDisplay d=eglGetDisplay(EGL_DEFAULT_DISPLAY);
  Q(d!=0);
  EGLint maj=0,mino=0; Q(eglInitialize(d,&maj,&mino));
  fprintf(stderr,"EGL %d.%d\n",maj,mino);
  if(eglBindAPI) eglBindAPI(EGL_OPENGL_API);
  EGLint cfgattr[]={EGL_SURFACE_TYPE,EGL_WINDOW_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_DEPTH_SIZE,24,EGL_NONE};
  EGLConfig cfg; EGLint ncfg=0;
  Q(eglChooseConfig(d,cfgattr,&cfg,1,&ncfg));
  if(ncfg<1){fprintf(stderr,"no config\n");return 5;}
  EGLSurface surf=eglCreateWindowSurface(d,cfg,(EGLNativeWindowType)win,0);
  Q(surf!=0);
  EGLContext ctx=eglCreateContext(d,cfg,0,0);
  Q(ctx!=0);
  Q(eglMakeCurrent(d,surf,surf,ctx));
  fprintf(stderr,"context current; GL_RENDERER=%s\n",(const char*)glGetString(GL_RENDERER));

  /* render loop */
  glClearColor(0,0,0,1);
  for(int i=0;i<frames;i++){
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glColor3f(1,0.9,0.2);
    glBegin(GL_TRIANGLES);
      glVertex2f(0.0f,0.6f);
      glVertex2f(-0.6f,-0.5f);
      glVertex2f(0.6f,-0.5f);
    glEnd();
    glFlush();
    eglSwapBuffers(d,surf);
    while(XPending(dpy)) XNextEvent(dpy,&ev);
  }
  fprintf(stderr,"RENDERED %d frames OK\n",frames);
  eglMakeCurrent(d,0,0,0);
  XUnmapWindow(dpy,win); XDestroyWindow(dpy,win); XCloseDisplay(dpy);
  return 0;
}
