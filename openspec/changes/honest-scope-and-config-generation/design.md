# Design: honest-scope-and-config-generation

## Context

Every claim below was checked against `torvalds/linux` tag `v6.12` on 2026-11-09. Where a claim could not be checked because it needs the physical target machine, it is marked as such. The project has no target hardware accessible yet, which constrains this change to work that is verifiable without it.

Three separate rounds of review found the same failure shape each time: an artifact that looks like a machine-checked assertion but is inert. `CONFIG_DRM_NVIDIA` (no such symbol), a verification loop whose regex never matched, a fragment line with a trailing comment, a NIC detector that matched nothing and exited 0. The design below is organised around making that shape impossible rather than around catching it by eye.

## Goals / Non-Goals

**Goals**
- Generation that cannot exit 0 on a configuration missing a requested symbol.
- A fragment format whose every line is either applied or rejected.
- Scope stated where it is violated, not in a separate document.
- NCCL configuration that reaches the process that actually trains.

**Non-Goals**
- Building or booting a kernel. No target machine exists.
- Automated PCI-to-symbol mapping. The mapping step is human and stays human.
- Benchmarks. The project measures on its own hardware or not at all.

## Decisions

### D1 — Merge without `-m`, and let the kernel's own check run

`scripts/kconfig/merge_config.sh` (v6.12) documents `-m` as *"only merge the fragments, do not execute the make command"*. Its implementation copies the merged file and returns **before** the verification loop:

```sh
if [ "$RUNMAKE" = "false" ]; then
        cp -T -- "$TMP_FILE" "$KCONFIG_CONFIG"
        echo "# merged configuration written to $KCONFIG_CONFIG (needs make)"
        exit
fi
...
for CFG in $(sed -n -e "$SED_CONFIG_EXP1" -e "$SED_CONFIG_EXP2" $TMP_FILE); do
        ...
        echo "Value requested for $CFG not in final .config"
```

So `-m` opts out of the missed-dependency check. The upstream script's own header already promises it: *"Provides warnings on overridden values, and specified values that did not make it to the resulting .config file (due to missed dependencies or config symbol removal)."*

**Decision:** call `merge_config.sh` without `-m` for the final pass, capture output, and treat `Value requested for` as a hard error. A hand-written per-symbol verification loop is not added — it duplicates the above, and the first hand-written version shipped a regex that never matched.

**The kernel's own check is not enforced by its exit status.** In v6.12 the verification loop prints three lines per unsatisfied symbol and then falls off the end of the script — no `exit 1`. `merge_config.sh` returns 0 whether or not every requested symbol survived. So the string match is the enforcement, not a supplement to it, and the script needs a comment saying so: a later cleanup that "simplifies" the check into `set -e` reliance silently removes the only thing distinguishing this change from the `-m` version it replaces.

**Consequence:** the base is resolved with `alldefconfig` rather than `olddefconfig`. Acceptable: both fill defaults for symbols the base omits, and `alldefconfig` is what the upstream tool drives.

**Not used:** `-s` (strict mode). It fails on any redefinition, and the fragment legitimately overrides `PREEMPT_VOLUNTARY` (base) with `PREEMPT_NONE`.

### D2 — Fragment grammar, and why a commented-out symbol is sabotage

The merge tool extracts requested symbols with anchored patterns:

```sh
SED_CONFIG_EXP1="s/^\(${CONFIG_PREFIX}[a-zA-Z0-9_]*\)=.*/\1/p"
SED_CONFIG_EXP2="s/^# \(${CONFIG_PREFIX}[a-zA-Z0-9_]*\) is not set$/\1/p"
```

Two consequences, both load-bearing:

1. `# CONFIG_FOO is not set  # trailing note` does **not** match `EXP2` (the `$` anchor fails). The line is inert: not applied, not verified, and it reads to a human like an assertion.
2. `# CONFIG_64BIT is not set` **does** match, and is a request to *unset* `CONFIG_64BIT`. On x86 `config 64BIT` has a prompt (`bool "64-bit kernel" if "$(ARCH)" = "x86"`), so it is settable and kconfig will try to honour it. Writing "this is a no-op, already implied" as a commented-out line asks for a 32-bit kernel.

