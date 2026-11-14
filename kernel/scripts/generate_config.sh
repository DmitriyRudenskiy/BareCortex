#!/bin/sh
# generate_config.sh — build kernel/.config from kernel/fragments/.
#
# usage:
#   generate_config.sh <kernel-source-tree> [--allow-unaudited-kernel]
#
# What it does, in order:
#   1. checks the tree's kernel series against the audited series recorded
#      in the workload fragment's header (D10); a mismatch fails and names
#      the re-audit, and is overridable only by the flag — never by editing
#      a constant
#   2. rejects malformed fragment lines before anything is merged, so the
#      grammar is enforced rather than recommended (D2)
#   3. requires the per-machine target fragment, and requires it to name a
#      network controller driver and a storage controller driver, classified
#      by where the kernel tree defines the symbol (D3) — no speculative
#      driver list
#   4. generates the base unconditionally with alldefconfig in a scratch
#      output dir, so the output never depends on the tree's prior .config
#   5. merges all fragments in one pass, without -m, so the merge tool's
#      own verification runs (D1: -m returns before the loop)
#   6. treats "Value requested for" in the merge output as a hard error
#   7. compares values, not presence, for every fragment line (D14)
#   8. writes the result to kernel/.config — the tracked content is the
#      fragments; the generated file is untracked
#
# The merge log goes to a temp file outside the repository. No base backup
# is kept: the scratch dir is the base, and it is deleted on exit.

set -eu

SCRIPT_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
# One repository-root constant; every generated path hangs off it.
REPO_ROOT=$(cd -- "$SCRIPT_DIR/../.." && pwd)
FRAG_DIR=$REPO_ROOT/kernel/fragments
WORKLOAD_FRAGMENT=$FRAG_DIR/ai-workload.fragment
TARGET_FRAGMENT=$FRAG_DIR/target.fragment
OUT=$REPO_ROOT/kernel/.config

usage() {
    echo "usage: $0 <kernel-source-tree> [--allow-unaudited-kernel]" >&2
    exit 2
}

TREE=
ALLOW_UNAUDITED=0
for arg in "$@"; do
    case $arg in
        --allow-unaudited-kernel) ALLOW_UNAUDITED=1 ;;
        -h|--help) usage ;;
        *)
            [ -z "$TREE" ] || usage
            TREE=$arg
            ;;
    esac
done
[ -n "$TREE" ] || usage
[ -d "$TREE" ] || { echo "generate_config: no such kernel tree: $TREE" >&2; exit 1; }
[ -f "$WORKLOAD_FRAGMENT" ] || {
    echo "generate_config: missing $WORKLOAD_FRAGMENT" >&2; exit 1; }

# --- 1. kernel series gate (D10) --------------------------------------------
# The audited series is recorded in the workload fragment's header — the
# single reference for this gate. The bypass is a flag, greppable in a shell
# history line; an edited constant is indistinguishable from a fix.
audited=$(sed -n 's/^# Audited against: Linux v\([0-9][0-9]*\.[0-9][0-9]*\).*/\1/p' \
    "$WORKLOAD_FRAGMENT" | head -n1)
[ -n "$audited" ] || {
    echo "generate_config: the fragment header records no audited series" >&2;
    exit 1; }
actual=$(cd "$TREE" && make -s kernelversion)
series=${actual%.*}
if [ "$series" != "$audited" ]; then
    if [ "$ALLOW_UNAUDITED" -eq 1 ]; then
        echo "generate_config: building on $actual, audited against v$audited (flag given)"
    else
        {
            echo "generate_config: kernel series mismatch"
            echo "  audited:  v$audited (recorded in the fragment header)"
            echo "  actual:   $actual"
            echo "  Re-audit the fragment against $actual before building on it:"
            echo "  check every symbol in kernel/fragments/ against that tag,"
            echo "  update the header's audited line, and record which tag the"
            echo "  audit used — or pass --allow-unaudited-kernel to build on"
            echo "  the unaudited tree anyway."
        } >&2
        exit 1
    fi
