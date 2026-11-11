# BareCortex

A bare-metal operating system for AI compute nodes, built from a pinned
upstream book rather than assembled from a distribution. The vocabulary below
is the language the specs, the build, and the rewrites are written in; a word
that appears here has one meaning and the alternatives under _Avoid_ are the
meanings it does not have.

**Before minting a name, grep for it.** A name is a class, a field, an owner, a
phase, or a requirement. Search the repository for the name and its synonyms;
if an existing enumeration covers it, extend that enumeration; mint only the
delta you own. Two parallel vocabularies were minted in the session that wrote
this rule.

## The pin and what it says

**Pin**:
The exact upstream edition — and the revision of it — that the image is built
from.
_Avoid_: version lock, upstream latest, "current"

**Derived file**:
The package list extracted from the pinned book, carrying the revision it was
extracted at.
_Avoid_: generated file, mirror

**Second book**:
A hand-maintained copy of the book's package list — the failure the derived
file exists to prevent.

**`base.lock`**:
The one file naming every package in the image. Where older prose says
"manifest" it means this file and nothing else.
_Avoid_: manifest, package list, inventory

**Owner**:
The single change that may define an entry, a file, or a format. Other changes
add entries to it or name it; none restates it.

**Register**:
One machine-readable file whose rows each name the change owning them, read by
checks and pointed at by prose. `base.lock`, the class map, the phase table, and
a hand-off's consumer list are registers, which is why an ownership argument is
settled by "which row of which register" and not by negotiation.
_Avoid_: manifest, ledger, inventory — each names one specific register here

## The build

**Phase**:
A chapter boundary with entry requirements and exit criteria a machine can
evaluate.

**Exit criteria**:
The observations that must hold before a phase may be recorded as passed. A
phase whose criteria were not evaluated is not passed.

**Off-book phase**:
A build phase whose contents come from this repository or from no book page, so
that a pin bump's diff shows the book moving and nothing else. It carries
first-party code and source-built packages with no page.
_Avoid_: first-party phase — the phase carries `uv`, which is not first-party

**Phase row**:
One row of the phase table — `{name, position, requires[], installs[]}` — and
the single home of ordering, entry requirements, and invalidation.
_Avoid_: phase list, dependency graph

**Orphan**:
A `base.lock` entry named by no phase row's `installs[]`. It invalidates nothing
when it changes, so it ships stale; the coverage check fails naming it.

**Command target**:
The executable whose existence an enabled service is checked against: the
resolved value of `command=` after its `conf.d` substitution. Anything else a
service needs is inventory territory, not this.

**Checkpoint**:
A restorable copy of the build tree and its state record. It restores the tree,
not the disk beside it.

**Phase evidence**:
What a phase leaves behind — command output, installed version, evaluated
criteria, and the checksums of what it built.
_Avoid_: logs, artifacts

**Boot-critical set**:
The named set of kernel drivers that must be built into the kernel, supplied by
a machine this project does not have.
_Avoid_: required modules, storage drivers

**Disposable host**:
The condition of the build machine that makes the inherited passwordless-root
convention acceptable. Without it, the convention is a vulnerability. A guest
whose entire disk is a file is the case that settles it: the substrate can be a
machine nobody can afford to lose, and the host the build destroys can still be
disposable.
_Avoid_: sandbox, VM — neither states who may be destroyed

**Accounted for**:
True of an installed file that traces to a `base.lock` entry. The build refuses
an image containing anything that is not.

## Why a thing is in the image

**Provenance class**:
Why a package is in the image, and the fields its entry must carry to make that
claim checkable. Which classes exist is the class map's business; prose does not
list them.
_Avoid_: manifest kind, package type

**Class map**:
`class-map.toml` — the file whose rows are the provenance classes, each row
naming the fields that class requires and the change owning it. Hand-authored,
and outside `base.lock` because `base.lock`'s contents are derived.
_Avoid_: class list, provenance table

**Source-built**:
Built from the pinned book during the build.
_Avoid_: upstream, from source

**Binary-distribution**:
Adopted as a redistributable binary from its vendor.

**Binary-authenticated-fetch**:
Carried by a human because fetching it needs a login; the build verifies a
checksum of a locally supplied file.

**First-party**:
Code whose source is this repository. The only class with no book page and no
pre-build checksum.
_Avoid_: internal, our code

**Build scope**:
The set of sources whose commit produced an installed artifact — the crate for a
compiled binary, the file for a copied one. What a first-party version counts.
_Avoid_: path, directory

## Governing it after it ships

**Service inventory**:
The record of each shipped OpenRC service — its runlevel, what it requires, and
how it reports failure — enforced by a command that fails on a service with no
entry. It says how a service behaves; `base.lock` says why it is there.
_Avoid_: service list, manifest

**Repo-authoritative**:
True of shipped content whose authority is the repository. An edit made on the
installed machine is out of band and the next build overwrites it.

**Cost threshold**:
The condition a fallback branch fires on, fixed before the measurement that
checks it, and carrying its unit, its anchor, and the price of firing.
_Avoid_: budget, limit

**Rootfs budget**:
The partition-plan number a cost threshold is compared against. A property of a
machine this project does not have.

**Failsafe**:
The state a controller parks in when its measurement fails — for a fan, the
speed it holds at when the temperature cannot be read. A policy, and not owned
by the mechanism that parks in it.
_Avoid_: safe mode, fallback speed

## Rewrites

**Incumbent**:
The implementation a rewrite replaces. It stays in the repository as the parity
reference and the thing a rollback reinstalls.
_Avoid_: legacy, old script, deprecated

**Parity run**:
Driving the incumbent and its replacement over the same input and recording
what each did.

**Usage evidence**:
The observation that the incumbent is actually used. Without it no rewrite is
proposed.

**Candidate**:
Status of a replacement that is written but has never been installed.
_Avoid_: done, ready

**Installed**:
Status of a replacement that is in an image with its rollback executed once.

**Accelerator path**:
The components no language policy reaches: kernel, GPU driver, CUDA userspace,
CPython, PyTorch.

**Mechanism**:
The part of a component that acts — a fan write, a process launch. The
thresholds, intervals, and fail states that decide when it acts are its policy,
and a mechanism may exist without any.
_Avoid_: policy