**Decision:** fragments contain only the two forms, one per line, no trailing text. Removal is expressed by deleting the line; rationale goes in the change proposal. Generation rejects malformed lines before merging, so the grammar is enforced rather than recommended.

### D3 — `x86_64_defconfig` cannot be trusted as a base, and the fragment cannot fix it

`arch/x86/configs/x86_64_defconfig` (v6.12, 279 lines) sets `CONFIG_NETDEVICES=y` and provides exactly one network driver:

```
CONFIG_VIRTIO_NET=y     CONFIG_DRM_VIRTIO_GPU=y     CONFIG_DRM_I915=y
```

No IGB, IXGBE, I40E, ICE, MLX5, BNXT, SFC, or TG3. It is a KVM guest config. A build from base plus an AI-workload fragment therefore produces a kernel with no driver for any physical server NIC, and — because the merge tool only reports symbols that were *requested* and did not survive — it reports success.

**Decision:** a second, **mandatory** fragment derived from the target machine. Generation fails if it is absent or if it names no network driver. It cannot be committed with real contents (it is per-machine), so it must be a required argument or a required path with no default — a `WARN` and `continue` on a missing fragment reproduces the exact failure this change exists to remove, on a fresh checkout, every time.

**Rejected:** enumerating common NIC drivers speculatively in the shipped fragment. That is a guess with a `=y` on it, which is the failure mode we are removing, and it grows the kernel for hardware nobody has.

### D4 — Keep `CONFIG_CPU_FREQ`; set the governor in the init system

`# CONFIG_CPU_FREQ is not set` was proposed to get predictable clocks. Two problems:

- `CPU_THERMAL` depends on `CPU_FREQ_THERMAL`, which depends on `CPU_FREQ`. Turning cpufreq off removes the CPU frequency cooling device from the thermal framework. On a chassis shared with several GPUs that is a thermal-response regression, not a micro-optimisation.
- It is the wrong layer. The base sets `CONFIG_CPU_FREQ_DEFAULT_GOV_USERSPACE=y`, so nothing drives the clocks at boot — which is why the box idles low. The fix is a governor, and the project already claims "Custom OpenRC services".

**Decision:** leave `CONFIG_CPU_FREQ` at its base value; add an OpenRC `cpufreq` service pinned to `performance`, configured in `openrc/conf.d/`. If the governor must be baked in instead, `CONFIG_CPU_FREQ_DEFAULT_GOV_PERFORMANCE=y` is the one-line alternative.

### D5 — EDAC: the proposed symbol does not exist

`CONFIG_EDAC_MM_EDAC` appears **zero** times in `drivers/edac/Kconfig` at v6.12; it was folded into the EDAC core years ago. The real shape:

```
menuconfig EDAC
        tristate "EDAC (Error Detection And Correction) reporting"
        depends on HAS_IOMEM && EDAC_SUPPORT && RAS
```

Drivers are per memory controller (`EDAC_AMD64`, `EDAC_I7300_EDAC`, `EDAC_SKYLAKE`, …) and cannot be chosen without the target. Note `tristate`: `=y` is valid, `=m` is the more usual choice.

**Decision (revised):** EDAC moves **entirely** to the target fragment. Core without a controller driver reports nothing, and the driver is fixed by the target's memory controller. The README's 24/7-stability claim stays qualitative until that fragment exists — an enabled-but-driverless EDAC is another line that reads like coverage and provides none.

### D6 — SysRq and panic timeout need their companion options

Both proposed symbols exist, and both are inert at their defaults:

```
config MAGIC_SYSRQ_DEFAULT_ENABLE
        hex "Enable magic SysRq key functions by default"
        depends on MAGIC_SYSRQ
        default 0x1
```

`PANIC_TIMEOUT` defaults to `0` (wait forever), so `=10` is a real change — but panic-then-reboot with no persistent record means the machine comes back and the cause is gone. That is the worst quadrant for a node whose selling point is 72-hour runs.

**Correction, recorded because it is the defect this whole change is about.** An earlier revision of this document, and the proposal it reviewed, both stated that `0x1` "enables only the log-level key". That is false, and it was repeated from the proposal without being checked against the source:

```c
/* drivers/tty/sysrq.c, v6.12
 * A value of 1 means 'all', other nonzero values are an op mask:
 */
static bool sysrq_on_mask(int mask)
{
        return sysrq_always_enabled ||
               sysrq_enabled == 1 ||
               (sysrq_enabled & mask);
}
```