fi

# --- 2. fragment grammar (D2) -------------------------------------------------
# Every line is blank, a comment, CONFIG_X=y|m|n, or '# CONFIG_X is not
# set'. Anything else is inert against the merge tool's anchored patterns —
# a decision that asserts nothing. A commented-out value is the worst form:
# it reads like a disabled decision and is pure prose.
bad=
for frag in "$FRAG_DIR"/*.fragment; do
    [ -f "$frag" ] || continue
    lineno=0
    while IFS= read -r line || [ -n "$line" ]; do
        lineno=$((lineno + 1))
        case $line in
            "") continue ;;
            "# CONFIG_"*"="*)
                bad="$bad $frag:$lineno (commented-out value: $line)" ;;
            "#"*) continue ;;
            CONFIG_*)
                case $line in
                    CONFIG_*=y|CONFIG_*=m|CONFIG_*=n) continue ;;
                    *) bad="$bad $frag:$lineno (not CONFIG_X=y|m|n: $line)" ;;
                esac ;;
            *) bad="$bad $frag:$lineno (not a comment or config line: $line)" ;;
        esac
    done < "$frag"
done
if [ -n "$bad" ]; then
    echo "generate_config: malformed fragment lines:" >&2
    for entry in $bad; do echo "  $entry" >&2; done
    exit 1
fi

# --- 3. target fragment (D3) --------------------------------------------------
if [ ! -f "$TARGET_FRAGMENT" ]; then
    {
        echo "generate_config: missing $TARGET_FRAGMENT"
        echo "  It is per-machine and must exist before generation: run the"
        echo "  detector and map the drivers by hand. A warning-and-continue"
        echo "  here would emit a kernel with no driver for this machine's"
        echo "  hardware — the exact failure this check exists to remove."
    } >&2
    exit 1
fi
# An example is documentation, not input.
for ex in "$FRAG_DIR"/*.example; do
    if [ -f "$ex" ]; then
        echo "generate_config: refusing to merge an example fragment: $ex" >&2
        exit 1
    fi
done

# A symbol counts as a network (storage) controller driver when the kernel
# tree defines it under drivers/net/ (drivers/nvme/, drivers/scsi/,
# drivers/ata/). The tree's own Kconfig is the classifier; no driver list is
# shipped with this script.
classify() {
    f=$(grep -rl --include=Kconfig -E "^config $1\$" \
        "$TREE/drivers/net" "$TREE/drivers/nvme" "$TREE/drivers/scsi" \
        "$TREE/drivers/ata" 2>/dev/null | head -n1)
    [ -n "$f" ] || return 0
    case $f in
        */drivers/net/*) echo net ;;
        *) echo storage ;;
    esac
}

net=0
storage=0
while IFS= read -r line; do
    case $line in
        CONFIG_*=y|CONFIG_*=m|CONFIG_*=n) ;;
        *) continue ;;
    esac
    sym=${line#CONFIG_}
    sym=${sym%%=*}
    kind=$(classify "$sym")
    if [ "$kind" = net ]; then net=1; fi
    if [ "$kind" = storage ]; then storage=1; fi
done < "$TARGET_FRAGMENT"
if [ "$net" -ne 1 ]; then
    echo "generate_config: $TARGET_FRAGMENT names no network controller driver" >&2
    exit 1
fi
if [ "$storage" -ne 1 ]; then
    echo "generate_config: $TARGET_FRAGMENT names no storage controller driver (an NVMe or HBA symbol)" >&2
    exit 1
fi

# --- 4-5. base + merge (D1) ----------------------------------------------------
log=$(mktemp "${TMPDIR:-/tmp}/barecortex-merge.XXXXXX")
base_dir=$(mktemp -d "${TMPDIR:-/tmp}/barecortex-base.XXXXXX")
trap 'rm -rf "$log" "$base_dir"' EXIT

# Unconditional base: a scratch output dir has no prior state, and
# alldefconfig is what the merge tool drives anyway.
( cd "$TREE" && make -s O="$base_dir" alldefconfig )
base=$base_dir/.config
[ -f "$base" ] || { echo "generate_config: alldefconfig produced no base config" >&2; exit 1; }

# One pass over every fragment, no -m: with -m the tool copies the merged
# file and returns before its verification loop, opting out of the
# missed-dependency check. merge_config.sh writes its scratch files relative
# to the working directory, so it runs from the tree.
fragments=
for f in "$FRAG_DIR"/*.fragment; do
    [ -f "$f" ] || continue
    fragments="$fragments $f"
done
( cd "$TREE" && "$TREE/scripts/kconfig/merge_config.sh" \
    -O "$TREE" $base $fragments ) >>"$log" 2>&1 || {
    echo "generate_config: merge_config.sh failed:" >&2
    cat "$log" >&2
    exit 1; }

# --- 6. the string match is the enforcement ------------------------------------
# merge_config.sh exits 0 whether or not every requested symbol survived: its
# verification loop only prints (v6.12 scripts/kconfig/merge_config.sh — the
# "Value requested for" loop has no exit). Never replace this grep with
# reliance on the exit status; doing so silently removes the only thing that
# distinguishes this from the -m version.
if grep -q "Value requested for" "$log"; then
    echo "generate_config: requested symbols did not survive the merge:" >&2
    grep -A2 "Value requested for" "$log" >&2
    exit 1
fi

# --- 7. values, not presence (D14) ---------------------------------------------
# merge_config.sh's extraction discards the value before its comparison runs,
# so an invalid value on a real symbol passes its check. For every fragment
# line, read the symbol out of the final .config and compare the value,
# including the 'is not set' case.
fail=0
verified=0
for frag in "$FRAG_DIR"/*.fragment; do
    [ -f "$frag" ] || continue
    while IFS= read -r line; do
        case $line in
            CONFIG_*=y|CONFIG_*=m|CONFIG_*=n)
                sym=${line%%=*}
                want=${line#*=}
                if [ "$want" = n ]; then
                    actual=$(grep -E "^${sym}=|^# ${sym} is not set" "$TREE/.config" | head -n1)
                    [ -z "$actual" ] || actual=${actual%%=*}
                    [ -z "$actual" ] || {
                        echo "generate_config: $frag: $sym requested=n actual=$actual" >&2
                        fail=1; }
                else
                    actual=$(sed -n "s/^${sym}=//p" "$TREE/.config" | head -n1)
                    [ -n "$actual" ] || actual="(not set)"
                    if [ "$actual" != "$want" ]; then
                        echo "generate_config: $frag: $sym requested=$want actual=$actual" >&2
                        fail=1
                    fi
                fi
                verified=$((verified + 1))
                ;;
            "# CONFIG_"*" is not set")
                sym=$(printf '%s\n' "$line" | sed -n 's/^# \(CONFIG_[A-Z0-9_]*\) is not set$/\1/p')
                actual=$(sed -n "s/^${sym}=//p" "$TREE/.config" | head -n1)
                if [ -n "$actual" ]; then
                    echo "generate_config: $frag: $sym requested=(not set) actual=$actual" >&2
                    fail=1
                fi
                verified=$((verified + 1))
                ;;
        esac
    done < "$frag"
done
if [ "$fail" -ne 0 ]; then
    exit 1
fi
if [ "$verified" -eq 0 ]; then
    echo "generate_config: no fragment line to verify — nothing was done" >&2
    exit 1
fi

# --- 8. output -------------------------------------------------------------------
cp "$TREE/.config" "$OUT"
nfrags=$(printf '%s\n' $fragments | wc -l | tr -d ' ')
echo "generate_config: $OUT written — $nfrags fragments merged, $verified lines verified, kernel $actual"
