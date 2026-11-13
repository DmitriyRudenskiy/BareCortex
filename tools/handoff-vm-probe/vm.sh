#!/usr/bin/env bash
# Run one command on the LFS build host (the "handoff VM") from the Mac.
#
#   tools/handoff-vm-probe/vm.sh 'df -h /mnt/lfs; tail -3 /mnt/lfs/logs/chapter10.log'
#   echo 'whoami; id' | tools/handoff-vm-probe/vm.sh
#
# The VM answers only on the physical host: Mac -> 192.168.0.86 -> qemu guest on
# 127.0.0.1:2222. It holds /mnt/lfs and the chapter logs, and it reboots itself
# between phases, so "connection closed" usually means "mid-reboot", not "broken".
#
# Authentication is a password, not a key: the cloud seed that builds this guest
# (~/barecortex-vm/seed/user-data on the host) sets plain_text_passwd and
# ssh_pwauth and authorises no key at all. OpenSSH >= 8.4 feeds that password to
# ssh through SSH_ASKPASS_REQUIRE=force, which is what ~/barecortex-vm/vm.sh
# already does on the host itself; this script is the same mechanism with a
# ProxyJump added, because from the Mac the guest is only reachable through it.
#
#   BARECORTEX_VM_PASSWORD     the guest password, or
#   ~/.config/barecortex/vm.env  with a BARECORTEX_VM_PASSWORD=... line
#
# Two details are load bearing and are handled here rather than left to whoever
# writes the next one-liner:
#
#   1. Connection reuse is off. A reused socket skips authentication, and the
#      guest's sshd has MaxStartups 10:30:100: a burst of commands each opening
#      a connection gets refused at the listener, which looks exactly like a
#      dead VM.
#   2. The command runs through `bash -lc` on the far side. Without a login
#      shell, and with no PTY, a command that outlives its connection is killed
#      with SIGTERM (exit 143) instead of finishing.
set -euo pipefail

if [[ $# -lt 1 || "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  echo "usage: ${0##*/} 'command'   (or: echo 'command' | ${0##*/})" >&2
  exit 2
fi

JUMP=${BARECORTEX_VM_JUMP:-192.168.0.86}
GUEST=${BARECORTEX_VM_USER:-bct}
PORT=${BARECORTEX_VM_PORT:-2222}
ENV_FILE=$HOME/.config/barecortex/vm.env

[[ -n ${BARECORTEX_VM_PASSWORD:-} ]] || {
  [[ -r $ENV_FILE ]] && BARECORTEX_VM_PASSWORD=$(grep -m1 '^BARECORTEX_VM_PASSWORD=' "$ENV_FILE" | cut -d= -f2-)
}
if [[ -z ${BARECORTEX_VM_PASSWORD:-} ]]; then
  echo "${0##*/}: no guest password. Set BARECORTEX_VM_PASSWORD, or put it in" >&2
  echo "  $ENV_FILE as BARECORTEX_VM_PASSWORD=... (the same value the VM's cloud seed" >&2
  echo "  sets; on the host it is also in ~/barecortex-vm/.pw)." >&2
  exit 3
fi

# The askpass helper holds the password in a 0700 file for the length of one ssh.
ASKPASS=$(mktemp) && printf '#!/bin/sh\necho "%s"\n' "$BARECORTEX_VM_PASSWORD" > "$ASKPASS"
chmod 700 "$ASKPASS"
trap 'rm -f "$ASKPASS"' EXIT
KNOWN_HOSTS=${TMPDIR:-/tmp}/barecortex-vm-known-hosts

# Read our own stdin once: the retry below has to feed the same bytes again.
STDIN=""
[[ -t 0 ]] || STDIN=$(cat)

run() {
  # Single quotes in the command survive by being closed, escaped, reopened.
  local quoted
  quoted=$(printf "%s" "$1" | sed "s/'/'\\\\''/g")
  SSH_ASKPASS=$ASKPASS SSH_ASKPASS_REQUIRE=force SSH_ASKPASS_DELAY=1000 \
  ssh -o BatchMode=no -o ConnectTimeout=25 -o ServerAliveInterval=15 -o ServerAliveCountMax=4 \
      -o ControlMaster=no -o ControlPath=none \
      -o PreferredAuthentications=password -o PubkeyAuthentication=no \
      -o StrictHostKeyChecking=accept-new -o "UserKnownHostsFile=$KNOWN_HOSTS" \
      -o "ProxyJump=$JUMP" -p "$PORT" "$GUEST@127.0.0.1" \
      "cat > /tmp/bc-stdin.\$\$.sh; bash -lc '$quoted' < /tmp/bc-stdin.\$\$.sh; rm -f /tmp/bc-stdin.\$\$.sh" \
      <<< "$STDIN"
}

# One retry, and only for the failure a live VM actually produces: the guest is
# mid-reboot, or its sshd is refusing. A command that ran and failed is returned
# untouched -- retrying a half-run build step is worse than reporting it.
out=$(run "$1" 2>&1) && { printf '%s\n' "$out"; exit 0; }
rc=$?
if printf '%s' "$out" | grep -qiE 'Connection (closed|reset|timed out)|kex_exchange_identification|Connection refused|closed by UNKNOWN'; then
  echo "${0##*/}: guest ssh hiccup (reboot? MaxStartups?), retrying once" >&2
  sleep 8
  out=$(run "$1" 2>&1) && { printf '%s\n' "$out"; exit 0; }
  rc=$?
fi
printf '%s\n' "$out" >&2
exit "$rc"
