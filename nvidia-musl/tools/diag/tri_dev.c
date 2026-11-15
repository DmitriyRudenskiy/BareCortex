/* Headless OpenGL triangle via EGL device platform (no X11) -> FBO -> PPM.
 * Uses EGL_EXT_platform_device + EGL_EXT_device_query to bind a display to the
 * NVIDIA GPU directly, then renders a triangle into an FBO and reads it back.
 * glibc build; EGL via dlopen/dlsym, GL via header. */
#define _GNU_SOURCE
#define GL_GLEXT_PROTOTYPES
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <GL/gl.h>

typedef void*EGLDisplay;typedef void*EGLConfig;typedef void*EGLContext;typedef void*EGLSurface;typedef void*EGLDeviceEXT;
typedef unsigned int EGLBoolean;typedef int EGLint;typedef void*EGLNativeDisplayType;typedef unsigned int EGLenum;
#define EGL_NONE 0x3000
#define EGL_OPENGL_BIT 0x0001
#define EGL_OPENGL_API 0x30A2
#define EGL_SURFACE_TYPE 0x3033
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_RED_SIZE 0x0032
#define EGL_GREEN_SIZE 0x0033
#define EGL_BLUE_SIZE 0x0034
#define EGL_PBUFFER_BIT 0x0002
#define EGL_WINDOW_BIT 0x0001
#define EGL_PLATFORM_DEVICE_EXT 0x313F
#define EGL_DEVICE_COUNT_EXT 0x313E
#define EGL_DISPLAY_NAME_EXT 0x3555
#define EGL_DEVICE_TYPE_EXT 0x3100
#define EGL_DEVICE_TYPE_GPU_EXT 0x5001
#define EGL_DEVICE_TYPE_DISPLAY_EXT 0x5002
#define EGL_DEVICE_TYPE_CPU_EXT 0x5003
typedef EGLBoolean(*fQDI)(EGLDeviceEXT,EGLint,EGLint*);

typedef EGLDisplay(*fGD)(EGLNativeDisplayType);
typedef EGLBoolean(*fInit)(EGLDisplay,EGLint*,EGLint*);
typedef EGLBoolean(*fBind)(EGLenum);
typedef EGLBoolean(*fCh)(EGLDisplay,const EGLint*,EGLConfig*,EGLint,EGLint*);
typedef EGLContext(*fCC)(EGLDisplay,EGLConfig,EGLContext,const EGLint*);
typedef EGLBoolean(*fMC)(EGLDisplay,EGLSurface,EGLSurface,EGLContext);
typedef const char*(*fQS)(EGLDisplay,EGLint);
typedef EGLint(*fGE)(void);
typedef EGLBoolean(*fQD)(EGLint,EGLDeviceEXT*,EGLint*);
typedef EGLDisplay(*fGPD)(EGLenum,const void*,const EGLint*);
typedef EGLBoolean(*fQDS)(EGLDeviceEXT,EGLint,EGLint,char*);
typedef void*(*fEGPA)(const char*);
static void*EH;
static fGD gd;static fInit ini;static fBind bind;static fCh ch;static fCC cc;static fMC mc;
static fQS qs;static fGE ge;static fEGPA egpa;static fQD qd;static fGPD gpd;static fQDS qds;static fQDI qdi;
#define Q(s) if(!(s)){fprintf(stderr,"FAIL %s err=0x%x\n",#s,ge?ge():0);return 3;}

