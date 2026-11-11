# The init layer

What runs as PID 1, what replaces each artifact an operator arriving from the
systemd edition would look for, and the commands that verify both claims. Every
row cites the page of the pinned edition
(`LFS 13.0-openrc`, <https://www.linuxfromscratch.org/~zeckma/openrc/lfs/13.0/>)
that says it; nothing here is remembered from an older release.

## PID 1

`openrc-init`, from OpenRC-0.63, built in `chapter08/openrc.html` (§8.84) and
linked into place by the book's own command:

```console
for i in {init,shutdown}; do ln -sv openrc-$i /sbin/$i done
```

So `/sbin/init` is a symlink and PID 1 is `openrc-init`. The edition installs no
SysVinit package, no `/etc/inittab` and no `/etc/rc.d` scripts — the strings
`sysvinit`, `inittab` and `rcS` appear in none of its 161 pages — and it installs
no systemd. Choosing OpenRC is not choosing no init: the runlevels below are the
service model, and `/sbin/poweroff` and `/sbin/reboot` are two two-line scripts
the same page writes by hand.

Identify PID 1 on a running host:

```console
readlink /proc/1/exe      # -> /usr/bin/openrc-init
ps -o comm= -p 1          # -> openrc-init
rc-status                 # the runlevel in effect and what it started
```

The first two name the program; the third is the one an operator actually wants
when a service did not come up. All three are read-only.

## The runlevel set

`chapter09/usage.html` §9.5.1, quoted in the book's own words:

| Runlevel | What the book says it does |
|---|---|
| `sysinit` | brings up and mounts kernel virtual filesystems |
| `boot` | starts up important services like setting the time and checks the filesystem |
| `nonetwork` | default without networking |
| `default` | where most services should be, like setting up audio services |
| `shutdown` | kills all processes and remounts `/` in read-only mode |

Services carry dependency information (`depend()` in an `openrc-run` file), so
ordering comes from the declarations rather than from numbered `/etc/rcN.d`
links. That is the substitution for SysV sequence numbers, and it is why no
`rcS` exists to port.

## Substitution table

| Artifact an operator looks for | Ships? | What replaces it | Upstream that documents the replacement |
|---|---|---|---|
| `systemd` as PID 1 | no | OpenRC-0.63; `/sbin/init` → `openrc-init` (`chapter08/openrc.html`) | OpenRC — <https://github.com/openrc/openrc> and its wiki |
| `systemctl` | no | `rc-service` (run one), `rc-update` (enable at a runlevel), `rc-status` (inspect), `openrc` (enter a runlevel) | the man pages that build installs |
| `journalctl`, the journal | no | no journal at all: Sysklogd-2.7.2 (`chapter08/sysklogd.html`) writes `/var/log/*` | sysklogd upstream; the book's logging page for the file layout |
| `systemd-networkd` | no | OpenRC's `network` service, configured by `/etc/conf.d/network` (`chapter09/network.html` §9.4.1) | OpenRC |
| `logind`, seat and session management | no | nothing replaces it, and no substitute is claimed: consoles are `agetty` from util-linux, enabled per tty (`chapter09/usage.html` §9.5.2) | none — this is an absence, and the udev build's `-D logind=false` is the same absence taken at build time |
| `*.service` unit files | no | `/etc/init.d/<name>` `openrc-run` scripts, with configuration in `/etc/conf.d/<name>` | OpenRC's `openrc-run(8)` |
| udev | **yes — from the systemd tarball** | `chapter08/udev.html` builds udev out of `systemd-259.1.tar.xz` | the systemd tarball, udev subset |
| `systemd-tmpfiles`, `hwdb` tooling | partly | `systemd-hwdb` is installed as `/usr/bin/udev-hwdb` by the same page; nothing else from the tarball is installed | the systemd tarball |

## The one true wording of the claim

**No systemd as init or service manager, and no `logind`.**

Not "no systemd source is compiled": the pinned edition builds udev from
`systemd-259.1.tar.xz` on purpose, and installs a binary named `systemd-hwdb`
under the name `udev-hwdb`. What makes the claim true is a build option, recorded
in the page that needs it:

- `-D link-udev-shared=false` — "prevents udev from linking to the internal
  systemd shared library, `libsystemd-shared`".
- `-D logind=false -D vconsole=false` — stops udev rules for components that are
  not installed.

`scripts/build/check-no-systemd.py --root <image>` is the command behind the
claim: it reads the dynamic section of every ELF file in the image, plus
`etc/pam.d/*` and `etc/nsswitch.conf`, and fails naming the file and the library
for any runtime resolution of `libsystemd*`, `libelogind*`, `libnss_systemd*` or
`pam_systemd*`. A hit on a udev binary is not an exception to add — it means the
static-link assumption above broke, and the fix is that option.

## What the edition hand-writes, which is not a substitution

Three things the book does not get from any package, all of them load-bearing:

1. **`/etc/init.d/udevd` and `/etc/init.d/udev-trigger`** (§9.5.3), enabled at
   `sysinit`. The page's reason: "OpenRC doesn't ship services for `udevd`,
   leading to kernel modules not being loaded." Device-node and module work in
   this project therefore sits behind these two files existing.
2. **`agetty.tty1` … `agetty.tty6`** (§9.5.2): OpenRC ships one `agetty` service
   and the book symlinks it per console and enables each at `default`, after
   deleting what the package installed at that runlevel.
3. **`/sbin/poweroff` and `/sbin/reboot`** (`chapter08/openrc.html`), two-line
   wrappers over `openrc-shutdown`.

Configuration files the operator is told to edit, not write:
`/etc/conf.d/hwclock`, `/etc/conf.d/consolefont`, `/etc/conf.d/keymaps`,
`/etc/conf.d/agetty`, `/etc/conf.d/network`, `/etc/rc.conf` (§9.4–§9.5.8).

## The delta no book covers

Everything above is the edition's work, citable to a page. What this repository
adds beyond the edition is three services and three configuration files, and the
substitution table's last column has one honest answer for all of them: **none —
this repository is the upstream.** The pinned edition's 161 pages contain no
NVIDIA service, OpenRC ships none, and NVIDIA's own documentation and packages
assume systemd units that this system does not have.

| File | What it is the only source of | What breaks if it stops being maintained |
|---|---|---|
| `openrc/init.d/nvidia-persistenced`, `conf.d/nvidia-persistenced` | starting the driver's persistence daemon with the right user and pidfile, and loading the modules first | the binary's path and options move with the driver branch; OpenRC still reports **started** as long as the command exits 0, so the service can be doing nothing for months and the symptom shows up as a driver that has to be re-initialised between jobs |
| `openrc/init.d/gpu-fancontrol`, `conf.d/gpu-fancontrol` | the boot-time fan write, delegated to `bmc-fan` because `nvidia-smi -f` is unsupported on most GeForce cards | the wrapper outlives the binary it wraps — `bmc-fan` is a `candidate` in `utils/rust-coverage.toml`, installed nowhere — so today the service cannot succeed on a real host. The unmaintained failure is thermal throttling that reads as a slow training run |
| `openrc/init.d/ai-training`, `conf.d/ai-training` | running a training script as a boot service, with `CUDA_VISIBLE_DEVICES` exported from `GPU_IDS` | it is the one service that executes user code, so every path in it is a guess about a machine that does not exist yet: `PYTHON_BIN=/opt/ai/venv/bin/python` predates the `uv` decision, and `GPU_IDS="0,1"` ships as the boot behaviour of a five-GPU host — the compiled-in device default that `bmc-run` was made to refuse, in a config file |

Maintenance here is not a style preference, it is the mechanism: an `openrc-run`
script that rots does not announce it, because start-success is the script's exit
status and the script cannot tell that the thing it launched is not doing its
job. That is why each of the three names its requirements, its runlevel, and its
failure mode, and why `gpu-fancontrol` delegates the one write that can lie.

## Open question the build will hit at chapter 9

The edition's own `rc-update` commands enable exactly three things:
`agetty.tty1..6` at `default`, and `udevd`/`udev-trigger` at `sysinit`. Nothing
in the 161 pages enables `localmount`, `procfs`, `sysfs`, `devfs`, `cgroups`,
`swaps`, `hostname`, `staticroute`, `sysctl`, or the `network` service the same
chapter configures — and a system without `localmount`, `procfs` and `sysfs` at
`sysinit` does not reach a shell.

Two possibilities, and the build decides which: OpenRC's `meson install` creates
the `sysinit`/`boot` runlevel symlinks itself, or this edition expects the
administrator to enable them and its own pages omit the step. The command that
answers it on a built system is `rc-update -N show`; the answer belongs in the
bootable phase's page list, and until it is known, a first boot that stops before
`/proc` exists is an expected outcome of following the book rather than a bug in
the orchestrator.
