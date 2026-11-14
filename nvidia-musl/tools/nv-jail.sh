#!/bin/sh
# nv-jail — approach A (report/04): glibc jail for PyTorch on the musl host.
# Minimal rung: torch.cuda.is_available() == True inside an ubuntu-base
# rootfs under bwrap, with the HOST's driver bound in (no modules installed
# inside; --no-kernel-modules by construction). No root; everything in $JAIL.
#
# usage: nv-jail.sh setup   # ubuntu-base rootfs + python3-pip (network on)
#        nv-jail.sh torch   # pip install torch (network on, big download)
#        nv-jail.sh run [python args...]   # run inside the jail (no net needed)
# env: JAIL (default ~/nv-jail), FLOOR_MB (disk floor, default 2048)
set -eu

JAIL=${JAIL:-$HOME/nv-jail}
mkdir -p "$JAIL/tmp" # pip scratch on disk, not in RAM (host is at 46/47GB)
FLOOR_MB=${FLOOR_MB:-2048}
CMD=${1:?usage: setup|torch|run}; shift || true

floor_guard() {
	free=$(df -Pm "$JAIL" 2>/dev/null | awk 'NR==2{print $4}')
	[ -n "$free" ] || return 0
	[ "$free" -ge "$FLOOR_MB" ] || {
		echo "DISK FLOOR: ${free}MB free < ${FLOOR_MB}MB — stopping, per operator" >&2
		exit 1; }
	echo "== disk: ${free}MB free (floor ${FLOOR_MB}MB)"
}

# ubuntu-base 24.04 (noble): glibc 2.39, python 3.12 — torch manylinux wheels fit
UB_DIR=https://cdimage.ubuntu.com/ubuntu-base/releases/24.04/release
HOSTLIB=/usr/lib/x86_64-linux-gnu
sb() { # the jail: host driver bound in (no modules inside), nvidia nodes per-node
	bwrap --bind "$JAIL" / --proc /proc --dev /dev --dev-bind "$JAIL/tmp" /tmp \
		--ro-bind /sys /sys --unshare-user --uid 0 --die-with-parent \
		--ro-bind /etc/resolv.conf /etc/resolv.conf \
		--ro-bind "$HOSTLIB"/libcuda.so.580.159.03 "$HOSTLIB"/libcuda.so.1 \
		--ro-bind "$HOSTLIB"/libnvidia-ml.so.580.159.03 "$HOSTLIB"/libnvidia-ml.so.1 \
		--dev-bind /dev/nvidiactl /dev/nvidiactl \
		--dev-bind /dev/nvidia-uvm /dev/nvidia-uvm \
		$(for i in 0 1 2 3 4; do echo --dev-bind /dev/nvidia$i /dev/nvidia$i; done) \
		"$@"
}

case $CMD in
setup)
	floor_guard
	[ -d "$JAIL" ] || mkdir -p "$JAIL"
	if [ ! -e "$JAIL/etc/os-release" ]; then
		U=$(curl -s "$UB_DIR/" | grep -oE 'ubuntu-base-24\.04(\.[0-9]+)?-base-amd64\.tar\.gz' | sort -uV | tail -1)
		[ -n "$U" ] || { echo "no ubuntu-base tarball at $UB_DIR" >&2; exit 1; }
		echo "== fetching $U"
		curl -sSL "$UB_DIR/$U" -o "$JAIL/$U"
		tar -xzf "$JAIL/$U" -C "$JAIL" && rm -f "$JAIL/$U"
	fi
	echo "== ubuntu $(sb sh -c 'grep -o "24.04[^ ]*" /etc/os-release | head -1' 2>/dev/null || echo '?')"
	# dpkg cannot chown inside a single-uid userns (gid 0 unmapped), so: download
	# only, then extract every .deb by hand with --no-same-owner. postinsts never
	# run; the two that matter here (ca-certificates, openssl) we invoke manually.
	A="apt-get -o APT::Sandbox::User=root -o APT::Immediate-Configure=0"
	sb $A update -qq
	sb $A install -y -qq -d --no-install-recommends \
		python3 python3-pip python3-venv ca-certificates openssl
	AARCH=$JAIL/var/cache/apt/archives
	command -v ar >/dev/null || { echo "need binutils (ar) on the host" >&2; exit 1; }
	for d in "$AARCH"/*.deb; do
		(cd "$AARCH" && b=$(basename "$d") && ar -t "$b" | grep -q data.tar.zst &&
			ar x "$b" data.tar.zst && tar -xf data.tar.zst --no-same-owner -C "$JAIL" && rm -f data.tar.zst) ||
		(cd "$AARCH" && b=$(basename "$d") && ar x "$b" data.tar.xz &&
			tar -xf data.tar.xz --no-same-owner -C "$JAIL" && rm -f data.tar.xz)
		echo "== extracted $(basename "$d")"
	done
	sb sh -c '(cd /usr/share/ca-certificates && ls mozilla/*) > /etc/ca-certificates.conf;
		update-ca-certificates >/dev/null 2>&1; python3 --version'
	echo "== jail ready"
	;;
torch)
	floor_guard
	[ -e "$JAIL/etc/os-release" ] || { echo "run setup first" >&2; exit 1; }
	sb python3 -m venv /opt/venv
	sb /opt/venv/bin/pip install --no-cache-dir torch
	;;
run)
	[ -e "$JAIL/etc/os-release" ] || { echo "run setup first" >&2; exit 1; }
	sb /opt/venv/bin/python "$@"
	;;
esac
