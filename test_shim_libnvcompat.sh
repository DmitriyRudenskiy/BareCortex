#!/bin/sh
# test_shim_libnvcompat.sh — the nvidia-musl compat shim must build, export
# every glibc symbol the NVIDIA userspace references (nm), and every wrapper
# must behave like the glibc original (dlopen/dlsym harness). The
# __pthread_key_create export is the load-bearing fix for the EGL ICD
# std::system_error abort (nvidia-musl/reports/14).
set -u

ROOT=$(cd -- "$(dirname -- "$0")" && pwd)
SHIM_SRC="$ROOT/nvidia-musl/tools/shim/libnvcompat.c"
HARNESS_SRC="$ROOT/nvidia-musl/tools/shim/test_shim_harness.c"
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fails=0

pass() { echo "PASS: $1"; }
fail() { echo "FAIL: $1"; fails=$((fails + 1)); }

[ "$(uname)" = Linux ] || { echo "SKIP: shim wraps Linux-only glibc symbols"; exit 0; }
# the mutex-kind patch reads musl's pthread_mutex_t layout; glibc builds
# cannot compile this shim, so find a musl compiler or skip.
musl_cc=""
printf '#include <pthread.h>\npthread_mutex_t m;\nint f(void){return m.__u.__i[0];}\n' > "$T/probe.c"
for c in ${MUSL_CC:-} musl-gcc cc gcc; do
	command -v "$c" >/dev/null 2>&1 || continue
	"$c" -c -o /dev/null "$T/probe.c" 2>/dev/null && { musl_cc=$c; break; }
done
[ -n "$musl_cc" ] || { echo "SKIP: needs a musl compiler (set MUSL_CC=/path/to/cc)"; exit 0; }
CC=$musl_cc

# gcompat present (Alpine target) => build exactly like nv-lite-install.sh,
# which makes the result byte-comparable to a known-good original.
gcflags=""
[ -e /lib/libgcompat.so.0 ] && gcflags="-Wl,--no-as-needed -Wl,-l:libgcompat.so.0 /lib/libgcompat.so.0"

# --- build (same flags as nv-lite-install.sh) ---------------------------------
if "$CC" -O2 -shared -fPIC -Wl,-soname,libnvcompat.so.0 $gcflags \
	-o "$T/libnvcompat.so.0" "$SHIM_SRC" 2>"$T/build.log"; then
	pass "shim builds"
else
	fail "shim does not build: $(cat "$T/build.log")"
	exit 1
fi

# --- soname: patchelf --add-needed libnvcompat.so.0 matches on soname ---------
if readelf -dW "$T/libnvcompat.so.0" 2>/dev/null | grep -qi "soname.*libnvcompat.so.0"; then
	pass "soname is libnvcompat.so.0"
else
	fail "wrong or missing soname (install would not resolve the NEEDED entry)"
fi

# --- byte-for-byte against a known-good original (ORIGINAL=/path/to/so) -------
# every SEC_ALLOC section must match exactly: that is the code and ABI the
# NVIDIA blobs execute. Non-allocated leftovers (.symtab STT_FILE embeds the
# source path, .comment the compiler version) are excluded on purpose.
if [ -n "${ORIGINAL:-}" ]; then
	secs_diff=0
	for sec in $(readelf -SW "$ORIGINAL" | awk '$7 ~ /A/ && $2 ~ /^\./ {print $2}'); do
		if objcopy -O binary --only-section="$sec" "$T/libnvcompat.so.0" "$T/a.bin" 2>/dev/null \
		   && objcopy -O binary --only-section="$sec" "$ORIGINAL" "$T/b.bin" 2>/dev/null \
		   && cmp -s "$T/a.bin" "$T/b.bin"; then
			:
		else
			echo "    $sec differs"
			secs_diff=$((secs_diff + 1))
		fi
	done
	if [ "$secs_diff" -eq 0 ]; then
		pass "all allocated sections byte-identical to $ORIGINAL"
	else
		fail "$secs_diff allocated section(s) differ from $ORIGINAL"
	fi
fi

# --- exported symbol set: what the NVIDIA ELFs actually reference -------------
for s in __nvcompat_mutex_lock __nvcompat_mutex_timedlock __nvcompat_mutex_trylock \
	dladdr1 __isoc23_strtol __isoc23_strtoul __isoc23_strtoll __isoc23_strtoull \
	__isoc23_sscanf fcntl64 alphasort64 scandir64 __pthread_key_create; do
	if nm -D --defined-only "$T/libnvcompat.so.0" | awk '{print $NF}' | grep -qx "$s"; then
		pass "exports $s"
	else
		fail "missing export: $s"
	fi
done

# --- functional harness: call each wrapper the way NVIDIA does ----------------
if "$CC" -O2 -o "$T/harness" "$HARNESS_SRC" -ldl 2>"$T/hbuild.log"; then
	out=$("$T/harness" "$T/libnvcompat.so.0" 2>&1)
	rc=$?
	echo "$out" | sed 's/^/    /'
	if [ $rc -eq 0 ]; then pass "functional harness"; else fail "functional harness"; fi
else
	fail "harness does not build: $(cat "$T/hbuild.log")"
fi

echo
if [ $fails -eq 0 ]; then echo "ALL PASS"; else echo "$fails FAILED"; fi
exit $((fails != 0))
