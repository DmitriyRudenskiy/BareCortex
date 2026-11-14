# 09 — 2.4: deviceQuery + first allocation, native musl (ladder step 3)

Date: 2026-11-14, same (gpu) host and lite env as `report/08`
(`user@192.168.0.86`, open modules 580.159.03, stock Alpine musl 1.2.6-r6 +
gcompat + sanitized host userspace). `tools/devicequery.c` — a musl-native
binary that `dlopen`s the sanitized `libcuda.so.1` and walks the driver API
(no CUDA toolkit: driver API needs no headers we do not declare).

```
cuInit -> 0
driver version: 13000
cuDeviceGetCount -> 0, devices=5
device 0: NVIDIA GeForce RTX 4060 Ti, CC 8.9, 15947 MiB   (×5, identical)
cuMemAlloc 1MiB -> rc=0 ptr=0x7a499d000000
```

`cuMemAlloc_v2` on the primary context (`cuDevicePrimaryCtxRetain` +
`cuCtxSetCurrent`) is the first **real device memory** obtained by a
musl-native process. Ladder step 3 (CUDA: `cuInit` + `deviceQuery`) is
**PASS — natively**, beyond what the rung demanded.

Deviation on record: TASK.md 2.4 orders the jail proof first ("fallback
обязан быть доказан"). We proved the native path first. The jail plan
(report/04, 8 steps) stays written and unexecuted; its value now is only as
the PyTorch rung's vehicle (official wheels are glibc), not as a fallback
for steps 2–3, which pass natively.

Not claimed: compute. No kernel launch has run under musl; `cuMemAlloc`
exercises the memory path only. The PyTorch rung (step 4) is where compute
gets tested for real.

Reproduce (on the host, after `run-gpu23.sh` staged the sanitized libs):

```sh
bwrap --bind ~/lite/edge / --proc /proc --dev /dev --tmpfs /tmp \
  --bind ~/lite /l --unshare-user --uid 0 --die-with-parent \
  env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin \
  cc -O2 -o /l/devicequery /l/devicequery.c -ldl
bwrap --bind ~/lite/edge / --dev-bind /dev /dev --ro-bind /sys /sys \
  --proc /proc --tmpfs /tmp --unshare-user --uid 0 --die-with-parent \
  env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin /l/devicequery /usr/lib/libcuda.so.1
```
