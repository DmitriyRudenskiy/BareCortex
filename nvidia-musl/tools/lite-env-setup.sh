#!/bin/sh
# Build the 2.2-lite test environment on a non-Alpine host, WITHOUT root.
#
# Produces, under $LITE:
#   $LITE/edge                 Alpine edge rootfs — musl 1.2.6-r6 + gcompat 1.1.0-r4,
#                              i.e. exactly the versions report/03 measured against
#   $LITE/edge/usr/local/bin/harness   tools/harness.c compiled by musl gcc, so its
#                              dlopen() IS Alpine's loader (the thing under test)
#   $LITE/pr/libnvcompat.so.0  Chimera PR #6531 shim, built per the PR recipe
#
# usage: lite-env-setup.sh            (idempotent; safe to re-run)
# env: LITE (default ~/lite), PR_SHA (PR #6531 head)
set -eu

LITE=${LITE:-$HOME/lite}
PR_SHA=${PR_SHA:-7b121686e15cc1d06ee1ee16968665004519d8a4}
TOOLS=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$LITE/pr"

# --- rootfs -------------------------------------------------------------------
U=$(curl -s https://dl-cdn.alpinelinux.org/alpine/edge/releases/x86_64/ |
	grep -oE 'alpine-minirootfs-[0-9.]+-x86_64\.tar\.gz' | sort -uV | tail -1)
[ -n "$U" ] || { echo "no edge minirootfs in the listing" >&2; exit 1; }
cd "$LITE"
[ -f "$U" ] || curl -sSLO "https://dl-cdn.alpinelinux.org/alpine/edge/releases/x86_64/$U"
[ -d edge ] || { mkdir -p edge; tar -xzf "$U" -C edge; }
echo "rootfs: Alpine $(cat edge/etc/alpine-release)"
# mount points for the probe sandbox (bwrap cannot mkdir through a read-only root)
mkdir -p "$LITE"/edge/{nv,work,out,probe}

# --- PR #6531 shim sources ----------------------------------------------------
for f in libnvcompat.c libnvcompat.map; do
	[ -s "pr/$f" ] || curl -sfL \
		"https://raw.githubusercontent.com/chimera-linux/cports/$PR_SHA/user/nvidia580/files/$f" \
		-o "pr/$f"
done

sb() {
	bwrap --bind "$LITE/edge" / --bind "$LITE/pr" /pr --ro-bind "$TOOLS" /src \
		--dev /dev --proc /proc --tmpfs /tmp --unshare-user --uid 0 \
		--ro-bind /etc/resolv.conf /etc/resolv.conf --die-with-parent \
		env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin HOME=/root "$@"
}

# --- toolchain + gcompat ------------------------------------------------------
sb apk update >/dev/null
sb apk add --no-cache gcompat musl-dev gcc patchelf strace binutils 2>&1 | tail -2

# --- harness (native musl) and the shim ---------------------------------------
sb cc -O2 -o /usr/local/bin/harness /src/harness.c -ldl
sb cc -O2 -nodefaultlibs -shared -Wl,-soname,libnvcompat.so.0 -Wl,--no-as-needed \
	-l:libgcompat.so.0 -o /pr/libnvcompat.so.0 /pr/libnvcompat.c

echo "== built"
sb apk info -e musl gcompat gcc patchelf
sb sh -c 'ls -l /usr/local/bin/harness /pr/libnvcompat.so.0; ldd --version 2>&1 | head -1'
