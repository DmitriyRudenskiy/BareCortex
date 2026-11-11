# First-party code is accounted for in `base.lock` as its own provenance class

Status: accepted 2026-11-10, enumeration corrected same day

The build refuses an image containing a package it cannot account for, and the
first-party utilities install into `/usr/bin` from this repository with no book
page and no upstream checksum. We decided they get `base.lock` entries under a
**first-party** provenance class rather than being accounted for by phase
records, because the refusal is only enforceable while the lock is a complete
map of the image.

## Considered Options

- **Account for first-party files in the phase record.** Rejected: it makes
  `base.lock` a partial map, and a partial map cannot back a refusal.
- **Fold first-party into `source-built`.** Rejected: `source-built` requires a
  book page and a checksum, so folding means weakening those to "page _or_ repo
  path" for every source-built package to accommodate the one case that has no
  page. A required field that is optional for one class is not a check.
- **A fourth class, `first-party`.** Accepted.

## Consequences

- Provenance classes are **not new** and this decision does not invent them:
  `source-built`, `binary-distribution` and `binary-authenticated-fetch` are
  recorded by `gpu-stack`, whose spec already states that entries live in
  `base.lock` and that the format belongs to `base-system-and-init`. This
  decision adds one class to that list.
- The list, with required fields per class, is a change to the **format** of
  `base.lock` and therefore lands in `base-system-and-init` D3. Every other
  change adds entries and cites the list; none restates it. `userspace-and-ml-stack`
  5.1 adds the `rust` entry and cites it.
- `first-party` is the only class whose required fields are satisfiable before
  the build runs without a book page: the commit and the source path. That
  asymmetry is deliberate and is recorded in ADR 0003.
