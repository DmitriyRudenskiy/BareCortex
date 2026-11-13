# Alpine as the base layer

The question this answers: if BareCortex took its init layer — OpenRC, busybox,
musl — from Alpine instead of building it in LFS, what would it actually have to
take, and what would the dependency graph look like?

Every number here was measured on 2026-11-13 by
`artifacts/handoff-vm/alpine-probe/probe.sh` on the x86_64 host, against
`v3.24/main`, and the raw output is in `artifacts/handoff-vm/alpine-probe/evidence/`.
The graph is not a guess from package metadata: it is what musl's own loader
answered when asked to resolve every executable in the assembled root.

## The base we would start from

`alpine-minirootfs-3.24.4-x86_64.tar.gz`

| | |
|---|---|
| sha256 | `cdaa38cb8de3537967ee5b6c9b6cd00cc962da2b018e6eea48696bb274923012` (published == computed) |
| tarball | 2 957 672 B |
| unpacked | 8 431 459 B, 515 paths, 84 files, 17 ELF |
| packages | 16 |

Those 16 are: `alpine-baselayout`, `alpine-baselayout-data`, `alpine-keys`,
`alpine-release`, `apk-tools`, `libapk`, `busybox`, `busybox-binsh`, `musl`,
`musl-utils`, `scanelf`, `libcrypto3`, `libssl3`, `ssl_client`,
`ca-certificates-bundle`, `zlib`.

Two things follow from that list. musl and busybox — two of the three we are
after — **are already there**, along with `/bin/sh`. And 5 837 448 of the 8 431 459
bytes, 69%, are `libcrypto.so.3` (4 993 808) and `libssl.so.3` (843 640): the
minirootfs is mostly the TLS stack that `apk` needs to fetch packages over HTTPS.
The whole of musl is 670 312 B and all of busybox 804 616 B. "Alpine's minimum is
8 MB" is really "2.6 MB of usable base plus a package manager".

## What taking the three actually takes

Resolved against Alpine's own index (`resolve-apk-closure.py`, edges from the
index's `D:` and `p:` fields, including the `so:` virtuals apk binds a DT_NEEDED
to): **7 packages, 1 168 765 B of declared installed size.**

| package | size | why it is in the closure |
|---|---|---|
| busybox 1.37.0-r31 | 504 769 | already in the tarball |
| musl 1.2.6-r3 | 415 466 | already in the tarball |
| openrc 0.63.2-r0 | 217 933 | the thing we want |
| libcap2 2.78-r0 | 21 582 | `so:libcap.so.2` for start-stop-daemon |
| openrc-user 0.63.2-r0 | 6 113 | depends on openrc |
| busybox-binsh 1.37.0-r31 | 1 571 | provides `/bin/sh` |
| busybox-ifupdown 1.37.0-r31 | 1 331 | closes `ifupdown-any` |

After installing them the root is 686 paths / 9 859 629 B. **OpenRC's real price
is 171 paths and 1.43 MB**, and that includes its 41 `/etc/init.d` entries and
the shell tree under `/usr/libexec/rc`.

Note the file naming if you script the fetch: in v3.24 an apk is
`openrc-0.63.2-r0.apk`. The architecture is in the repository path, not the
filename — `openrc-0.63.2-r0-x86_64.apk` is a 404.

## The graph, as the loader answered it

`ld-musl-x86_64.so.1 --library-path <root>/lib:<root>/usr/lib --list <prog>` is
ldd on musl. Run over every executable in the assembled root: **84 ELF objects,
exit status 0 for all 84, zero `not found`.**

```
sbin/openrc · sbin/start-stop-daemon · sbin/supervise-daemon · 55 helpers in /usr/libexec/rc
├── librc.so.1     ← openrc-0.63.2-r0    ┐
├── libeinfo.so.1  ← openrc-0.63.2-r0    ├── libc.musl-x86_64.so.1 → musl-1.2.6-r3 (in tarball)
├── libcap.so.2    ← libcap2-2.78-r0  ★ the only new library in the whole graph
└── /bin/sh        ← busybox-binsh → busybox (in tarball)

usr/lib/libcap.so.2.78, usr/lib/libpsx.so.2.78 → musl only
sbin/apk (already in the tarball)
└── libapk.so.3.0.0 → libssl.so.3 → libcrypto.so.3 → musl
                    └→ libz.so.1
```

By edge count: `libc.musl-x86_64.so.1` 82 executables, `libeinfo.so.1` 59,
`librc.so.1` 38, `libcrypto.so.3` 8, `libssl.so.3` 3, `libz.so.1` 2,
`libcap.so.2` 2, `libapk.so.3.0.0` 1. OpenRC's own two libraries need nothing but
musl; the only executable pair that reaches outside openrc and musl is
`start-stop-daemon` and `supervise-daemon`, and what they reach is libcap.

The graph is shallow and it is closed. Nothing in it pulls in X, GLib, Python, or
D-Bus — which is the usual reason an init layer turns into a distro.

## Three choices the index will not make for us

apk resolves a virtual to whichever provider it likes; a pinned image has to pin
the *choice*, not just the version, or the same lockfile builds a different base:

| dependency | providers in v3.24 main | this project |
|---|---|---|
| `/bin/sh` | busybox-binsh, dash-binsh, yash-binsh | busybox-binsh — the tarball already ships it |
| `ifupdown-any` | busybox-ifupdown (a placeholder), ifupdown-ng | decide with the networking phase; a placeholder satisfies apk and does nothing at boot |
| `openrc-user` | openrc-user, openrc-user-pam | openrc-user — no PAM until there is a PAM story |

## What this does not say

It is not a proposal to move off LFS. The LFS 13.0-openrc build in
`docs/INIT-LAYER.md` produces the same three at 0.63 with every patch visible,
and it is 10 chapters from finishing. What this measures is the size of the
alternative and the shape of its graph, so that "just use Alpine's OpenRC" is a
number and a dependency list rather than a mood — and so that if we ever do take
it, the three names above are already decided instead of being decided by apk at
build time.