int main(int argc,char**argv){
  const char*out=argc>1?argv[1]:"/tmp/tri.ppm"; int W=256,H=256;
  EH=dlopen("libEGL.so.1",RTLD_NOW|RTLD_GLOBAL); if(!EH){fprintf(stderr,"no egl: %s\n",dlerror());return 2;}
  gd=(fGD)dlsym(EH,"eglGetDisplay");ini=(fInit)dlsym(EH,"eglInitialize");bind=(fBind)dlsym(EH,"eglBindAPI");
  ch=(fCh)dlsym(EH,"eglChooseConfig");cc=(fCC)dlsym(EH,"eglCreateContext");mc=(fMC)dlsym(EH,"eglMakeCurrent");
  qs=(fQS)dlsym(EH,"eglQueryString");ge=(fGE)dlsym(EH,"eglGetError");
  egpa=(fEGPA)dlsym(EH,"eglGetProcAddress");
  /* device ext funcs come via eglGetProcAddress (EGL 1.5 ext), not dlsym */
  if(egpa){ qd=(fQD)egpa("eglQueryDevicesEXT"); gpd=(fGPD)egpa("eglGetPlatformDisplayEXT"); qds=(fQDS)egpa("eglQueryDeviceStringEXT"); qdi=(fQDI)egpa("eglQueryDeviceAttribEXT"); }
  if(!qd||!gpd){ qd=(fQD)dlsym(EH,"eglQueryDevicesEXT"); gpd=(fGPD)dlsym(EH,"eglGetPlatformDisplayEXT"); qdi=(fQDI)dlsym(EH,"eglQueryDeviceAttribEXT"); }

  EGLDisplay d=0;
  if(qd&&gpd){
    EGLint nd=0; qd(0,0,&nd);
    fprintf(stderr,"devices=%d\n",nd);
    if(nd>0){
      EGLDeviceEXT*dev=malloc(sizeof(EGLDeviceEXT)*nd); qd(nd,dev,&nd);
      if(bind) bind(EGL_OPENGL_API);
      for(int i=0;i<nd;i++){
        EGLDisplay td=gpd(EGL_PLATFORM_DEVICE_EXT,dev[i],0);
        if(!td) continue;
        EGLint tmj=0,tmn=0;
        if(!ini(td,&tmj,&tmn)) continue;
        EGLint tnc=0; ch(td,(EGLint[]){EGL_NONE},0,0,&tnc);
        fprintf(stderr,"  dev[%d] init=%d.%d configs=%d\n",i,tmj,tmn,tnc);
        if(tnc>0){ d=td; fprintf(stderr,"  -> using dev[%d]\n",i); break; }
      }
      free(dev);
    }
  }
  if(!d){ fprintf(stderr,"NO device display with configs (headless device platform unavailable)\n"); return 5; }
  EGLint maj=0,mino=0; Q(ini(d,&maj,&mino)); fprintf(stderr,"EGL %d.%d\n",maj,mino);
  if(bind) bind(EGL_OPENGL_API);
  EGLint nc=0; ch(d,(EGLint[]){EGL_NONE},0,0,&nc);
  fprintf(stderr,"configs=%d\n",nc);
  if(nc<1){fprintf(stderr,"no configs on device display\n");return 5;}
  EGLConfig*cfgs=malloc(sizeof(EGLConfig)*nc); ch(d,(EGLint[]){EGL_NONE},cfgs,nc,&nc);
  EGLConfig cfg=cfgs[0];
  EGLContext c=cc(d,cfg,0,0); Q(c);
  Q(mc(d,0,0,c));   /* surfaceless: no window/pbuffer needed */
  fprintf(stderr,"GL_VENDOR=%s\nGL_RENDERER=%s\nGL_VERSION=%s\n",
    (const char*)glGetString(GL_VENDOR),(const char*)glGetString(GL_RENDERER),(const char*)glGetString(GL_VERSION));

  /* FBO */
  GLuint fbo,tx; glGenFramebuffers(1,&fbo); glGenTextures(1,&tx);
  glBindTexture(GL_TEXTURE_2D,tx);
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,W,H,0,GL_RGB,GL_UNSIGNED_BYTE,0);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
  glBindFramebuffer(GL_FRAMEBUFFER,fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tx,0);
  GLenum st=glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if(st!=GL_FRAMEBUFFER_COMPLETE){fprintf(stderr,"FBO incomplete 0x%x\n",st);return 7;}
  glViewport(0,0,W,H);
  glClearColor(0.05f,0.05f,0.1f,1);
  glBindFramebuffer(GL_FRAMEBUFFER,fbo);
  glClear(GL_COLOR_BUFFER_BIT);
  glColor3f(1.0f,0.85f,0.15f);
  glBegin(GL_TRIANGLES);
    glVertex2f(0.0f,0.55f); glVertex2f(-0.55f,-0.45f); glVertex2f(0.55f,-0.45f);
  glEnd();
  glFlush();
  unsigned char*px=malloc(W*H*3);
  glReadPixels(0,0,W,H,GL_RGB,GL_UNSIGNED_BYTE,px);
  int cx=W/2,cy=H/2; unsigned char*cc2=px+((H-1-cy)*W+cx)*3;
  fprintf(stderr,"center pixel=(%d,%d,%d)\n",cc2[0],cc2[1],cc2[2]);
  int tri=cc2[0]>120&&cc2[2]<120;
  FILE*f=fopen(out,"wb"); fprintf(f,"P3\n%d %d\n255\n",W,H);
  for(int y=0;y<H;y++)for(int x=0;x<W;x++){int i=(y*W+x)*3;fprintf(f,"%d %d %d ",px[i],px[i+1],px[i+2]);}
  fclose(f);
  fprintf(stderr,"WROTE %s triangle=%s\n",out,tri?"YES":"NO");
  return tri?0:6;
}
