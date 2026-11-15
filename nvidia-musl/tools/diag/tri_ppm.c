/* Headless OpenGL triangle (NVIDIA) via EGL pbuffer -> PPM image. No X11/display.
 * Proves the GPU actually rasterizes a triangle: renders one, reads back pixels,
 * writes a PPM. EGL via dlopen/dlsym (no libegl-dev). glibc build. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <GL/gl.h>

typedef void*EGLDisplay;typedef void*EGLConfig;typedef void*EGLContext;typedef void*EGLSurface;
typedef unsigned int EGLBoolean;typedef int EGLint;typedef void*EGLNativeDisplayType;
#define EGL_DEFAULT_DISPLAY ((EGLNativeDisplayType)0)
#define EGL_NONE 0x3000
#define EGL_OPENGL_BIT 0x0001
#define EGL_OPENGL_API 0x30A2
#define EGL_PBUFFER_BIT 0x0002
#define EGL_SURFACE_TYPE 0x3033
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_RED_SIZE 0x0032
#define EGL_GREEN_SIZE 0x0033
#define EGL_BLUE_SIZE 0x0034
#define EGL_WIDTH 0x3057
#define EGL_HEIGHT 0x3056
typedef unsigned int EGLenum;

typedef EGLDisplay(*fGetDisplay)(EGLNativeDisplayType);
typedef EGLBoolean(*fInitialize)(EGLDisplay,EGLint*,EGLint*);
typedef EGLBoolean(*fBindAPI)(EGLenum);
typedef EGLBoolean(*fChooseConfig)(EGLDisplay,const EGLint*,EGLConfig*,EGLint,EGLint*);
typedef EGLSurface(*fCreatePbuffer)(EGLDisplay,EGLConfig,const EGLint*);
typedef EGLContext(*fCreateContext)(EGLDisplay,EGLConfig,EGLContext,const EGLint*);
typedef EGLBoolean(*fMakeCurrent)(EGLDisplay,EGLSurface,EGLSurface,EGLContext);
typedef const char*(*fQueryString)(EGLDisplay,EGLint);
typedef EGLint(*fGetError)(void);
static fGetDisplay gGetDisplay;static fInitialize gInitialize;static fBindAPI gBindAPI;
static fChooseConfig gChooseConfig;static fCreatePbuffer gCreatePbuffer;static fCreateContext gCreateContext;
static fMakeCurrent gMakeCurrent;static fQueryString gQueryString;static fGetError gGetError;
#define Q(s) if(!(s)){fprintf(stderr,"EGL FAIL %s err=0x%x\n",#s,gGetError?gGetError():0);return 3;}

int main(int argc,char**argv){
  const char*out=argc>1?argv[1]:"/tmp/tri.ppm";
  int W=256,H=256;
  void*eh=dlopen("libEGL.so.1",RTLD_NOW|RTLD_GLOBAL);
  if(!eh){fprintf(stderr,"dlopen libEGL: %s\n",dlerror());return 2;}
  gGetDisplay=(fGetDisplay)dlsym(eh,"eglGetDisplay");
  gInitialize=(fInitialize)dlsym(eh,"eglInitialize");
  gBindAPI=(fBindAPI)dlsym(eh,"eglBindAPI");
  gChooseConfig=(fChooseConfig)dlsym(eh,"eglChooseConfig");
  gCreatePbuffer=(fCreatePbuffer)dlsym(eh,"eglCreatePbufferSurface");
  gCreateContext=(fCreateContext)dlsym(eh,"eglCreateContext");
  gMakeCurrent=(fMakeCurrent)dlsym(eh,"eglMakeCurrent");
  gQueryString=(fQueryString)dlsym(eh,"eglQueryString");
  gGetError=(fGetError)dlsym(eh,"eglGetError");
  if(!gGetDisplay||!gInitialize||!gChooseConfig||!gCreateContext){fprintf(stderr,"dlsym missing\n");return 2;}

  EGLDisplay d=gGetDisplay(EGL_DEFAULT_DISPLAY); Q(d);
  EGLint maj=0,mino=0; Q(gInitialize(d,&maj,&mino));
  fprintf(stderr,"EGL %d.%d\n",maj,mino);
  if(gBindAPI) gBindAPI(EGL_OPENGL_API);
  EGLint ca[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,
               EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_NONE};
  EGLConfig cfg;EGLint nc=0; Q(gChooseConfig(d,ca,&cfg,1,&nc)); if(nc<1){fprintf(stderr,"no pbuffer config\n");return 5;}
  EGLint pa[]={EGL_WIDTH,W,EGL_HEIGHT,H,EGL_NONE};
  EGLSurface s=gCreatePbuffer(d,cfg,pa); Q(s);
  EGLContext c=gCreateContext(d,cfg,0,0); Q(c);
  Q(gMakeCurrent(d,s,s,c));
  fprintf(stderr,"GL_VENDOR=%s\nGL_RENDERER=%s\nGL_VERSION=%s\n",
    (const char*)glGetString(GL_VENDOR),(const char*)glGetString(GL_RENDERER),(const char*)glGetString(GL_VERSION));

  glViewport(0,0,W,H);
  glClearColor(0.05f,0.05f,0.1f,1);
  glClear(GL_COLOR_BUFFER_BIT);
  glColor3f(1.0f,0.85f,0.15f);          /* warm yellow triangle */
  glBegin(GL_TRIANGLES);
    glVertex2f(0.0f,0.55f);
    glVertex2f(-0.55f,-0.45f);
    glVertex2f(0.55f,-0.45f);
  glEnd();
  glFlush();

  unsigned char*px=malloc(W*H*3);
  glReadPixels(0,0,W,H,GL_RGB,GL_UNSIGNED_BYTE,px);
  /* sample: center (in tri) vs top-left corner (clear color) */
  int cx=W/2, cy=H/2;
  unsigned char*cc=px+((H-1-cy)*W+cx)*3;
  unsigned char*tc=px+((H-1-(int)(H*0.30))*W+(int)(W*0.30))*3;
  fprintf(stderr,"pixel center=(%d,%d,%d) upper-area=(%d,%d,%d)\n",cc[0],cc[1],cc[2],tc[0],tc[1],tc[2]);
  int tri= cc[0]>120 && cc[2]<120;      /* yellow: high R, low B */
  FILE*f=fopen(out,"wb");
  fprintf(f,"P3\n%d %d\n255\n",W,H);
  for(int y=0;y<H;y++)for(int x=0;x<W;x++){int i=(y*W+x)*3;fprintf(f,"%d %d %d ",px[i],px[i+1],px[i+2]);}
  fclose(f);
  fprintf(stderr,"WROTE %s  triangle_rendered=%s\n",out,tri?"YES":"NO");
  free(px);
  return tri?0:6;
}
