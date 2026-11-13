# Design: alpine-as-base

## Context

The base is decided: LFS 13.0-openrc, OpenRC 0.63 built at §8.84, PID 1 is
`openrc-init` (`docs/INIT-LAYER.md`, pinned in `openspec/project.md`). This change
exists because a competing sentence keeps appearing in review — take the init
layer from Alpine instead — and the repository's own rule is that an assertion
with no command behind it is not a requirement. So the sentence was measured
rather than argued with, and this design records what the measurement does and
does not license.

What was measured, in one line: Alpine v3.24's `alpine-minirootfs-3.24.4-x86_64`
(sha256 `cdaa38cb…3012`, 8 431 459 B unpacked, 16 packages) plus the closure of
`openrc busybox musl` — 7 packages, 1 168 765 B — with the graph produced by
musl's own loader over all 84 executables in the assembled root. Full tables in
`docs/ALPINE-AS-BASE.md`; raw output in `evidence/`.

## Goals / Non-Goals

**Goals.** Make the alternative's cost and shape reproducible. Pin the provider
choices Alpine's index leaves open. Give "we measured it" a meaning in this
repository: a committed script, a published checksum, a rerun.

**Non-Goals.** Not switching the base. Not benchmarking boot time or RSS against
the LFS build — neither host is idle enough to measure that honestly, and a
number nobody reproduced is the defect this project was founded on correcting.
Not adopting apk, and not deciding `image-minimisation`'s ownership question,
though this change hands it a working example.

## Decisions

### D1 — The graph comes from the loader, not from package metadata

Edges are what the loader resolves, obtained with
`ld-musl-x86_64.so.1 --library-path <root>/lib:<root>/usr/lib --list <prog>` over
every ELF object in the assembled root. On musl that command *is* `ldd`; the
loader is the same code path a real exec takes, so a soname it cannot resolve is
a binary that would not start.

Alternative considered: read `depend`/`provides` out of `APKINDEX` and draw the
graph from that. Rejected as the primary source — the index says what a package
*declares*, which is a superset of what the loader loads, and it is silent about
the `so:` binding actually being satisfiable in the root we assembled. It is used
for one thing only: resolving the closure, where it is authoritative because it
is what apk itself reads.

Consequence: the probe needs an x86_64 Linux host to run the loader. It needs no
root, no chroot and no container — the tarball is unpacked into a directory, the
apks are untarred over it, and the loader is invoked by path with an explicit
`--library-path`. That keeps the measurement reproducible on the build host
without putting a second system on it.

### D2 — apk installs them; `cp` of the three files is not a supported path

The probe fetches the closure and unpacks the apks rather than copying
`/sbin/openrc`, `bin/busybox` and `lib/ld-musl-x86_64.so.1` by hand. Copying
those three is the tempting version and it is wrong in a way that only shows at
boot: `start-stop-daemon` and `supervise-daemon` load `libcap.so.2`, which no
"just the three" list contains, and OpenRC's scripts need `/bin/sh`, which is a
separate package (`busybox-binsh`) from the `busybox` that provides the applets.

Consequence, and the reason this is a decision rather than a detail: adopting
Alpine's init layer is adopting apk as the mechanism that keeps it consistent.
That is what `build-orchestration` D1 declines. The cost of the mechanism is
visible in the same measurement — 5 837 448 B of the minirootfs's 8 431 459 B are
`libcrypto.so.3` and `libssl.so.3`, which exist so apk can speak HTTPS.

### D3 — Three virtuals are ours to decide, not apk's

`APKINDEX` resolves `/bin/sh`, `ifupdown-any` and `openrc-user` to more than one
provider, and apk's pick depends on repository order. A pinned base names the
provider, not just the version:

| virtual | providers | chosen | why |
|---|---|---|---|
| `/bin/sh` | busybox-binsh, dash-binsh, yash-binsh | busybox-binsh | the minirootfs already ships it; a second shell is a second thing to patch |
| `ifupdown-any` | busybox-ifupdown, ifupdown-ng | undecided, recorded as undecided | busybox-ifupdown is a placeholder that satisfies apk and does nothing at boot; choosing it silently would be a networking phase that appears to exist |
| `openrc-user` | openrc-user, openrc-user-pam | openrc-user | the pinned edition builds OpenRC with `-D pam=false`; a PAM-flavoured provider would contradict `docs/INIT-LAYER.md` |

The middle row stays open on purpose. Naming a provider for a virtual whose job
is networking belongs to whoever writes the networking phase, and a placeholder
package is exactly the kind of pass-that-does-nothing this project treats as a
defect.

### D4 — The measurement is an artifact, the document is its caption

`artifacts/handoff-vm/alpine-probe/` holds the two scripts and the raw output
(`ldd.json`, `closure.json`, the per-apk manifests, the minirootfs's apk db).
`docs/ALPINE-AS-BASE.md` quotes it and nothing else. When Alpine moves, the
published sha256 moves and every byte figure in the document is stale by
construction — the fix is to rerun, not to edit the table.

Alternative considered: a paragraph in `docs/INIT-LAYER.md`. Rejected — it would
be a claim about a system the repository does not build, sitting inside the
document that says what we do build, which is how a rumour becomes a spec.

## Risks / Trade-offs

**The measurement goes stale silently.** Alpine's v3.24 index moves; the closure
and every size with it. Mitigated by pinning the tarball's sha256 in the document
and by `probe.sh` printing published-against-computed checksums side by side, so
a stale figure is at least a *detected* stale figure.

**A closed graph is not a bootable system.** The probe proves the dependency
graph resolves; it does not prove OpenRC reaches multi-user on this hardware
without the LFS build's `/etc` layout, nor how it interacts with the udev built
from `systemd-259.1`. Any future change that actually adopts the alternative owes
a boot test, and this change does not pretend to have done it.

**Recording an alternative invites drift.** Someone may quote
`docs/ALPINE-AS-BASE.md` as if it were a plan. The document states in its first
line what question it answers, and its last section says what it does not say.
