# Boot Parameters

What the kernel command line sets on BareCortex, and how to check the
running system without rebooting.

## Observing the running system (no reboot)

```bash
# The command line the kernel actually started with
cat /proc/cmdline

# Whether the IOMMU is active in this boot, and in what mode
dmesg | grep -i iommu
```

`/proc/cmdline` is the ground truth for parameters — the bootloader config
on disk is a claim until the machine has booted with it. On Intel, an
active IOMMU shows as `DMAR: IOMMU enabled` (plus `Intel-IOMMU` lines); on
AMD, `AMD-Vi: ...`. Absence of those lines means the IOMMU is not active in
this boot, whatever the firmware or config says.

## IOMMU and `iommu=pt`

**Default: do not set `iommu=pt`.** Add it only for an observed failure
(see Troubleshooting below).

Whether the IOMMU is *active* is not a kernel-config decision.
`CONFIG_INTEL_IOMMU=y` / `CONFIG_AMD_IOMMU=y` build the driver; activation
comes from firmware, and the base config leaves
`CONFIG_INTEL_IOMMU_DEFAULT_ON` unset, so the default behaviour is "follow
the firmware". Check what this machine does with the command above rather
than assuming a default — the claim that most server BIOSes default the
IOMMU one way or the other is a frequency claim with no measurement behind
it.

The three possible states, and what `iommu=pt` means in each:

| State | How to tell | `iommu=pt` |
|---|---|---|
| No IOMMU active | no IOMMU lines in `dmesg` | no effect — there is no IOMMU to put in passthrough |
| IOMMU active, without `pt` | IOMMU enabled lines, `pt` not in `/proc/cmdline` | devices bypass translation for DMA; this is the change that can fix GPU P2P |
| IOMMU active, with `pt` | IOMMU enabled lines, `pt` in `/proc/cmdline` | already in this state; the parameter adds nothing |

## GPU peer-to-peer

Whether P2P is active is observed, not configured:

```bash
# Link types between the GPUs (NV#, PIX, PXB, PHB, SYS)
nvidia-smi topo -m

# P2P availability matrix: OK / ERR / UNS per pair — an availability
# verdict, not a bandwidth number
nvidia-smi topo -p2p r

# And the runnable check: an NCCL allreduce across all visible GPUs
sh scripts/tests/nccl_p2p_test.sh
```

`topo -p2p r` reports *availability* (`OK`, `ERR`, `UNS`), not bandwidth.
Link types such as `NV18` or `SYS` come from `topo -m`. Neither command
measures throughput, so a trigger condition phrased as "degraded bandwidth"
cannot come from either of them — the runnable allreduce is the check.

Interpreting the pair of outputs:

| `topo -m` link | `topo -p2p r` | IOMMU active | Reading |
|---|---|---|---|
| `NV#` | `OK` | either | P2P works over NVLink |
| `NV#` | `ERR` | yes | canonical failure: the IOMMU is blocking P2P — the troubleshooting case below |
| `NV#` | `ERR` | no | P2P refused without an IOMMU involved: driver/firmware, check `dmesg` for the driver's reason |
| `NV#` | `UNS` | either | the driver or hardware does not support P2P for this pair; no command-line change makes it |
| `PIX` / `PXB` / `PHB` / `SYS` | `OK` | either | PCIe P2P works (driver-dependent) |
| `PIX` / `PXB` / `PHB` / `SYS` | `ERR` / `UNS` | either | PCIe P2P unavailable: common on many platforms (and expected on GeForce); NCCL falls back to host-staged copies — measure before assuming a loss |

Every combination of link type, verdict, and IOMMU state matches exactly one
row; if a pair matches none, the `nvidia-smi` output has changed shape and
the table, not the machine, needs updating.

## Troubleshooting: P2P fails with the IOMMU active

1. Confirm the state: `dmesg | grep -i iommu` shows the IOMMU active and
   `nvidia-smi topo -p2p r` shows `ERR` for the pair (the table row above).
2. Add `iommu=pt` to the kernel command line (GRUB: edit
   `GRUB_CMDLINE_LINUX_DEFAULT` in `/etc/default/grub`, then
   `grub-mkconfig -o /boot/grub/grub.cfg`), reboot.
3. Re-run the two `nvidia-smi` commands and `scripts/tests/nccl_p2p_test.sh`.
   The parameter is a fix for this observed failure, not a default — if the
   machine has no active IOMMU, it changes nothing and should not be there.

## Huge pages

If a workload is measured to need huge pages, the size comes from that
measurement, not from a constant in this document. Size the pool to the
measured requirement and record it next to the bootloader entry, e.g.
`hugepages=<N>` where `<N>` is the number of 2 MiB pages the measurement
asked for. A bare reservation with no measurement behind it is memory the
system cannot give back.
