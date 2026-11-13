# Evidence from the build host

Two measurements, both taken on the x86_64 physical host (`192.168.0.86`), both
re-runnable, neither needing root:

- `alpine-probe/` — what taking OpenRC, busybox and musl from Alpine v3.24 would
  cost, and what its dependency graph looks like. Write-up:
  `docs/ALPINE-AS-BASE.md`; decision record: `openspec/changes/alpine-as-base/`.
- The LFS build host itself is documented in `docs/HANDOFF-VM.md`, and the
  commands that read it are one-liners through `tools/handoff-vm-probe/vm.sh`,
  for example:

  ```console
  tools/handoff-vm-probe/vm.sh 'uname -a; nproc; free -h; df -h /mnt/lfs'
  ```

  Nothing in this directory stores a credential. The guest password is read from
  `BARECORTEX_VM_PASSWORD` or `~/.config/barecortex/vm.env`.

The Alpine probe deliberately does not use the build host's LFS guest: that VM
reboots between build phases and its sshd is not always listening, so a
measurement that depended on it would not be reproducible.
