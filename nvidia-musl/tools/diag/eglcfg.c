/* Enumerate available EGL configs (surface types + renderable types). glibc, dlopen EGL. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
typedef void*EGLDisplay;typedef void*EGLConfig;typedef void*EGLContext;typedef void*EGLSurface;
typedef unsigned int EGLBoolean;typedef int EGLint;typedef void*EGLNativeDisplayType;
#define EGL_NONE 0x3000
#define EGL_SURFACE_TYPE 0x3033
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_BLUE_SIZE 0x0034
static void*EH;
typedef EGLBoolean(*fCh)(EGLDisplay,const EGLint*,EGLConfig*,EGLint,EGLint*);
typedef EGLBoolean(*fInit)(EGLDisplay,EGLint*,EGLint*);
typedef EGLDisplay(*fGD)(EGLNativeDisplayType);
typedef EGLint(*fAttrib)(EGLDisplay,EGLConfig,EGLint,EGLint*);
static fCh ch;static fInit ini;static fGD gd;static fAttrib att;
int main(void){
  EH=dlopen("libEGL.so.1",2); if(!EH){fprintf(stderr,"no egl\n");return 2;}
  gd=(fGD)dlsym(EH,"eglGetDisplay");ini=(fInit)dlsym(EH,"eglInitialize");
  ch=(fCh)dlsym(EH,"eglChooseConfig");att=(fAttrib)dlsym(EH,"eglGetConfigAttrib");
  EGLDisplay d=gd((void*)0); EGLint mj=0,mn=0; ini(d,&mj,&mn);
  fprintf(stderr,"EGL %d.%d\n",mj,mn);
  EGLint na=0; EGLint nl[]={EGL_NONE};
  ch(d,nl,0,0,&na);
  fprintf(stderr,"total configs=%d\n",na);
  EGLConfig*cfgs=malloc(sizeof(EGLConfig)*na);
  ch(d,nl,cfgs,na,&na);
  int shown=0;
  for(int i=0;i<na && shown<25;i++){
    EGLint st=0,rt=0,bs=0; att(d,cfgs[i],EGL_SURFACE_TYPE,&st);att(d,cfgs[i],EGL_RENDERABLE_TYPE,&rt);att(d,cfgs[i],EGL_BLUE_SIZE,&bs);
    int win=st&1, pb=st&2;
    int ogl=rt&1, gles=rt&2;
    if((win||pb)&&(ogl||gles)){
      fprintf(stderr,"cfg[%d] surface=0x%x(%s%s) render=0x%x(ogl=%d gles=%d) blue=%d\n",
        i,st,win?"W":"",pb?"P":"",rt,ogl,gles,bs);
      shown++;
    }
  }
  return 0;
}