`Documentation/admin-guide/sysrq.rst` agrees: `1 - enable all functions of sysrq`, `>1 - bitmask of allowed sysrq functions`, with the function bits starting at `0x2`. So the default already means *everything*, and `MAGIC_SYSRQ=y` was never the near-no-op it was described as.

**Decision (revised twice):** `CONFIG_MAGIC_SYSRQ=y` and **no `MAGIC_SYSRQ_DEFAULT_ENABLE` line at all** — the `0x1` default is the full set, so an explicit value would only ever narrow it. The proposed `0x1ff` is a bitmask (`>1`), which covers the currently documented bits `0x2`–`0x100` and silently drops any function bit a later kernel adds; `1` inherits them for free. If a restricted mask is ever wanted, name the bits and the reason in a comment.

`PSTORE` cannot be fixed here either. Its own help text is explicit:

```
config PSTORE
        tristate "Persistent store support"
        ...Only useful if you have a platform level driver that registers
        with pstore to provide the data ... If you don't have a platform
        persistent store driver, say N.
```

`PSTORE=y` plus `PSTORE_CONSOLE=y` with no backend mounts an empty filesystem and records nothing — the same inert assertion again. The backend on x86_64 servers is firmware-dependent (ACPI APEI/ERST, EFI, or ramoops with a reserved region), so it belongs in the target fragment. Until then `PANIC_TIMEOUT` stays `0`: rebooting with no surviving record is worse than waiting for someone to read the screen.

### D7 — Drop GPU passthrough from scope

`VFIO`, `VFIO_PCI`, `PCI_IOV`, `PCI_PASID` were carried over from a passthrough idea nothing else in the repository mentions, while the stated scope is "single physical server, Python training, no containers". They also argue with the boot documentation: `iommu=pt` is only interesting when the IOMMU is on, and the IOMMU is only interesting when something needs translation — yet the boot doc must then allow that the IOMMU is off entirely.

`PCI_PASID` was earlier flagged as needing an explicit `PCI_ATS`; that was wrong — `config PCI_PASID` carries `select PCI_ATS`, so it self-satisfies. It is removed here for scope, not correctness.

**Decision (OQ1 resolved: out of scope):** remove `VFIO`, `VFIO_PCI`, `PCI_IOV`, `PCI_PASID`. Nothing in the repository mentions VMs, and passthrough is the maximum abstraction in a project whose thesis is "zero abstraction between your code and the GPU". What the boot documentation should then say about `iommu=pt` is D12.

**`IRQ_REMAP` is not removed.** It was proposed for deletion as "isolation for passthrough", which is not what it is. It is an interrupt-delivery feature, and dropping it costs x2APIC outright:

```
config X86_X2APIC
        bool "Support x2apic"
        depends on X86_LOCAL_APIC && X86_64 && (IRQ_REMAP || HYPERVISOR_GUEST)

config IRQ_REMAP
        bool "Support for Interrupt Remapping"
        ...To use x2apic mode in the CPU's which support x2APIC enhancements
        or to support platforms with CPU's having > 8 bit APIC ID, say Y.
```

On bare metal `HYPERVISOR_GUEST` is off, so `IRQ_REMAP` is the only way `X86_X2APIC` becomes selectable — on a many-core server, which is the entire target class. Keeping `INTEL_IOMMU`/`AMD_IOMMU` enabled while deleting `IRQ_REMAP` is also the inconsistent combination: the IOMMU is what supplies remapping at runtime, and it is what makes `iommu=pt` necessary in the first place. `IRQ_REMAP` stays.

The keeping comment must state the dependency, not a deletion criterion. A comment reading "required for x2APIC on systems with >255 logical CPUs" invites removal the moment someone counts their cores and gets 64 — while the Kconfig dependency is unconditional and has nothing to do with CPU count. Say what depends on it and where.

### D12 — IOMMU: stop pinning it, and make `iommu=pt` a diagnosis

OQ5 resolved: the fragment does not force the IOMMU either way; the boot documentation stops presenting `iommu=pt` as a configuration choice and presents it as a fix for an observed failure.

