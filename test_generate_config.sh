#!/bin/sh
# test_generate_config.sh — self-contained checks for
# kernel/scripts/generate_config.sh. Builds a fake kernel tree (Makefile,
# stub merge_config.sh that reproduces v6.12's exit-0-on-missing-symbols,
# Kconfig files for driver classification) and a fake repo, then exercises
# the generator's failure modes. No kernel, no network, no build host.
set -u

ROOT=$(cd -- "$(dirname -- "$0")" && pwd)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fails=0

check_rc() { # $1=description $2=expected rc $3=actual rc
    if [ "$2" = "$3" ]; then
        echo "PASS: $1"
    else
        echo "FAIL: $1 (expected rc $2, got $3)"
        fails=$((fails + 1))
    fi
}
check_text() { # $1=description $2=needle $3=file
    if grep -q "$2" "$3" 2>/dev/null; then
        echo "PASS: $1"
    else
        echo "FAIL: $1 (missing: $2)"
        fails=$((fails + 1))
    fi
}

mkdir -p "$T/tree/scripts/kconfig" "$T/tree/drivers/net/ethernet/igb" \
    "$T/tree/drivers/nvme" "$T/repo/kernel/scripts" "$T/repo/kernel/fragments"

# --- fake kernel tree ---------------------------------------------------------
cat > "$T/tree/Makefile" <<'EOF'
KV ?= 6.12.9
kernelversion:
	@echo $(KV)
alldefconfig:
	@mkdir -p $(if $(O),$(O),.)
	@cp base.config.in $(if $(O),$(O)/.config,.config)
EOF
cat > "$T/tree/base.config.in" <<'EOF'
CONFIG_NET=y
CONFIG_EXT4_FS=y
EOF
printf 'config IGB\n' > "$T/tree/drivers/net/ethernet/igb/Kconfig"
printf 'config NVME\n' > "$T/tree/drivers/nvme/Kconfig"

# Stub merge_config.sh: positional BASE FRAG..., -O dir, scratch file
# relative to the CWD, and — the point of it — it ALWAYS exits 0, even when
# a requested symbol does not survive, exactly like v6.12.
cat > "$T/tree/scripts/kconfig/merge_config.sh" <<'EOF'
#!/bin/sh
tree="" base="" frags=""
while [ $# -gt 0 ]; do
    case $1 in
        -O) tree=$2; shift 2 ;;
        -m) shift ;;
        *)
            if [ -z "$base" ]; then base=$1; else frags="$frags $1"; fi
            shift
            ;;
    esac
done
[ -n "$tree" ] || exit 1
tmp=$(mktemp ./.tmp.config.XXXXXX)
cat "$base" > "$tmp"
for f in $frags; do cat "$f" >> "$tmp"; done
# last occurrence per symbol wins
awk '
    /^CONFIG_/ { split($1, a, "="); val[a[1]] = $0; next }
    /^# CONFIG_[A-Z0-9_]+ is not set$/ { sym = $2; val[sym] = $0; next }
    END { for (s in val) print val[s] }
' "$tmp" > "$tmp.r" && mv "$tmp.r" "$tmp"
# drop symbols the tree does not know (missed dependency / removal)
if [ -f "$tree/unknown-syms.txt" ]; then
    while read -r u; do
        grep -v "^${u}=\|^# ${u} is not set" "$tmp" > "$tmp.f" || true
        mv "$tmp.f" "$tmp"
    done < "$tree/unknown-syms.txt"
