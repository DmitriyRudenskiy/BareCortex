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
#   ./vm-host.sh disk 60G    create the LFS disk (/dev/vdb in the guest)
#   ./vm-host.sh snapshot t  freeze the LFS disk as checkpoint t, continue on a copy
#   ./vm-host.sh rollback t  discard the current LFS disk, resume from checkpoint t
#   ./vm-host.sh prune       delete checkpoints no image is built on (--do to actually do it)
#   ./vm-host.sh boot-lfs    boot the LFS disk alone — the first-boot test
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
  # The LFS disk is a second virtio disk — /dev/vdb — so the system under build
  # is never the system doing the building, and a ruined build costs a disk and
  # not a host. It is attached only when it exists, so every other use of this
  # script is unaffected.
  local lfsopt=""
  [ -f $VM/lfs.qcow2 ] && lfsopt="-drive if=virtio,file=$VM/lfs.qcow2"
  $QEMU -daemonize -pidfile $VM/vm.pid -D $VM/qemu.log \
    -m ${BC_MEM:-8192} -smp ${BC_SMP:-8} -machine q35 -accel kvm -cpu host \
    -L $P/usr/share/qemu \
    -drive if=virtio,file=$VM/work.qcow2 \
    -drive if=virtio,file=$VM/seed.iso,media=cdrom,readonly=on \
    ${lfsopt:+$lfsopt} \
    -netdev user,id=n0,hostfwd=tcp:127.0.0.1:${PORT}-:22 \
    -device virtio-net-pci,netdev=n0,romfile= \
    -display none -serial file:$VM/console.log
  echo "started pid $(cat $VM/vm.pid) on 127.0.0.1:${PORT}"
}

# The LFS partition lives on its own virtual disk, which is what lets chapter 2
# partition a real device and chapter 10 install a bootloader to it.
disk() {
  local size=${1:?usage: disk 60G}
  [ -f $VM/lfs.qcow2 ] && { echo "LFS disk exists: $(qemu-img info -U $VM/lfs.qcow2 | grep virtual)"; return 0; }
  stop
  $P/usr/bin/qemu-img create -f qcow2 $VM/lfs.qcow2 "$size" >/dev/null
  echo "LFS disk created ($size); restarting to attach it"
  start; wait_up 40
  rsh "lsblk -o NAME,SIZE,TYPE | grep disk"
}

# A checkpoint is an external qcow2 snapshot: the current image becomes an
# immutable parent and the build continues in a new child on top of it. Freezing
# costs a rename and a create, and rollback is choosing a different parent —
# which is why the phase table can afford a checkpoint after every phase.
snapshot() {
  local tag=${1:?usage: snapshot <phase>} n=0
  [ -f $VM/lfs.qcow2 ] || { echo "no LFS disk to checkpoint"; exit 1; }
  # A generation per tag, never a rename onto an existing parent: the second
  # checkpoint of a phase used to `mv` the child over the parent it still
  # pointed at, and the disk became its own backing file — qemu-img then recursed
  # through the chain until it ran out of file descriptors.
  mkdir -p $VM/sn
  while [ -f $VM/sn/$tag-$n.qcow2 ]; do n=$((n+1)); done
  stop
  mv $VM/lfs.qcow2 $VM/sn/$tag-$n.qcow2
  $P/usr/bin/qemu-img create -f qcow2 -F qcow2 -b $VM/sn/$tag-$n.qcow2 $VM/lfs.qcow2 >/dev/null
  prune --do
  echo "checkpointed LFS disk as $tag gen$n ($(du -h $VM/sn/$tag-$n.qcow2 | cut -f1)); building continues on a copy"
  start; wait_up 40
}

# Bounded retention. A checkpoint is a link in a qcow2 chain, and the working
# image's chain is the only history that still means anything: every other file in
# sn/ is a branch some rollback abandoned, worth nothing but disk. Deleting by age
# or by generation count would cut a parent out from under its child, so the rule
# is reachability, not count.
chain_names() {
  local img=${1:-$VM/lfs.qcow2}
  $P/usr/bin/qemu-img info -U --backing-chain "$img" \
    | sed -n "s,^backing file: \(.*\)$,\1,p; s,^image: \(.*\)$,\1,p" \
    | xargs -r -n1 basename | sort -u
}

