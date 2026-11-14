#!/bin/sh
# detect_target_devices.sh — list this machine's network controllers and
# write the target fragment for kernel/scripts/generate_config.sh.
#
# usage: detect_target_devices.sh
#
# It prints the PCI class 02 devices with their vendor:device IDs and
# writes kernel/fragments/target.fragment with TODO markers around the one
# step it cannot automate: mapping a vendor:device ID to its kernel driver
# symbol. It is called detect, not generate, because the fragment is a
# human decision this script scaffolds (D3).
#
# Exit codes:
#   0  an existing target.fragment has no unresolved TODOs — it was
#      hand-mapped, so it is not overwritten (re-running is a no-op)
#   1  no network controller was found, or the written fragment still
#      carries TODOs — the fragment is not buildable until a human maps
#      the drivers
set -eu

SCRIPT_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(cd -- "$SCRIPT_DIR/../.." && pwd)
FRAG_DIR=$REPO_ROOT/kernel/fragments
TARGET_FRAGMENT=$FRAG_DIR/target.fragment

# lspci -nn emits, per device:
#   00:02.0 Network controller [0200]: Intel Corporation I219-V [8086:15bb] (rev 30)
# The filter matches the bracketed class field, because the human-readable
# class name ("Network controller") is not a stable match, and the
# vendor:device ID is the field the driver lookup actually uses.
devices=$(lspci -nn 2>/dev/null | awk '
    / \[02[0-9a-f][0-9a-f]\]: / {
        line = $0
        sub(/^[0-9a-f][0-9a-f]:[0-9a-f][0-9a-f]\.[0-9] +\[02[0-9a-f][0-9a-f]\]: +/, "", line)
        if (match(line, /\[[0-9a-f][0-9a-f][0-9a-f][0-9a-f]:[0-9a-f][0-9a-f][0-9a-f][0-9a-f]\]/)) {
            vd = substr(line, RSTART + 1, RLENGTH - 2)
            name = substr(line, 1, RSTART - 1)
            sub(/ +$/, "", name)
            if (name != "")
                printf "%s %s\n", name, vd
        }
    }')
if [ -z "$devices" ]; then
    {
        echo "detect_target_devices: no PCI class 02 (network controller) device found"
        echo "  Run 'lspci -nn' on the target machine and check what it actually has."
        echo "  A target fragment cannot be scaffolded for hardware that is not present."
    } >&2
    exit 1
fi

if [ -f "$TARGET_FRAGMENT" ] && ! grep -q '^# TODO:' "$TARGET_FRAGMENT"; then
    echo "detect_target_devices: $TARGET_FRAGMENT has no unresolved TODOs — it was hand-mapped and is not overwritten"
    exit 0
fi

{
    echo "# Target fragment — written by detect_target_devices.sh on $(hostname 2>/dev/null || echo unknown) $(date -u +%Y-%m-%d)"
    echo "#"
    echo "# Complete every TODO before the generator will build this machine:"
    echo "# replace each TODO line with CONFIG_<DRIVER>=y, where <DRIVER> is"
    echo "# the kernel driver symbol for the vendor:device ID (look it up in"
    echo "# the kernel's PCI ID table, drivers/net/ethernet/<vendor>/Kconfig,"
    echo "# or with modinfo on a matching module)."
    echo ""
    echo "# --- Network controllers (PCI class 02) ---"
    # bdf is re-attached from lspci for the human's lookup convenience
    bdfs=$(lspci -nn 2>/dev/null | awk '/ \[02[0-9a-f][0-9a-f]\]: / { print $1 }')
    n=0
    while IFS= read -r dev; do
        [ -n "$dev" ] || continue
        n=$((n + 1))
        bdf=$(printf '%s\n' "$bdfs" | sed -n "${n}p")
        name=${dev% [}*
        vd=${dev##* }
        echo "# ${bdf:-?}  ${name}  ${vd}"
        echo "# TODO: replace this line with CONFIG_<DRIVER>=y for ${vd}"
    done <<EOF
$devices
EOF
    echo ""
    echo "# --- Storage controllers (PCI class 01) ---"
    echo "# The build boots from this machine's storage, and the generator"
    echo "# requires a storage controller driver in this fragment. List the"
    echo "# class 01 devices with:"
    echo "#   lspci -nn | grep ' \\[01'"
    echo "# and add the driver of the one that holds the root filesystem."
    echo "# TODO: replace this line with CONFIG_<DRIVER>=y for the root storage controller"
} > "$TARGET_FRAGMENT"

remaining=$(grep -c '^# TODO:' "$TARGET_FRAGMENT")
{
    echo "detect_target_devices: wrote $TARGET_FRAGMENT"
    echo "  $remaining unresolved TODO(s) — map each vendor:device ID to its driver symbol,"
    echo "  replace the TODO lines, and re-run. The generator refuses to build"
    echo "  while any TODO remains, and this script refuses to overwrite a"
    echo "  fragment that no longer has any."
} >&2
exit 1
