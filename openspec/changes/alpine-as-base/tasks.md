# Tasks: alpine-as-base

Scope note: nothing here needs the **target** machine and nothing needs the GPU.
The measurement tasks are `(build)` — an x86_64 Linux host with a toolchain and
disk, which the physical build host satisfies; the probe runs unprivileged, so it
does not compete with the LFS build for the guest. One task is `(target)`-adjacent
by nature and is therefore not written as a task at all: whether OpenRC from
Alpine reaches multi-user on this hardware. That belongs to a change that adopts
the alternative, and this change records that it is unmeasured.

## 0. Inputs and host classes

- [x] 0.1 Name the host for every task below: `(build)`, satisfied by the x86_64
      physical host at `192.168.0.86`. The LFS guest is not used — it reboots
      between phases and its sshd is not always listening, and a measurement that
      depends on it would not be reproducible. See `docs/HANDOFF-VM.md`
- [x] 0.2 Pin the input the numbers are about: `alpine-minirootfs-3.24.4-x86_64.tar.gz`,
      sha256 `cdaa38cb8de3537967ee5b6c9b6cd00cc962da2b018e6eea48696bb274923012`,
      published == computed at measurement time. Alpine release `v3.24`, repository
      `main`, architecture `x86_64`

## 1. Measure the closure

- [x] 1.1 Write `artifacts/handoff-vm/alpine-probe/resolve-apk-closure.py`: read
      `APKINDEX`, walk `D:` and `p:` (including the `so:` virtuals), print the
      closure with declared sizes and every ambiguous dependency. Run:
      `./resolve-apk-closure.py APKINDEX out openrc busybox musl`
      → 7 packages, 1 168 765 B, three ambiguous virtuals
- [x] 1.2 Record the ambiguity apk resolves on our behalf, because a pinned base
      has to name the provider and not the version: `/bin/sh`
      (busybox-binsh, dash-binsh, yash-binsh), `ifupdown-any`
      (busybox-ifupdown, ifupdown-ng), `openrc-user` (openrc-user,
      openrc-user-pam). Two are decided in D3; `ifupdown-any` is deliberately
      left open
- [x] 1.3 Fix the apk filename rule the probe had wrong: in v3.24 an apk is
      `<name>-<version>.apk`, the architecture is in the repository path.
      `<name>-<version>-<arch>.apk` returns 404, which is how the first run of
      `probe.sh` fetched nothing and still reported a clean graph over the
      17 ELF objects of the unmodified tarball — a pass that proved nothing

## 2. Measure the graph

- [x] 2.1 Write `artifacts/handoff-vm/alpine-probe/probe.sh`: verify the tarball
      against its published sha256, unpack, install the closure by untarring the
      apks, then resolve every ELF object with
      `ld-musl-x86_64.so.1 --library-path <root>/lib:<root>/usr/lib --list`
- [x] 2.2 Run it and keep the output. Result: root grows 515 → 686 paths and
      8 431 459 → 9 859 629 B; **84 ELF objects, exit status 0 for all 84, zero
      `not found`**. Evidence: `evidence/ldd.json`, `evidence/closure.json`,
      `evidence/manifest-*.txt`, `evidence/apk-installed.minirootfs`
- [x] 2.3 Attribute the graph's nodes to packages from the apks' own manifests
      (`tar tzf <apk>`, `.git/` excluded), not from where the files happen to
      live. Result: 58 of the 84 executables belong to openrc, 2 to libcap2, 1 to
      musl; `libcap.so.2` is the only library in the graph that the tarball does
      not already have, and it is loaded by exactly two binaries
      (`start-stop-daemon`, `supervise-daemon`)
- [x] 2.4 Record what the tarball is actually made of, because it changes the
      argument: `libcrypto.so.3` 4 993 808 B + `libssl.so.3` 843 640 B = 69% of
      its 8 431 459 B, present so that apk can fetch packages over HTTPS. musl is
      670 312 B, busybox 804 616 B
- [x] 2.5 Count the non-ELF half of OpenRC: 41 `/etc/init.d` entries and the
      shell tree under `/usr/libexec/rc`, inside the 171 paths / 1.43 MB the
      closure adds. A graph of ELF edges alone would have missed that this is
      mostly a shell program

## 3. Write it down where it cannot be mistaken for a plan

- [x] 3.1 `docs/ALPINE-AS-BASE.md`: the tarball by checksum, the closure table,
      the loader's graph, the three virtuals, and a closing section on what the
      measurement does not say. Every figure in it comes from group 1 or 2
- [x] 3.2 `artifacts/handoff-vm/alpine-probe/README.md`: how to rerun, what each
      evidence file settles, and the rule that a moved sha256 means rerun rather
      than edit
- [ ] 3.3 When Alpine's `v3.24` index moves, rerun `probe.sh` and let it rewrite
      the numbers. **Do not adjust `docs/ALPINE-AS-BASE.md` by hand** — a table
      that no longer matches its script is the unsourced-number defect wearing a
      citation

## 4. Hand the one useful finding to the change that can use it

- [x] 4.1 Tell `image-minimisation` that a working ownership record exists and
      where to look — recorded as `openspec/changes/image-minimisation/tasks.md`
      3.10: Alpine keeps `/lib/apk/db/installed`, which maps every
      installed path to the package that owns it, and the probe reads it
      (`evidence/apk-installed.minirootfs`). That change's D2 has to invent the
      equivalent for a tree built by a book with no package manager; it may
      borrow the format, or reject it. It may not stay unaware of it, because
      "no one can say which files a package owns" is currently asserted in that
      change's proposal as though it were a law of nature rather than a property
      of having no package manager
- [x] 4.2 Confirm with `base-system-and-init` that this change changes nothing it
      owns: the base book stays LFS 13.0-openrc, `base.lock` gains no entries,
      and `docs/INIT-LAYER.md` is not edited by it. Verified, not asserted: the
      change's one work commit, `2c47a4f`, touches only its own artifacts
      (probe, `docs/ALPINE-AS-BASE.md`, this change's files) — 21 files, no
      `base.lock`, no `docs/INIT-LAYER.md`, nothing under
      `openspec/changes/base-system-and-init/` — and the working tree is clean
      for all three

## 5. What would make this change worth archiving

- [ ] 5.1 If a future change proposes Alpine as the base, it starts from this
      measurement and owes the thing this one does not do: boot OpenRC from the
      assembled root and record whether it reaches multi-user. Until that
      exists, `docs/ALPINE-AS-BASE.md` is a cost sheet, and quoting it as a plan
      is a misuse of it