fi
# values the tree's Kconfig would override anyway
if [ -f "$tree/force-values.txt" ]; then
    while read -r spec; do
        s=${spec%%=*}; v=${spec#*=}
        grep -v "^${s}=\|^# ${s} is not set" "$tmp" > "$tmp.f" || true
        if [ "$v" != "!" ]; then printf '%s=%s\n' "$s" "$v" >> "$tmp.f"; fi
        mv "$tmp.f" "$tmp"
    done < "$tree/force-values.txt"
fi
cp "$tmp" "$tree/.config"
rm -f "$tmp"
# verification loop: prints, never exits nonzero — the real script's defect
req=$(mktemp ./.req.XXXXXX)
for f in $frags; do
    grep -oE "CONFIG_[A-Z0-9_]+" "$f" >> "$req" || true
done
sort -u "$req" | while read -r s; do
    if ! grep -qE "^${s}=|^# ${s} is not set" "$tree/.config"; then
        echo "Value requested for $s not in final .config"
        echo "Requested value:  (see fragment)"
        echo "Actual value:     "
    fi
done
rm -f "$req"
exit 0
EOF
chmod +x "$T/tree/scripts/kconfig/merge_config.sh"

# --- fake repo ------------------------------------------------------------------
cp "$ROOT/kernel/scripts/generate_config.sh" "$T/repo/kernel/scripts/"

reset() {
    cat > "$T/repo/kernel/fragments/ai-workload.fragment" <<'EOF'
# Test workload fragment
# Audited against: Linux v6.12 (6.12.99, 2026-01-01) — test header
CONFIG_NET=y
CONFIG_HUGETLBFS=y
EOF
    cat > "$T/repo/kernel/fragments/target.fragment" <<'EOF'
# Per-machine target fragment (test)
CONFIG_IGB=y
CONFIG_NVME=y
EOF
    rm -f "$T/tree/unknown-syms.txt" "$T/tree/force-values.txt" \
        "$T/repo/kernel/fragments/"*.example \
        "$T/repo/kernel/.config"
    set_kv 6.12.9
}

run() {
    ( cd "$T/repo/kernel/scripts" && ./generate_config.sh "$T/tree" "$@" ) 2>&1
}

set_kv() {
    awk -v kv="$1" '{ if ($0 ~ /^KV/) print "KV ?= " kv; else print }' \
        "$T/tree/Makefile" > "$T/tree/Makefile.new"
    mv "$T/tree/Makefile.new" "$T/tree/Makefile"
}

# 1. happy path
reset
out=$(run); rc=$?
check_rc "happy path exits 0" 0 "$rc"
check_text "happy path writes kernel/.config with the NIC driver" "CONFIG_IGB=y" "$T/repo/kernel/.config"
check_text "happy path carries a workload line" "CONFIG_HUGETLBFS=y" "$T/repo/kernel/.config"
check_text "happy path keeps the base line" "CONFIG_NET=y" "$T/repo/kernel/.config"

# 2. version gate
set_kv 6.18.10
out=$(run); rc=$?
check_rc "unaudited series fails" 1 "$rc"
printf '%s\n' "$out" > "$T/out.txt"
check_text "gate names the re-audit" "Re-audit" "$T/out.txt"
out=$(run --allow-unaudited-kernel); rc=$?
check_rc "flag overrides the gate" 0 "$rc"
reset

# 3. missing target fragment
rm "$T/repo/kernel/fragments/target.fragment"
out=$(run); rc=$?
check_rc "missing target fragment fails" 1 "$rc"
reset

# 4. target with no network driver
printf 'CONFIG_NVME=y\n' > "$T/repo/kernel/fragments/target.fragment"
out=$(run); rc=$?
check_rc "target without a network driver fails" 1 "$rc"
reset

# 5. target with no storage driver
printf 'CONFIG_IGB=y\n' > "$T/repo/kernel/fragments/target.fragment"
out=$(run); rc=$?
check_rc "target without a storage driver fails" 1 "$rc"
reset

# 6. grammar: trailing text on a value line
printf 'CONFIG_BROKEN=y  # oops\n' >> "$T/repo/kernel/fragments/ai-workload.fragment"
out=$(run); rc=$?
check_rc "trailing text on a value line fails" 1 "$rc"
reset

# 7. grammar: commented-out value
printf '# CONFIG_OFF=y\n' >> "$T/repo/kernel/fragments/ai-workload.fragment"
out=$(run); rc=$?
check_rc "commented-out value fails" 1 "$rc"
reset

# 8. a requested symbol that does not survive — the stub exits 0, so the
#    grep is what must catch it
printf 'CONFIG_DOES_NOT_EXIST=y\n' >> "$T/repo/kernel/fragments/target.fragment"
echo "CONFIG_DOES_NOT_EXIST" > "$T/tree/unknown-syms.txt"
out=$(run); rc=$?
check_rc "unsurvived symbol fails despite merge exit 0" 1 "$rc"
printf '%s\n' "$out" > "$T/out.txt"
check_text "failure names the symbol" "CONFIG_DOES_NOT_EXIST" "$T/out.txt"
reset

# 9. D14: requested value that did not survive, on a symbol that did
printf 'CONFIG_DRM=m\n' >> "$T/repo/kernel/fragments/ai-workload.fragment"
echo "CONFIG_DRM=y" > "$T/tree/force-values.txt"
out=$(run); rc=$?
check_rc "value mismatch fails (presence-only merge check would pass)" 1 "$rc"
printf '%s\n' "$out" > "$T/out.txt"
check_text "value mismatch reports requested and actual" "requested=m actual=y" "$T/out.txt"
reset

# 10. D14: requested 'is not set' that did not survive
printf '# CONFIG_EXT4_FS is not set\n' >> "$T/repo/kernel/fragments/ai-workload.fragment"
echo "CONFIG_EXT4_FS=y" > "$T/tree/force-values.txt"
out=$(run); rc=$?
check_rc "'is not set' mismatch fails" 1 "$rc"
reset

# 11. an example fragment is documentation, not input
printf 'CONFIG_IGB=y\n' > "$T/repo/kernel/fragments/target-nic.fragment.example"
out=$(run); rc=$?
check_rc "example fragment is refused" 1 "$rc"

if [ "$fails" -eq 0 ]; then
    echo "OK: all generate_config checks passed"
else
    echo "$fails check(s) FAILED"
    exit 1
fi
