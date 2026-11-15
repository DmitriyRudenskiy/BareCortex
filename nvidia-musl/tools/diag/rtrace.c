#define _GNU_SOURCE
#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
static int fd=-1;
static void L(const char*f,...){ if(fd<0)fd=open("/gl/rtrace.log",1|0100|02000,0644); if(fd<0)return; char b[200]; va_list a; va_start(a,f); int n=vsnprintf(b,sizeof b,f,a); va_end(a); if(n>0)write(fd,b,n); }
int pthread_mutex_lock(pthread_mutex_t*m){static int(*r)(pthread_mutex_t*)=0;if(!r)r=dlsym(RTLD_NEXT,"pthread_mutex_lock");int x=r?r(m):-1;L("pthread_mutex_lock(%p)=%d",m,x);return x;}
int pthread_mutex_trylock(pthread_mutex_t*m){static int(*r)(pthread_mutex_t*)=0;if(!r)r=dlsym(RTLD_NEXT,"pthread_mutex_trylock");int x=r?r(m):-1;L("pthread_mutex_trylock(%p)=%d",m,x);return x;}
int pthread_mutex_timedlock(pthread_mutex_t*m,const struct timespec*t){static int(*r)(pthread_mutex_t*,const struct timespec*)=0;if(!r)r=dlsym(RTLD_NEXT,"pthread_mutex_timedlock");int x=r?r(m,t):-1;L("pthread_mutex_timedlock(%p)=%d",m,x);return x;}
int __nvcompat_mutex_lock(pthread_mutex_t*m){static int(*r)(pthread_mutex_t*)=0;if(!r)r=dlsym(RTLD_NEXT,"__nvcompat_mutex_lock");int x=r?r(m):-1;L("__nvcompat_mutex_lock(%p)=%d",m,x);return x;}
void*malloc(size_t n){static void*(*r)(size_t)=0;if(!r)r=dlsym(RTLD_NEXT,"malloc");void*p=r?r(n):0;static int c=0;L("malloc(%zu)=%p (n=%d)",n,p,++c);return p;}
char*getenv(const char*k){static char*(*r)(const char*)=0;if(!r)r=dlsym(RTLD_NEXT,"getenv");char*v=r?r(k):0;L("getenv(%s)=%p",k,v);return v;}
