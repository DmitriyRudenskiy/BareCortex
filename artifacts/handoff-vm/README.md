# Evidence from the build host

Two measurements and the host they were taken on, all at `192.168.0.86`, all
re-runnable, none needing root:

- `alpine-probe/` — what taking OpenRC, busybox and musl from Alpine v3.24 would
  cost, and what its dependency graph looks like. Write-up:
  `docs/ALPINE-AS-BASE.md`; decision record: `openspec/changes/alpine-as-base/`.
- `gentoo-probe/` — what a complete OpenRC system contains: the ELF census that
  finds no 32-bit code in 1 487 objects, and the graph the stage3's own glibc
  loader resolved. Write-up: `docs/GENTOO-AS-BASE.md`; decision record:
  `openspec/changes/amd64-only-userspace/`.
- The LFS build host itself is documented in `docs/HANDOFF-VM.md`, and the
  commands that read it are one-liners through `tools/handoff-vm-probe/vm.sh`,
  for example:

  ```console
  tools/handoff-vm-probe/vm.sh 'uname -a; nproc; free -h; df -h /mnt/lfs'
  ```

  Nothing in this directory stores a credential. The guest password is read from
  `BARECORTEX_VM_PASSWORD` or `~/.config/barecortex/vm.env`.

Neither probe uses the build host's LFS guest: that VM reboots between build
phases and its sshd is not always listening, so a measurement that depended on it
would not be reproducible. Both run unprivileged beside a build in flight.
