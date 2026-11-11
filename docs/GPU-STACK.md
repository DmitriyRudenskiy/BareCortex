# The GPU stack

Two things are written down nowhere else: the cooling policy, and how to read the
version matrix off a machine. Boot order for the services is
[INIT-LAYER.md](INIT-LAYER.md); the pinned hardware is
[HARDWARE_REQUIREMENTS.md](HARDWARE_REQUIREMENTS.md).

## The cooling policy

What exists today is **one write at start and one at stop**. No loop, no
threshold, no timer — that is the current design, not an omission of one:

| Moment | Command | Effect |
|---|---|---|
| `rc-service gpu-fancontrol start` | `bmc-fan --percent 80` (`FAN_SPEED`, `openrc/conf.d/gpu-fancontrol`) | per GPU: `nvidia-smi -i <N> -f 80`, then a read-back of the fan speed |
| `rc-service gpu-fancontrol stop` | `bmc-fan --percent 0` | "0 returns control to the driver" (`bmc-fan --help`) |

**The input field: there is none.** Nothing in this repository reads a
temperature. `bmc-fan` never opens a sensor — it writes a fan speed and reads the
fan speed back. The candidate fields for a polling policy (NVML's temperature,
reachable as `nvidia-smi --query-gpu=temperature.gpu --format=csv,noheader`, or
the hwmon `temp*_input` files) are `gpu-stack` 7.1's decision, and this is the
line that says the decision is open.

**The interval: there is none.** `conf.d/gpu-fancontrol` used to carry a
`CHECK_INTERVAL` that no code read. It is deleted, and the file says why: an
interval nobody polls on is a number an operator is expected to trust.

**The state a failed read leaves the fans in.** Today the only failure the
service can observe is a failed *write*, and it is loud — the four outcomes
`bmc-fan` prints, and what each does to the service:

| `bmc-fan` output | Exit | `rc-service` result |
|---|---|---|
| `OK gpu N accepted fan speed 80%`, then the read-back — or `does not report fan speed back; acceptance is the strongest statement available` | 0 | started |
| `REFUSED gpu N rejected fan speed 80%: <the driver's own words>` | 1 | failed |
| `INDETERMINATE device list unavailable; nothing was written` | 2 | failed |
| `UNAVAILABLE nvidia-smi not found; no fan state was read or written` | 3 | failed |

For the loop 7.1–7.3 will add, the state on a failed read is **control returned
to the driver** — `--percent 0`, the same write `stop()` already makes. The
reason is the same reason the wrapper exists: the driver's own thermal curve is
the only policy shipped and tested for that card, and a policy that cannot read
cannot decide anything. The alternative — hold the last value written — leaves a
card at whatever PWM it was last told while the service reports *started*, which
is precisely the failure the previous version had: `nvidia-smi -f` behind
`|| true` with stderr pointed at `/dev/null`, green on every card that ignored
the write.

## The version matrix

Read off a host, not off a document. Every row is a command that has been run
somewhere, with the source that documents it:

| What | Command | Documented by |
|---|---|---|
| A CUDA-capable GPU is present | `lspci \| grep -i nvidia` | [CUDA Installation Guide](https://docs.nvidia.com/cuda/cuda-installation-guide-linux/) §3.1 |
| Driver version, per GPU | `nvidia-smi --query-gpu=driver_version --format=csv,noheader` | the [datacenter driver guide](https://docs.nvidia.com/datacenter/tesla/driver-installation-guide/index.html) verifies its install with `nvidia-smi` |
| Driver version, without the tool | `cat /proc/driver/nvidia/version` | the driver's own procfs entry — useful exactly when `nvidia-smi` is the thing that is broken |
| Toolkit | `nvcc --version` | CUDA Installation Guide |
| What the venv actually sees | `python -c "import torch; print(torch.__version__, torch.version.cuda, torch.backends.cudnn.version(), torch.cuda.nccl.version())"` | PyTorch's own API — the only version numbers that describe a training run |
| Topology, read rather than assumed | `nvidia-smi topo -m` | the driver; see [MULTI_GPU_SETUP.md](MULTI_GPU_SETUP.md) for what the symbols mean |

What makes a combination legal, in the sources' own terms:

- Driver installation is the driver guide's job. The CUDA Installation Guide
  says so directly: *"Instructions for installing NVIDIA Drivers are now in the
  Driver installation guide."* A toolkit document that no longer carries driver
  instructions is not a source for them.
- **Minor Version Compatibility** (CUDA 11 and later) allows applications built
  within the same major CUDA family to run on a sufficiently new driver, with
  some feature limitations — [CUDA Compatibility](https://docs.nvidia.com/deploy/cuda-compatibility/index.html).
- **Forward Compatibility** uses the `cuda-compat-<major>-<minor>` package to run
  applications built with a newer toolkit on an older base driver, subject to
  platform and GPU support — same source.
- **NGC releases are a coherence reference, not something this machine runs.**
  The project runs no containers ([PHILOSOPHY.md](PHILOSOPHY.md)), but an NGC
  PyTorch release is NVIDIA's statement that a specific torch, cuDNN, NCCL and
  CUDA work together. Read the pairing from the
  [Frameworks Support Matrix](https://docs.nvidia.com/deeplearning/frameworks/support-matrix/index.html):
  26.09 is based on CUDA 13.4.1, 26.05 and 26.04 on 13.2.1, 25.11 and 25.10 on
  13.0.2. The "CUDA 13.2+" this project states in `HARDWARE_REQUIREMENTS.md`
  sits on the 26.04/26.05 row.
- **What this matrix does not contain is a driver version.** The hardware table's
  driver column is `unknown` until the build records it
  (`honest-scope-and-config-generation` D15), and a driver version written here
  would be that same unverified claim from the other side.
