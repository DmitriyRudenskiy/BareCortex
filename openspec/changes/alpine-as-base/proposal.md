# Proposal: alpine-as-base

## Why

Every time the init layer is discussed someone says "just take OpenRC from
Alpine", and every time the conversation ends without a number. The repository
has no measurement of what that would mean, and `docs/INIT-LAYER.md` is committed
to building OpenRC 0.63 from the pinned LFS 13.0-openrc edition — so the sentence
is neither adopted nor refuted, it just keeps coming back. It has now been
measured, and the measurement is committed:
`artifacts/handoff-vm/alpine-probe/` reproduces it in about 40 seconds on any
x86_64 Linux box, unprivileged, and `docs/ALPINE-AS-BASE.md` is the write-up.

The answer is smaller and more awkward than expected. Taking OpenRC, busybox and
musl from Alpine v3.24 is **7 packages and 1 168 765 B**, of which two (musl,
busybox) the Alpine minirootfs already ships; OpenRC's own cost is **171 paths and
1.43 MB**, and the dependency graph is closed and shallow — 84 executables, all
resolving, and the only library outside openrc and musl is libcap, needed by two
binaries. That part is a strong argument. The awkward part is that the
minirootfs's 8 431 459 B are **69% OpenSSL** (`libcrypto.so.3` 4 993 808 plus
`libssl.so.3` 843 640), because the way to obtain those three packages is `apk`,
and `apk` needs a TLS stack to fetch them. `build-orchestration` D1 declines to
recommend a package manager, following the book's own §8.2; adopting Alpine's
base means adopting one, and shipping its 5.8 MB unless something says otherwise.

So this change decides nothing about the base — the LFS build is 10 chapters from
done and stays the base. What it does is turn the sentence into a record with a
command behind it, and pin the three choices Alpine's index refuses to make, so
that if the alternative is ever taken it is taken deliberately and reproducibly
instead of by whichever provider apk felt like resolving `/bin/sh` to that day.

## What Changes

- `docs/ALPINE-AS-BASE.md` becomes the repository's measurement of the
  alternative: the base tarball by sha256, the 7-package closure, the loader's
  graph, and the three virtuals whose provider has to be chosen by us.
- `artifacts/handoff-vm/alpine-probe/` carries the scripts that produce those
  numbers and the raw output they produced, so a future reader re-measures
  instead of trusting a table.
- Three provider choices are named as decisions rather than left to apk:
  `/bin/sh`, `ifupdown-any`, `openrc-user` / `openrc-user-pam`.
- A rule about how an alternative base may be argued in this repository: with a
  measurement a script reproduces, or not at all.

## Impact

- **Refs** `base-system-and-init` (the base book is pinned there; this change
  does not un-pin it) and `docs/INIT-LAYER.md` (unchanged; it still describes
  what we build).
- **Cross-reads** `image-minimisation`. That change needs the answer to "which
  files does this package own", which today no one can give because there is no
  package manager. Alpine's `/lib/apk/db/installed` is exactly that record, and
  the probe reads it. The ownership mechanism is `image-minimisation`'s to adopt
  or reject; this change only records that a working example of it exists and
  where to look.
- **No build behaviour changes.** Nothing in `phases.toml`, `base.lock`, or
  `scripts/build/` moves because of this change.
