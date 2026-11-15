#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <unistd.h>
#include <stdlib.h>
/* Header-free standard EGL flow via dlsym. EGL_DEFAULT_DISPLAY=0, TRUE=1, FALSE=0.
   Goal: does eglInitialize (ICD load) succeed on musl? Where exactly is the panic? */
typedef void*EGLDisplay;typedef void*EGLConfig;typedef void*EGLContext;typedef void*EGLSurface;
typedef unsigned int EGLBoolean;typedef int EGLint;
static int lg(const char*m){int f=open("/gl/elg.log",0102,0644);if(f>=0){int n=(int)__builtin_strlen(m);write(f,m,n);close(f);}return 0;}
static void die(const char*m,int e){char b[128];int n=snprintf(b,sizeof b,"X %s errno=%d\n",m,e);int f=open("/gl/elg.log",0102,0644);if(f>=0){write(f,b,n);close(f);}_exit(99);}
int main(void){
  lg("START");
  void*h=dlopen("/egl/libEGL.so.1.1.0",2); if(!h){const char*e=dlerror();char b[256];int n=snprintf(b,sizeof b,"X DLOPEN: %s\n",e?e:"?");int f=open("/gl/elg.log",0102,0644);if(f>=0){write(f,b,n);close(f);}_exit(99);} lg("DLOPEN_OK");
  lg("resolving standard entry points");
  EGLBoolean(*eglGetDisplay)(void*)=(EGLBoolean(*)(void*))dlsym(h,"eglGetDisplay");
  EGLBoolean(*eglInitialize)(EGLDisplay,EGLint*,EGLint*)=(EGLBoolean(*)(EGLDisplay,EGLint*,EGLint*))dlsym(h,"eglInitialize");
  EGLDisplay(*eglGetDisplayF)(void*)=(EGLDisplay(*)(void*))dlsym(h,"eglGetDisplay");
  if(!eglGetDisplayF||!eglInitialize){die("DSYM_STD",0);} lg("DSYM_STD_OK");
  lg("calling eglGetDisplay(0)");
  EGLDisplay d=eglGetDisplayF((void*)0); lg("GETDISPLAY_OK");
  if(!d){lg("DISPLAY_NULL");_exit(97);}
  lg("calling eglInitialize (ICD LOAD happens here)");
  EGLint maj=0,mino=0;
  EGLBoolean ok=eglInitialize(d,&maj,&mino);
  lg("EGLINITIZE_RETURNED");   /* if panic is in eglInitialize, this is NOT logged */
  char b[96];int n=snprintf(b,sizeof b,"INIT ok=%d maj=%d min=%d\n",ok,maj,mino);
  int f=open("/gl/elg.log",0102,0644);if(f>=0){write(f,b,n);close(f);}
  lg("SUCCESS_REACHED_INIT");
  _exit(0);   /* skip any exit-teardown panic */
}