**The reason matters more than the change.** `CONFIG_INTEL_IOMMU=y` and `CONFIG_AMD_IOMMU=y` build a *driver*; whether the IOMMU is *active* is decided by firmware and by `CONFIG_INTEL_IOMMU_DEFAULT_ON`, which `x86_64_defconfig` sets `is not set`. So the base already does "follow the BIOS", and the fragment's three IOMMU lines were redundant pins, not coercion. Delete them as redundant pins — describing it as "removing forced enablement" would be false and would get the same wrong reasoning applied to another symbol.

**`IRQ_REMAP` is unaffected.** It is compiled support; it is inert at runtime when the IOMMU is off, and it is what `X86_X2APIC` depends on regardless.

Two claims in the proposed replacement text must not ship:

- *"Most server BIOS default to IOMMU disabled or handle P2P correctly"* is an unsourced frequency claim — precisely what the `docs-scope` requirement forbids. Replace it with the command that checks this machine: `dmesg | grep -i iommu`.
- *"Add it if `nvidia-smi topo -p2p r` shows ... degraded bandwidth"* is unactionable. `topo -p2p` prints an availability matrix of `OK`/`ERR`/`UNS`; link types such as `NV18` or `SYS` come from `topo -m`, and neither measures bandwidth. A trigger condition the named command cannot produce is a diagnostic nobody can follow.

Reformatting the conditional table into a troubleshooting section is not a behaviour change: the section still states the three system states (no IOMMU, IOMMU with `pt`, IOMMU without `pt`), which is what the `boot-parameters` requirement asks for. Describe it as a change of emphasis.

### D8 — NCCL: `/etc/profile.d/` does not reach the service

`openrc/init.d/ai-training` runs `start-stop-daemon --exec ${command}` from `start_pre()`, where it currently does `export NCCL_DEBUG=INFO`. An OpenRC service is not a login shell, so `/etc/profile.d/nccl.sh` is never sourced by it. Consolidating into `/etc/profile.d/` while leaving the init script as-is would leave three definitions and silently give the service a different environment from an interactive shell.

**Decision (revised):** the single source is `/etc/conf.d/nccl`, not `/etc/profile.d/nccl.sh`. OpenRC auto-sources only `conf.d` for the *service's own name*, so a shared file must be sourced explicitly either way, and `conf.d` is the conventional home for service configuration. Three consumers: the init script sources it in `start_pre()`, the deploy script sources it, and `/etc/profile.d/nccl.sh` becomes a one-line shim for interactive shells. The inline `export NCCL_DEBUG=INFO` in `start_pre()` is deleted, and `setup_cuda.sh` writes the `conf.d` file.

**Load-bearing caveat:** `conf.d` files conventionally assign without exporting, and OpenRC sources them into the init script's own shell — a non-exported variable never enters the environment `start-stop-daemon` hands to the training process. The file must use `export`, with a comment explaining why it deviates from `conf.d` convention, or the consumer must `set -a; source …; set +a`. Without this the consolidation is invisible to the one process it was written for.

### D9 — Split the change at the hardware boundary

The original task list began with "collect `lspci -nn` from the target machine" and ended with "boot and validate NCCL P2P" — and no target machine is accessible. A change whose first task is blocked indefinitely is not a plan; it is how change tracking dies.

**Decision:** this change contains only work verifiable without hardware. A follow-up change (`target-hardware bring-up`) owns NIC enumeration, kernel build, boot, P2P and NCCL validation, and the acceptance criteria that depend on them.

## Risks / Trade-offs

- **`alldefconfig` may enable symbols the minimal philosophy rejects.** Mitigation: the fragment is a delta and the diff against base is reviewed; `merge_config.sh -r` lists redundant entries.
- **A mandatory target fragment makes fresh checkouts fail.** Intentional. The alternative is a kernel with no network that reports success.
- **Everything here is unverified against a real build.** No kernel source is present on the working machine and no target exists. The verification added by D1 is the mechanism that will find what this design got wrong.
- **Kernel version drift.** Symbols are pinned to v6.12 by inspection; a version bump can rename or remove them. Mitigation: the pin is recorded in the fragment and D1 fails generation on the next run.

### D10 — Kernel version is an audited-version marker, and the gate must not be silently editable

OQ3 resolved: v6.12 is a pin, gated on `PATCHLEVEL` with `SUBLEVEL` free, because point releases do not change Kconfig.

