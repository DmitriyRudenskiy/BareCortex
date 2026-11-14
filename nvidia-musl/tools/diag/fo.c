#define _GNU_SOURCE
#include <stdio.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

__attribute__((constructor)) static void mc(void){ fprintf(stderr, "FO interceptor loaded\n"); }
static int (*real_openat)(int, const char*, int, ...) = 0;
static int (*real_open)(const char*, int, ...) = 0;
static int (*real_unlink)(const char*) = 0;
static int (*real_rmdir)(const char*) = 0;
static int (*real_access)(const char*, int) = 0;
static int (*real_mkdir)(const char*, ...) = 0;
static void init(void){
  real_openat = dlsym(RTLD_NEXT,"openat"); real_open = dlsym(RTLD_NEXT,"open");
  real_unlink = dlsym(RTLD_NEXT,"unlink"); real_rmdir = dlsym(RTLD_NEXT,"rmdir");
  real_access = dlsym(RTLD_NEXT,"access"); real_mkdir = dlsym(RTLD_NEXT,"mkdir");
}
int openat(int d, const char *p, int fl, ...){ mode_t m=0; va_list a; va_start(a,fl); if(fl&(O_CREAT|O_TMPFILE)) m=va_arg(a,int); va_end(a); if(!real_openat) init(); int r = real_openat(d,p,fl,m); if (r<0) fprintf(stderr,"FO openat %s -> -1 (%m)\n", p); return r; }
int open(const char *p, int fl, ...){ mode_t m=0; va_list a; va_start(a,fl); if(fl&O_CREAT) m=va_arg(a,int); va_end(a); if(!real_open) init(); int r = real_open(p,fl,m); if (r<0) fprintf(stderr,"FO open %s -> -1 (%m)\n", p); return r; }
int unlink(const char *p){ if(!real_unlink) init(); int r = real_unlink(p); fprintf(stderr,"FO unlink %s -> %d\n", p, r); return r; }
int rmdir(const char *p){ if(!real_rmdir) init(); int r = real_rmdir(p); fprintf(stderr,"FO rmdir %s -> %d\n", p, r); return r; }
int access(const char *p, int m){ if(!real_access) init(); int r = real_access(p,m); if (r<0) fprintf(stderr,"FO access %s -> -1 (%m)\n", p); return r; }
int mkdir(const char *p, ...){ mode_t m=0; va_list a; va_start(a,p); m=va_arg(a,int); va_end(a); if(!real_mkdir) init(); int r = real_mkdir(p,m); fprintf(stderr,"FO mkdir %s -> %d\n", p, r); return r; }
