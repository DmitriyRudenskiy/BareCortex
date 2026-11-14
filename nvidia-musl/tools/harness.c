/* dlopen probe for the nvidia-musl lite path (nvidia-musl/TASK.md 2.2).
 *
 * Build INSIDE the Alpine rootfs so it is a native musl binary and its dlopen()
 * is musl's loader — that is the thing under test. Driven by dlopen-harness.sh,
 * which runs one process per library so a crash cannot end the run.
 *
 * Status hierarchy (operator guardrail 1), one line each on stdout:
 *   LOADER_OK / LOADER_FAIL <dlerror>       dlopen(RTLD_NOW)
 *   SYM_OK <name> / SYM_NULL <name>         dlsym, ABI presence
 *   CALL_OK <name> rc=<n>                   the call returned (device absent is
 *                                           a PASS: "loader OK, device absent")
 *   CALL_CRASH                              only reachable via a signal, so the
 *                                           parent records it; kept for clarity
 *
 * Exit: 0 ok, 2 loader fail, 3 symbol missing, 64 usage.
 *
 * usage: harness <lib.so> [sym ...] [--call0 name] [--calli0 name]
 *   --call0   call int name(void)
 *   --calli0  call int name(int) with 0
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int (*fn0_t)(void);
typedef int (*fni_t)(int);

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: harness <lib.so> [sym ...] [--call0 n] [--calli0 n]\n");
		return 64;
	}
	void *h = dlopen(argv[1], RTLD_NOW | RTLD_GLOBAL);
	if (!h) {
		printf("LOADER_FAIL %s\n", dlerror());
		return 2;
	}
	printf("LOADER_OK\n");
	int rc = 0;

	for (int i = 2; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "--call0") || !strcmp(a, "--calli0")) {
			if (i + 1 >= argc)
				return 64;
			const char *nm = argv[++i];
			void *p = dlsym(h, nm);
			if (!p) {
				printf("SYM_NULL %s\n", nm);
				rc = 3;
				continue;
			}
			fflush(stdout);
			int r = a[6] ? ((fni_t)p)(0) : ((fn0_t)p)();
			printf("CALL_OK %s rc=%d\n", nm, r);
			continue;
		}
		void *p = dlsym(h, a);
		printf("%s %s\n", p ? "SYM_OK" : "SYM_NULL", a);
		if (!p)
			rc = 3;
	}
	return rc;
}
