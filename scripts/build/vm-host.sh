#!/bin/bash
# BareCortex disposable build host.
#
# A KVM guest whose entire disk is a qcow2 file. This is what satisfies
# `build-orchestration 0.3` and design D10: the passwordless-root convention
# inherited from prepare_build.sh is acceptable on a disposable host, and a
# guest defined this way is disposable by construction — deleting the directory
# is the uninstall, and the physical machine is only ever written inside $HOME.
#
# qemu is not installed. It is extracted from Ubuntu's own .debs into
# ~/.local/qemu with dpkg-deb -x, because installing it needs root on a host
# this change is not allowed to modify. Its libraries are the host's, so
# LD_LIBRARY_PATH points at the extracted ones that are missing.
#
# The guest has no GPU: device passthrough needs root. So `(gpu)` work stays on
# the physical host and `(build)` work happens here — the two classes
# gpu-stack's scope note already separates.
#
#   ./vm-host.sh seed        build the cloud-init seed (new instance-id each run)
#   ./vm-host.sh start       start the guest, forwarding 127.0.0.1:2222 -> :22
#   ./vm-host.sh wait        wait until ssh answers
#   ./vm-host.sh sh <cmd>    run a command as bct (password from .pw, askpass)
#   ./vm-host.sh stop        kill the guest, waiting for the disk lock to free
#   ./vm-host.sh grow 64G    stop, enlarge the qcow2, grow the guest filesystem
#   ./vm-host.sh console     tail the boot console
#
# From outside the physical host:  ssh -J <host> -p 2222 bct@127.0.0.1
set -e
VM=$HOME/barecortex-vm
P=$HOME/.local/qemu
export LD_LIBRARY_PATH=$P/usr/lib/x86_64-linux-gnu
QEMU=$P/usr/bin/qemu-system-x86_64
PORT=${BC_VM_PORT:-2222}

[ -x "$QEMU" ] || { echo "qemu not extracted at $P — see the header"; exit 1; }

seed() {
  local pw=${1:-$(openssl rand -base64 12)}
  mkdir -p $VM/seed
  cat > $VM/seed/user-data <<UD
#cloud-config
users:
  - name: bct
    shell: /bin/bash
    groups: [users]
    sudo: ALL=(ALL) NOPASSWD:ALL
    lock_passwd: false
    plain_text_passwd: ${pw}
ssh_pwauth: true
disable_root: true
package_update: false
final_message: "BareCortex build host ready after \${UPTIME}s"
UD
  printf 'instance-id: bc-build-%s\nlocal-hostname: bc-build\n' "$(date +%s)" > $VM/seed/meta-data
  $P/usr/bin/genisoimage -output $VM/seed.iso -volid cidata -joliet -rock \
    $VM/seed/user-data $VM/seed/meta-data >/dev/null 2>&1
  echo "$pw" > $VM/.pw; chmod 600 $VM/.pw
  echo "seed rebuilt; guest password in $VM/.pw"
}

start() {
  # The base image is kept read-only and copied, so "reset the host" is one cp.
  [ -f $VM/work.qcow2 ] || { cp --reflink=auto $VM/base.qcow2 $VM/work.qcow2
    $P/usr/bin/qemu-img resize $VM/work.qcow2 ${BC_DISK:-24G} >/dev/null; }
  [ -f $VM/seed.iso ] || seed
  $QEMU -daemonize -pidfile $VM/vm.pid -D $VM/qemu.log \
    -m ${BC_MEM:-8192} -smp ${BC_SMP:-8} -machine q35 -accel kvm -cpu host \
    -L $P/usr/share/qemu \
    -drive if=virtio,file=$VM/work.qcow2 \
    -drive if=virtio,file=$VM/seed.iso,media=cdrom,readonly=on \
    -netdev user,id=n0,hostfwd=tcp:127.0.0.1:${PORT}-:22 \
    -device virtio-net-pci,netdev=n0,romfile= \
    -display none -serial file:$VM/console.log
  echo "started pid $(cat $VM/vm.pid) on 127.0.0.1:${PORT}"
}

stop() {
  [ -f $VM/vm.pid ] || { echo "not running"; return 0; }
  local pid; pid=$(cat $VM/vm.pid)
  kill "$pid" 2>/dev/null
  # QEMU holds a write lock on the qcow2 until it is gone. Returning early makes
  # the next command fail with `Failed to get "write" lock`, which is exactly
  # what `grow` would then trip over.
  for _ in $(seq 1 60); do kill -0 "$pid" 2>/dev/null || break; sleep 0.5; done
  kill -9 "$pid" 2>/dev/null
  rm -f $VM/vm.pid; echo stopped
}

# The disk budget is the binding constraint on this build, so growing it is one
# command rather than a sequence nobody remembers. The partition in the Debian
# cloud image already spans the device, so growpart reports NOCHANGE and the
# filesystem is the only thing that has to move; sgdisk -e relocates the backup
# GPT when the table does need it.
grow() {
  local size=${1:?usage: grow 64G}
  stop
  $P/usr/bin/qemu-img resize $VM/work.qcow2 "$size"
  start
  wait_up 40
  rsh "command -v sgdisk >/dev/null || sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq gdisk >/dev/null 2>&1
       sudo sgdisk -e /dev/vda >/dev/null 2>&1
       sudo growpart /dev/vda 1 2>&1 | tail -1
       sudo resize2fs /dev/vda1 2>&1 | tail -1
       sudo sgdisk -v /dev/vda 2>&1 | grep -i 'no problems' || echo 'GPT NOT CLEAN'
       df -h / | tail -1"
}

# Password auth without sshpass: OpenSSH >= 8.4 honours SSH_ASKPASS_REQUIRE=force.
rsh() {
  printf '#!/bin/sh\ncat %s/.pw\n' "$VM" > /tmp/bc-askpass.$$ && chmod +x /tmp/bc-askpass.$$
  SSH_ASKPASS=/tmp/bc-askpass.$$ SSH_ASKPASS_REQUIRE=force setsid -w \
    ssh -o BatchMode=no -o StrictHostKeyChecking=no -o UserKnownHostsFile=$VM/.kh \
        -o PreferredAuthentications=password -p $PORT bct@127.0.0.1 "$@" 2>&1
  rm -f /tmp/bc-askpass.$$
}

wait_up() {
  for i in $(seq 1 ${1:-60}); do
    rsh true >/dev/null 2>&1 && { echo "up after $((i*2))s"; return 0; }
    sleep 2
  done
  echo "not up — see $VM/console.log"; return 1
}

case "${1:-}" in
  seed) seed "${2:-}" ;;
  start) start ;;
  stop) stop ;;
  grow) shift; grow "$@" ;;
  wait) wait_up ;;
  sh) shift; rsh "$*" ;;
  console) tail -20 $VM/console.log ;;
  *) echo "usage: $0 {seed [pw]|start|stop|wait|sh <cmd>|console}"; exit 2 ;;
esac