The proposed gate compares `make -s kernelversion` against a `REQUIRED_KSERIES` constant and exits 1. The mechanism is right to exist — symbol *existence* is already caught by D1 on any tree, so the gate's only real job is the thing D1 cannot see: a symbol that still exists but changed meaning or default. But as a hard fail on an editable constant it enforces nothing. Under a deadline the operator edits the constant, and the gate's only measurable effect is teaching that constants are there to be bumped.

**Decision:** keep the check, make the bypass loud. Mismatch prints the audited version, the actual version, and the re-audit procedure, then fails — and is overridable only by an explicit `--allow-unaudited-kernel` flag, never by editing a file. A flag in a shell history line is greppable; a changed constant is indistinguishable from a fix.

### D11 — Success must be conditioned on having done the thing

Six instances of one defect surfaced across this review, in different people's code:

| Artifact | Success path | What it actually did |
|---|---|---|
| `merge_config.sh -m` | exit 0 | skipped the verification loop |
| hand-written symbol loop | checked "is not set" lines | regex required literal quotes; never matched |
| fragment lines with trailing text | asserted a decision | inert against the anchored pattern |
| NIC detector | exit 0, printed the banner | filter never matched `lspci -nn` output |
| `eend 0` in the cpufreq service | "governor set" | set nothing if no `scaling_governor` was writable |
| `WARN` + `continue` on missing target fragment | config emitted | kernel with no network driver |

**Decision:** a standing convention for every script this project ships — report success only after counting the work done, and fail at zero. In the cpufreq service that means writing the governor to each CPU, counting successes, and `eend 1` when the count is zero (which is also the correct answer when `CONFIG_CPU_FREQ` is off or `intel_pstate` exposes no governor). Counting *attempts* is not counting work: `-w` tests permission bits, not whether the write succeeded, and an unchecked `echo` exit status lets a rejected governor inflate the count. Verify the write — check the status, or read `scaling_governor` back. Note also that `eend`'s optional message belongs to the failure line, so a success count printed there will not be seen; use `einfo`.

The convention is recorded here rather than in a spec because it constrains implementation, not observable behaviour.

### D13 — Base book and init: the published OpenRC edition, pinned by revision

OQ6 resolved: OpenRC, systemd not used, GPU stack from NVIDIA's own documentation.

This has a consequence the documentation currently hides, though not the one first recorded here. **Correction, 2026-11-08:** an OpenRC edition of LFS does exist — `linuxfromscratch.org/~zeckma/openrc/lfs/`, `Version 13.0-openrc`, OpenRC built in `chapter08/openrc.html` — published outside the main view index, which is why probing `/lfs/view/stable-openrc/` returned 404 and was read as absence. What the probe did establish still holds and is the actual constraint: `lfs/view/stable-openrc/` and `blfs/view/stable-openrc/` are dead links, and **LFS 13.x publishes no SysVinit edition at all** (`13.0/`, `13.1/` → 404; only `-systemd`), while BLFS's OpenRC material is an SVN snapshot with no release. So the base is the published OpenRC edition, pinned by revision, and the honest documentation problem is that the book is not findable where every reader will look.

Concretely, the base is the OpenRC edition, and the delta this project maintains is the pinning of it plus whatever diverges from the book it patches. OpenRC still needs an init supervisor — the edition keeps sysvinit's `/sbin/init` as PID 1 and invokes `/sbin/openrc` per runlevel — so "no systemd" must never be read as "no init". Details live in `base-system-and-init`.

The second consequence is that **"No systemd" is currently an assertion, not a check**. BLFS packages probe for `libsystemd` and enable integration by default (`--with-systemd`, meson `systemd=auto`), so the failure mode is silent: an OpenRC system whose binaries link `libsystemd.so.0`, with a README badge that stays true. The verification is one command over the built image — `ldd` across `/usr/bin`, `/usr/sbin`, `/lib`, grepped for `libsystemd|libelogind|pam_systemd|libnss_systemd` — and it belongs in the build, not in a badge.

The GPU stack moves to NVIDIA's own documentation, all four of which resolve: the datacenter driver installation guide, the CUDA Installation Guide for Linux, the CUDA Compatibility guide, and OpenRC's own repository for the init layer. `GLFS` stays out: it is *Gaming* Linux From Scratch (Steam, Wine), and citing it for CUDA was a live link with a false claim about its contents.

