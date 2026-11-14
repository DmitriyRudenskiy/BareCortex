#!/bin/sh
# 2.2-lite loader forensics: can the headless NVIDIA set load under STOCK Alpine
# musl (no patched libc), with gcompat, and what does the libnvcompat shim add?
#
# Everything runs in a bwrap sandbox whose / is the Alpine rootfs, so the loader
# under test is Alpine's own /lib/ld-musl-x86_64.so.1. One process per library
# (guardrail 2): a crash is recorded, it never ends the run.
#
# Statuses per library (guardrail 1):
#   PASS_LOADER  dlopen(RTLD_NOW) returned a handle
#   PASS_ABI     dlsym on the entry points returned non-NULL
#   NO_DEVICE    the entry point was callable and returned an error code
#                ("loader OK, device absent") — a PASS, not a fail
#   CRASH/FAIL   signal, timeout, or NULL handle
#
# usage: dlopen-harness.sh [--set headless|all] [--shim] [--call] [--strace]
#   --preload PATH  LD_PRELOAD this into every probe (e.g. /lib/libgcompat.so.0 —
#             required on Alpine: musl's loader maps libc.so.6/libm.so.6/... to
#             ITSELF, so gcompat is never pulled in by DT_NEEDED. Measured 14.11,
#             report/07. The Chimera PR dodges it with --replace-needed libc.so.6
#             libc.so plus gcompat-libs, which ships libc.so.)
#   --shim    also test copies sanitized with the Chimera patchelf recipe
#             (PR #6531 minus the libc.so rename, which Alpine's gcompat does
#             not need: it ships libc.so.6 itself)
#   --call    enable the call stage (nvmlInit_v1 / cuInit)
#   --strace  also record strace -f -e trace=openat,mmap per library
#
# env: ROOTFS (Alpine rootfs with harness+gcompat), NV (extracted .run dir),
#      OUT (log dir), LIBNVC (libnvcompat.so.0)
set -u

ROOTFS=${ROOTFS:-$HOME/lite/edge}
NV=${NV:-$HOME/.cache/nv580/ex/NVIDIA-Linux-x86_64-580.196.07}
OUT=${OUT:-$HOME/lite/out}
LIBNVC=${LIBNVC:-$HOME/lite/pr/libnvcompat.so.0}
NVCMAP=${NVCMAP:-$HOME/lite/pr/libnvcompat.map}
SET=headless SHIM=0 CALL=0 STRACE=0 PRELOAD=

while [ $# -gt 0 ]; do
	case $1 in
	--set) SET=$2; shift ;;
	--shim) SHIM=1 ;;
	--call) CALL=1 ;;
	--preload) PRELOAD=$2; shift ;;
	--strace) STRACE=1 ;;
	*) echo "unknown arg: $1" >&2; exit 64 ;;
	esac
	shift
done

SAN=$OUT/sanitized
mkdir -p "$OUT" "$SAN" "$ROOTFS"/{nv,work,out,probe}
SUM=$OUT/summary.tsv
TRACELOG=$OUT/trace.txt
: >"$SUM"
: >"$TRACELOG"
printf 'file\ttrace\tloader\tabi\tcall\tlog\n' >>"$SUM"

# --- the file set -------------------------------------------------------------
# headless = ladder step 1 (report/03): CUDA compute + NVML + MPS helpers.
# Real files only — the payload also ships .so.1 symlinks, which would double up.
if [ "$SET" = all ]; then
	files=$(find "$NV" -maxdepth 1 -type f | sort)
else
	files=$(cd "$NV" && ls -d libcuda.so.* libnvidia-ml.so.* nvidia-smi \
		nvidia-modprobe nvidia-persistenced libnvidia-tls.so.* \
		libnvidia-opencl.so.* libnvidia-ptxjitcompiler.so.* libnvidia-glsi.so.* \
		2>/dev/null | while read -r f; do [ -f "$f" ] && [ ! -L "$f" ] && echo "$PWD/$f"; done | sort -u)
fi
files=$(echo "$files" | tr '\n' ' ')
[ -n "$(echo "$files" | tr -d ' ')" ] || { echo "no files matched under $NV" >&2; exit 1; }

# shim inputs are staged into one dir so the sandbox bind always exists
PROBE=$(mktemp -d)
trap 'rm -rf "$PROBE"' EXIT
if [ "$SHIM" = 1 ]; then
	[ -f "$LIBNVC" ] && [ -f "$NVCMAP" ] || { echo "shim needs $LIBNVC and $NVCMAP" >&2; exit 1; }
	cp "$LIBNVC" "$NVCMAP" "$PROBE/"
fi

# --- sandbox helpers ----------------------------------------------------------
sb() { # sb <log> <cmd...> : run one command in the sandbox, capture everything
	log=$1; shift
	bwrap --ro-bind "$ROOTFS" / --ro-bind "$NV" /nv --bind "$SAN" /work \
		--bind "$OUT" /out --ro-bind "$PROBE" /probe \
		--dev /dev --proc /proc --tmpfs /tmp --unshare-user --uid 0 \
		--die-with-parent \
		env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin LD_LIBRARY_PATH=/work:/nv:/probe \
		${PRELOAD:+LD_PRELOAD=$PRELOAD} timeout 90 "$@" >"$log" 2>&1
}

