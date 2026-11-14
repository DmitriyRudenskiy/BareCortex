# The handoff VM

The machine the LFS build runs on: a qemu guest on the physical host
(`192.168.0.86`), reachable from the Mac only through that host. It is called the
handoff VM because work arrives here as a handoff — a disk image, a phase to
resume — rather than as a checkout someone logs into.

Everything below was read off the machine, not remembered. Where a number is
quoted it is the number that was measured, and the command that measures it is
next to it.

## How to reach it

```console
tools/handoff-vm-probe/vm.sh 'df -h /mnt/lfs; tail -3 /mnt/lfs/logs/chapter10.log'
```

Mac → `user@192.168.0.86` (ProxyJump) → `bct@127.0.0.1:2222` (qemu's forwarded
ssh). On the host itself the project's own runner does the same without the jump:
`~/barecortex-vm/vm.sh sh '<cmd>'`.

## What it is

| | |
|---|---|
| guest | Debian 13 (`bc-build`), cloud image, `ttyS0` console |
| vcpus / ram | 8 / 8 GiB (`qemu -smp 8 -m 8192`, `-accel kvm -cpu host`) |
| root disk | `work.qcow2`, virtio |
| build disk | `lfs.qcow2` → `/dev/vdc1`, mounted at `/mnt/lfs` per phase |
| ssh | `bct@127.0.0.1:2222`, password auth, `sudo` NOPASSWD |
| console | `~/barecortex-vm/console.log` on the host (`vm.sh console`) |

The cloud seed (`~/barecortex-vm/seed/user-data`) is the authority on the account:
it creates `bct`, sets `sudo: ALL=(ALL) NOPASSWD:ALL`, `ssh_pwauth: true`,
`disable_root: true` — and **authorises no SSH key at all**. Password is not a
fallback here, it is the only door. The host keeps the value in
`~/barecortex-vm/.pw`; the Mac reads `BARECORTEX_VM_PASSWORD` (or
`~/.config/barecortex/vm.env`), and nothing in this repo stores it.

## Two things that will bite you

**Connection reuse is off, on purpose.** A reused socket skips authentication,
and the guest's sshd keeps `MaxStartups 10:30:100`: a burst of commands, each
opening its own connection, gets refused at the listener. The refusal arrives as
`Connection closed by UNKNOWN port 65535`, which is indistinguishable from a dead
VM. `vm.sh` passes `-o ControlMaster=no -o ControlPath=none` so every call
authenticates properly and none of them pile up.

**A remote command needs a login shell.** Without a PTY and without `bash -lc`,
a command that outlives its connection is killed with SIGTERM — exit 143 —
instead of finishing. `vm.sh` wraps everything in `bash -lc` for that reason; a
long phase belongs in `nohup` on the guest, not in a foreground ssh.

## When it does not answer

`Connection closed by 127.0.0.1 port 2222` while qemu is running and the console
shows a login prompt means **the guest's sshd is not accepting**, not that the
path is wrong. Confirm with the host's own runner, which shares no code with the
Mac's:

```console
ssh user@192.168.0.86 'cd ~/barecortex-vm && ./vm.sh sh "echo up"'
```

If that fails too, the guest needs its sshd (or a boot); nothing on the Mac can
fix it. Do not stop or restart the VM to find out — a phase may be mid-write on
`/mnt/lfs`. `vm.sh` retries once, because the ordinary reason is a reboot between
phases.

## What is on it

`/mnt/lfs` is the LFS tree and `/mnt/lfs/logs` holds one log per phase; the build
orchestrator (`scripts/build/build-rootfs.sh`) drives it and `phases.toml` says what is
due. `docs/BUILD-LOG.md` is the running record of what each phase did.