### D14 — The merge check verifies presence, not value, and the audited kernel is now visibly old

Two facts from checking the world rather than the notes.

**The check is name-only.** `merge_config.sh` extracts symbols with `s/^\(${CONFIG_PREFIX}[a-zA-Z0-9_]*\)=.*/\1/p` — the value is discarded by the regex before the comparison runs. So the loop that prints `Value requested for $CFG not in final .config` answers "did this symbol survive?", never "did it survive *as asked*?" A fragment line carrying an invalid value for its symbol type therefore passes the only enforcement D1 has, silently.

The live example came from an external plan for the same machine: `CONFIG_TRANSPARENT_HUGEPAGE=madvise`. In v6.12 `TRANSPARENT_HUGEPAGE` is a `menuconfig bool`; the mode is a separate choice symbol, `TRANSPARENT_HUGEPAGE_MADVISE=y`. The requested line leaves `CONFIG_TRANSPARENT_HUGEPAGE=y` in the final `.config`, the check finds the symbol, nothing is printed, and the mode that was asked for is not set. Same class as the six instances in D11, one level further up: an artifact that reads like a machine-checked assertion and enforces nothing.

**Decision:** the generator asserts **values**, not presence — for every line in the fragment, read the symbol out of the final `.config` and compare the value, including the `is not set` case, and fail listing symbol, requested value, actual value. The `merge_config.sh` loop stays as a second, weaker net and is commented as such.

**The pin is behind, and that is what the gate is for.** As of 2026-11-08 kernel.org lists mainline 7.4-rc1 and stable 7.3.3, with 6.12 still maintained (6.12.116). The base book this project now builds from ships Linux 7.1.8. The audit in this change was done against v6.12, so it is not invalidated — 6.12 is a supported series — but every future build hits the D10 gate, and the flag path stops being hypothetical. Re-auditing the fragment against the kernel the base book actually ships is a task, not a note, and it is the task that keeps D10 from becoming the thing that gets bypassed on the first bad day.

### D15 — What the external plan does better, and what it must not import

An external 14-cycle plan for the same machine was reviewed against these decisions. Adopted: a single pinned version file as the only source of version truth; a per-variable evidence ledger for NCCL tuning (no variable without a row, kept only if the measured baseline improves); a machine-readable keep-list for language decisions; storage-controller symbols in the mandatory target fragment alongside NIC drivers, because an unbootable kernel and a networkless one cost the same; `/dev/cpu_dma_latency` as the C-state mechanism instead of a BIOS setting. Its `pam_limits` mechanism for `memlock` is **rejected**: the pinned base edition has no PAM at all — the string does not occur in its package list and OpenRC itself is built `-D pam=false` — so the limit is set where OpenRC actually reads it, in the service's `rc_ulimit` (or a cgroup v2 limit for the training job), and the boot parameter `memlock=` is not a thing to be configured through a login stack that does not exist.

Rejected with reasons recorded here so the argument is not re-run: `# CONFIG_DRM is not set` in the same plan that installs `nvidia-drm.ko`, which cannot build without DRM; the invalid THP line above; a hugepage pool sized as a fraction of RAM, which reserves memory no one measured as needed and cannot give back — the same unsourced number this change removed, now wearing a formula; `LD_LIBRARY_PATH` in `profile.d` as the exposure mechanism for anything a service loads, which is D8's defect re-imported; and VFIO, still awaiting the VM requirement that D7 asked for.

## Open Questions

- **OQ1 — Resolved: passthrough out of scope.** See D7.
- **OQ3 — Resolved: v6.12 pinned, `PATCHLEVEL`-gated.** See D10.
- **OQ6 — Resolved: OpenRC, base is the published LFS OpenRC edition (corrected — see D13), GPU stack from NVIDIA documentation.** See D13; the base-system specification is the separate `base-system-and-init` change.
- **OQ2 — What is the target machine?** Blocks the follow-up change entirely: NIC drivers, EDAC controller driver, `NR_CPUS`, pstore backend.
- **OQ4 — Which pstore backend does the target's firmware offer** (APEI/ERST, EFI, or neither)? Until answered, `PANIC_TIMEOUT` stays `0` per D6.
- **OQ5 — Resolved: do not pin the IOMMU; `iommu=pt` becomes a troubleshooting fix.** See D12.
