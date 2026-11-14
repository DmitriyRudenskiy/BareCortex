/* devicequery — TASK.md 2.4 minimal rung, driver API only, no CUDA toolkit.
 * dlopens libcuda.so.1 (the sanitized one), then: cuInit, device count,
 * per-device name / compute capability / memory, driver version, and one
 * real cuMemAlloc_v2 on the primary context (exercises the memory ioctls).
 * musl-native binary; the loader under test is Alpine's own.
 * build (in the lite env): cc -O2 -o devicequery devicequery.c -ldl
 * usage: devicequery [path/to/libcuda.so.1]   (default: libcuda.so.1)
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

typedef int CUresult;
typedef int CUdevice;
typedef void *CUcontext;
typedef unsigned long long CUdeviceptr;

#define ATTR_CC_MAJOR 75
#define ATTR_CC_MINOR 76

int main(int argc, char **argv)
{
	const char *lib = argc > 1 ? argv[1] : "libcuda.so.1";
	void *h = dlopen(lib, RTLD_NOW | RTLD_GLOBAL);
	if (!h) { fprintf(stderr, "dlopen %s: %s\n", lib, dlerror()); return 2; }

	CUresult (*p_cuInit)(unsigned) = dlsym(h, "cuInit");
	CUresult (*p_cuDriverGetVersion)(int *) = dlsym(h, "cuDriverGetVersion");
	CUresult (*p_cuDeviceGetCount)(int *) = dlsym(h, "cuDeviceGetCount");
	CUresult (*p_cuDeviceGet)(CUdevice *, int) = dlsym(h, "cuDeviceGet");
	CUresult (*p_cuDeviceGetName)(char *, int, CUdevice) = dlsym(h, "cuDeviceGetName");
	CUresult (*p_cuDeviceTotalMem)(size_t *, CUdevice) = dlsym(h, "cuDeviceTotalMem_v2");
	CUresult (*p_cuDeviceGetAttribute)(int *, int, CUdevice) = dlsym(h, "cuDeviceGetAttribute");
	CUresult (*p_cuDevicePrimaryCtxRetain)(CUcontext *, CUdevice) = dlsym(h, "cuDevicePrimaryCtxRetain");
	CUresult (*p_cuCtxSetCurrent)(CUcontext) = dlsym(h, "cuCtxSetCurrent");
	CUresult (*p_cuMemAlloc)(CUdeviceptr *, size_t) = dlsym(h, "cuMemAlloc_v2");
	CUresult (*p_cuMemFree)(CUdeviceptr) = dlsym(h, "cuMemFree_v2");
	if (!p_cuInit || !p_cuDeviceGetCount || !p_cuDeviceGetName || !p_cuMemAlloc) {
		fprintf(stderr, "dlsym failed (missing entry point)\n");
		return 3;
	}

	CUresult rc = p_cuInit(0);
	printf("cuInit -> %d\n", rc);
	if (rc) return 1;

	int drv = -1;
	if (p_cuDriverGetVersion && p_cuDriverGetVersion(&drv) == 0)
		printf("driver version: %d\n", drv);

	int n = 0;
	rc = p_cuDeviceGetCount(&n);
	printf("cuDeviceGetCount -> %d, devices=%d\n", rc, n);
	if (rc || n <= 0) return 1;

	for (int i = 0; i < n; i++) {
		CUdevice d;
		char name[128] = "?";
		size_t mem = 0;
		int major = -1, minor = -1;
		if (p_cuDeviceGet(&d, i)) continue;
		p_cuDeviceGetName(name, sizeof name, d);
		if (p_cuDeviceTotalMem) p_cuDeviceTotalMem(&mem, d);
		if (p_cuDeviceGetAttribute) {
			p_cuDeviceGetAttribute(&major, ATTR_CC_MAJOR, d);
			p_cuDeviceGetAttribute(&minor, ATTR_CC_MINOR, d);
		}
		printf("device %d: %s, CC %d.%d, %zu MiB\n", i, name, major, minor, mem >> 20);
	}

	/* one real allocation on device 0's primary context */
	if (p_cuDevicePrimaryCtxRetain && p_cuCtxSetCurrent && p_cuMemAlloc) {
		CUdevice d0;
		CUcontext ctx = NULL;
		CUdeviceptr p = 0;
		if (p_cuDeviceGet(&d0, 0) == 0 &&
		    p_cuDevicePrimaryCtxRetain(&ctx, d0) == 0 &&
		    p_cuCtxSetCurrent(ctx) == 0) {
			rc = p_cuMemAlloc(&p, 1u << 20);
			printf("cuMemAlloc 1MiB -> rc=%d ptr=0x%llx\n", rc, (unsigned long long)p);
			if (!rc && p_cuMemFree) p_cuMemFree(p);
			return rc ? 1 : 0;
		}
		fprintf(stderr, "primary context unavailable\n");
		return 1;
	}
	return 0;
}