prune() {
  local keep=$(chain_names) f n deleted=0
  [ -n "$keep" ] || { echo "cannot read the disk chain; keeping every checkpoint"; return 1; }
  for f in $VM/sn/*.qcow2; do
    [ -e "$f" ] || continue
    n=$(basename "$f")
    printf "%s\n" "$keep" | grep -Fxq "$n" && continue
    sz=$(du -h "$f" | cut -f1)
    if [ "$1" = "--do" ]; then
      rm -f "$f" && echo "deleted $n ($sz) - no image is built on it"
      deleted=$((deleted+1))
    else
      echo "would delete $n ($(du -h "$f" | cut -f1)) - no image is built on it"
    fi
  done
  [ "$1" = "--do" ] && echo "pruned $deleted checkpoint(s)"
  return 0
}

# The newest generation of a tag is what `rollback` means by that tag.
latest() {
  local tag=$1 n=0
  while [ -f $VM/sn/$tag-$((n+1)).qcow2 ]; do n=$((n+1)); done
  [ -f $VM/sn/$tag-$n.qcow2 ] && echo $n
}

rollback() {
  local tag=${1:?usage: rollback <phase>}
  [ -f $VM/sn/$tag.qcow2 ] || { echo "no checkpoint $tag"; exit 1; }
  stop
  rm -f $VM/lfs.qcow2
  $P/usr/bin/qemu-img create -f qcow2 -F qcow2 -b $VM/sn/$tag.qcow2 $VM/lfs.qcow2 >/dev/null
  echo "rolled back to $tag"
  start; wait_up 40
}

# The first-boot test is the LFS disk with nothing else attached: a system that
# boots only while its builder's disk is present is not bootable.
boot-lfs() {
  stop
  $QEMU -daemonize -pidfile $VM/vm.pid -D $VM/qemu.log \
    -m ${BC_MEM:-8192} -smp ${BC_SMP:-8} -machine q35 -accel kvm -cpu host \
    -L $P/usr/share/qemu \
    -boot order=h -drive if=virtio,file=$VM/lfs.qcow2 \
    -netdev user,id=n0,hostfwd=tcp:127.0.0.1:${PORT}-:22 \
    -device virtio-net-pci,netdev=n0,romfile= \
    -display none -serial file:$VM/console.log
  echo "booting the LFS disk; console: $VM/console.log"
}

stop() {
  [ -f $VM/vm.pid ] || { echo "not running"; return 0; }
  local pid; pid=$(cat $VM/vm.pid)
  # Ask the guest to shut itself down first. `kill` is a power cut: ext4 with
  # delayed allocation can lose a file created seconds earlier, and a checkpoint
  # that silently drops the files of the phase it just froze is worse than no
  # checkpoint. Only if the guest will not answer does this become a kill.
  if ssh -o BatchMode=yes -o ConnectTimeout=5 -p $PORT bct@127.0.0.1 \
       'sync; sudo poweroff' 2>/dev/null; then
    for _ in $(seq 1 60); do kill -0 "$pid" 2>/dev/null || break; sleep 0.5; done
    [ -f $VM/vm.pid ] && kill -0 "$pid" 2>/dev/null && { kill -9 "$pid" 2>/dev/null || true; }
    rm -f $VM/vm.pid; sync; echo stopped; return 0
  fi
  kill "$pid" 2>/dev/null
  # QEMU holds a write lock on the qcow2 until it is gone. Returning early makes
  # the next command fail with `Failed to get "write" lock`, which is exactly
  # what `grow` would then trip over.
  for _ in $(seq 1 60); do kill -0 "$pid" 2>/dev/null || break; sleep 0.5; done
  # `|| true`: if QEMU already exited on its own, kill -9 finds no process,
  # returns non-zero, and `set -e` silently aborts the caller — which is how
  # `grow` came to stop the VM and then exit before resizing anything.
  kill -9 "$pid" 2>/dev/null || true
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
  $P/usr/bin/qemu-img info -U "$VM/work.qcow2" | grep 'virtual size'
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
  disk) shift; disk "$@" ;;
  snapshot) shift; snapshot "$@" ;;
  rollback) shift; rollback "$@" ;;
  prune) shift; prune "$@" ;;
  boot-lfs) boot-lfs ;;
  console) tail -20 $VM/console.log ;;
  *) echo "usage: $0 {seed [pw]|start|stop|wait|sh <cmd>|grow <size>|disk <size>|snapshot <tag>|rollback <tag>|boot-lfs|console}"; exit 2 ;;
esac