# --- optional shim pass: PR #6531 recipe on copies ----------------------------
if [ "$SHIM" = 1 ]; then
	echo "== sanitizing copies with the libnvcompat recipe -> $SAN"
	# Alpine's gcompat ships libc.so.6 only; the PR's rename needs a libc.so
	# pointing at it (Chimera ships this as the gcompat-libs package).
	[ -e "$ROOTFS/lib/libc.so" ] || ln -s libgcompat.so.0 "$ROOTFS/lib/libc.so"
	for f in $files; do
		cp "$f" "$SAN/$(basename "$f")"
	done
	for f in $files; do
		b=$(basename "$f")
		sb "$OUT/patchelf.$b.log" patchelf \
			--remove-needed ld-linux-x86-64.so.2 \
			--remove-needed libdl.so.2 --remove-needed libm.so.6 \
			--remove-needed libpthread.so.0 --remove-needed librt.so.1 \
			--replace-needed libc.so.6 libc.so \
			--add-needed libnvcompat.so.0 \
			--rename-dynamic-symbols /probe/libnvcompat.map \
			"/work/$b" || echo "PATCHELF_FAIL $b"
	done
fi

# --- probe each library -------------------------------------------------------
syms_for() {
	case $1 in
	libcuda.so.*) echo "cuInit" ;;
	libnvidia-ml.so.*) echo "nvmlInit_v2 nvmlDeviceGetName" ;;
	*) echo "" ;;
	esac
}
calls_for() {
	[ "$CALL" = 1 ] || { echo ""; return; }
	case $1 in
	libcuda.so.*) echo "--calli0 cuInit" ;;
	libnvidia-ml.so.*) echo "--call0 nvmlInit_v2" ;;
	*) echo "" ;;
	esac
}

for f in $files; do
	b=$(basename "$f")
	syms=$(syms_for "$b")
	calls=$(calls_for "$b")

	# inside the sandbox the payload is /nv and the sanitized copies are /work
	[ "$SHIM" = 1 ] && target=/work/$b || target=/nv/$b

	# 1. loader's own dependency walk (musl: --list == LD_TRACE_LOADED_OBJECTS)
	sb "$OUT/ldlist.$b.log" /lib/ld-musl-x86_64.so.1 --list "$target"
	trc=$?
	{
		echo "### $b"
		cat "$OUT/ldlist.$b.log"
		echo
	} >>"$TRACELOG"
	if grep -q "^bwrap:" "$OUT/ldlist.$b.log"; then trcs=SANDBOX_ERROR
	elif grep -q "error loading\|cannot load" "$OUT/ldlist.$b.log"; then trcs=MISSING
	else trcs=OK; fi

	# 2. dlopen RTLD_NOW (+ dlsym, + optional call), in its own process
	if [ "$STRACE" = 1 ]; then
		sb "$OUT/dlopen.$b.log" strace -f -e trace=openat,mmap \
			-o "/out/strace.$b.log" /usr/local/bin/harness "$target" $syms $calls
	else
		sb "$OUT/dlopen.$b.log" /usr/local/bin/harness "$target" $syms $calls
	fi
	rc=$?

	loader=$(grep -m1 -o "LOADER_OK\|LOADER_FAIL" "$OUT/dlopen.$b.log" || true)
	[ -n "$loader" ] || loader=NO_STATUS
	grep -q "^bwrap:" "$OUT/dlopen.$b.log" && loader=SANDBOX_ERROR
	nok=$(grep -c "^SYM_OK" "$OUT/dlopen.$b.log" || true)
	nnull=$(grep -c "^SYM_NULL" "$OUT/dlopen.$b.log" || true)
	abi=-
	[ "$syms" = "" ] || abi="$nok/$((nok + nnull))"
	call=-
	if [ -n "$calls" ]; then
		if grep -q "^CALL_OK" "$OUT/dlopen.$b.log"; then
			call=$(grep -m1 "^CALL_OK" "$OUT/dlopen.$b.log" | sed 's/CALL_OK //')
			[ "$rc" = 0 ] || call="$call rc=$rc"
		else
			call=CRASH_OR_FAIL
		fi
	fi
	[ "$rc" -ge 128 ] && loader="CRASH(sig$((rc - 128)))"
	[ "$rc" = 124 ] && loader=TIMEOUT

	printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$b" "$trcs" "$loader" "$abi" "$call" \
		"$OUT/dlopen.$b.log" >>"$SUM"
	printf '%-36s trace=%-8s loader=%-16s abi=%-5s call=%s\n' "$b" "$trcs" \
		"$loader" "$abi" "$call"
done

echo
echo "== summary: $SUM"
echo "== loader walk: $TRACELOG"
