#!/bin/sh
# test_detect_target_devices.sh — self-contained checks for
# kernel/scripts/detect_target_devices.sh. Uses a fake lspci on PATH, a fake
# kernel tree, and the real generator, and finishes with the full chain:
# detect -> human maps the TODOs -> generate succeeds.
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

mkdir -p "$T/bin" "$T/tree/scripts/kconfig" "$T/tree/drivers/net/ethernet/igb" \
    "$T/tree/drivers/nvme" "$T/repo/kernel/scripts" "$T/repo/kernel/fragments"

# --- fake lspci ---------------------------------------------------------------
cat > "$T/bin/lspci" <<'EOF'
#!/bin/sh
[ "$1" = "-nn" ] || exit 2
cat <<'OUT'
00:00.0 Host bridge [0600]: Intel Corporation Xeon E3-1200 v3 Host Bridge [8086:0c01]
00:02.0 Network controller [0200]: Intel Corporation Ethernet Controller I219-V [8086:15bb] (rev 30)
01:00.0 Non-Volatile memory controller [0108]: Samsung NVMe Controller [144d:a808]
02:00.0 Network controller [0200]: Mellanox Technologies MT27710 Family [15b3:1016]
OUT
EOF
chmod +x "$T/bin/lspci"
export PATH="$T/bin:$PATH"

# --- fake kernel tree (same shape as test_generate_config.sh) -------------------
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
mkdir -p "$T/tree/drivers/net/ethernet/mlx5"
printf 'config MLX5_CORE\n' > "$T/tree/drivers/net/ethernet/mlx5/Kconfig"
printf 'config NVME\n' > "$T/tree/drivers/nvme/Kconfig"
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
awk '
    /^CONFIG_/ { split($1, a, "="); val[a[1]] = $0; next }
    /^# CONFIG_[A-Z0-9_]+ is not set$/ { sym = $2; val[sym] = $0; next }
    END { for (s in val) print val[s] }
' "$tmp" > "$tmp.r" && mv "$tmp.r" "$tmp"
cp "$tmp" "$tree/.config"
rm -f "$tmp"
req=$(mktemp ./.req.XXXXXX)
for f in $frags; do grep -oE "CONFIG_[A-Z0-9_]+" "$f" >> "$req" || true; done
sort -u "$req" | while read -r s; do
    if ! grep -qE "^${s}=|^# ${s} is not set" "$tree/.config"; then
        echo "Value requested for $s not in final .config"
    fi
done
rm -f "$req"
exit 0
EOF
chmod +x "$T/tree/scripts/kconfig/merge_config.sh"

# --- fake repo -------------------------------------------------------------------
cp "$ROOT/kernel/scripts/generate_config.sh" "$ROOT/kernel/scripts/detect_target_devices.sh" \
    "$T/repo/kernel/scripts/"
cat > "$T/repo/kernel/fragments/ai-workload.fragment" <<'EOF'
# Test workload fragment
# Audited against: Linux v6.12 (6.12.99, 2026-01-01) — test header
CONFIG_NET=y
EOF

run_det() {
    ( cd "$T/repo/kernel/scripts" && ./detect_target_devices.sh ) 2>&1
}

# 1. fresh write: TODOs remain, exit 1, devices listed with IDs and names
out=$(run_det); rc=$?
check_rc "fresh write exits nonzero while TODOs remain" 1 "$rc"
check_text "fragment names the I219 vendor:device ID" "8086:15bb" "$T/repo/kernel/fragments/target.fragment"
check_text "fragment names the Mellanox vendor:device ID" "15b3:1016" "$T/repo/kernel/fragments/target.fragment"
check_text "fragment keeps the human-readable name" "Intel Corporation Ethernet Controller I219-V" "$T/repo/kernel/fragments/target.fragment"
check_text "fragment carries a network TODO" "TODO: replace this line with CONFIG_<DRIVER>=y for 8086:15bb" "$T/repo/kernel/fragments/target.fragment"
check_text "fragment carries the storage TODO" "root storage controller" "$T/repo/kernel/fragments/target.fragment"
n=$(grep -c '^# TODO:' "$T/repo/kernel/fragments/target.fragment")
[ "$n" = 3 ] && echo "PASS: exactly three TODOs (two NICs + storage)" || { echo "FAIL: expected 3 TODOs, got $n"; fails=$((fails + 1)); }
# every non-blank line is a comment until a human maps the drivers
bad=$(grep -v '^#\|^$' "$T/repo/kernel/fragments/target.fragment" || true)
[ -z "$bad" ] && echo "PASS: fresh fragment is all comments (generator-legal)" || { echo "FAIL: non-comment lines in fresh fragment: $bad"; fails=$((fails + 1)); }

# 2. re-run on the TODO-laden file: regenerates, still nonzero
out=$(run_det); rc=$?
check_rc "re-run on a TODO fragment regenerates" 1 "$rc"

# 3. human maps the TODOs -> re-run refuses to overwrite, exit 0
python3 - "$T/repo/kernel/fragments/target.fragment" <<'EOF'
import sys
p = sys.argv[1]
lines = open(p).read().splitlines()
out = []
for ln in lines:
    if ln.startswith('# TODO:') and '8086:15bb' in ln:
        out.append('CONFIG_IGB=y')
    elif ln.startswith('# TODO:') and '15b3:1016' in ln:
        out.append('CONFIG_MLX5_CORE=y')
    elif ln.startswith('# TODO:'):
        out.append('CONFIG_NVME=y')
    else:
        out.append(ln)
open(p, 'w').write('\n'.join(out) + '\n')
EOF
cp "$T/repo/kernel/fragments/target.fragment" "$T/mapped.fragment"
out=$(run_det); rc=$?
check_rc "complete fragment is not overwritten (exit 0)" 0 "$rc"
cmp -s "$T/mapped.fragment" "$T/repo/kernel/fragments/target.fragment" \
    && echo "PASS: hand-mapped file byte-identical after re-run" \
    || { echo "FAIL: hand-mapped file was modified"; fails=$((fails + 1)); }

# 4. full chain: the mapped fragment now builds
out=$( cd "$T/repo/kernel/scripts" && ./generate_config.sh "$T/tree" ) 2>&1; rc=$?
check_rc "detect -> map -> generate succeeds" 0 "$rc"
check_text "generated config carries the NIC driver" "CONFIG_IGB=y" "$T/repo/kernel/.config"
check_text "generated config carries the storage driver" "CONFIG_NVME=y" "$T/repo/kernel/.config"

# 5. no network device: nonzero, nothing written
cat > "$T/bin/lspci" <<'EOF'
#!/bin/sh
echo "00:00.0 Host bridge [0600]: Intel Corporation Xeon E3-1200 v3 [8086:0c01]"
exit 0
EOF
chmod +x "$T/bin/lspci"
rm -f "$T/repo/kernel/fragments/target.fragment"
out=$(run_det); rc=$?
check_rc "no network controller fails" 1 "$rc"
printf '%s\n' "$out" > "$T/out.txt"
check_text "failure says no network controller" "no PCI class 02" "$T/out.txt"
[ ! -f "$T/repo/kernel/fragments/target.fragment" ] \
    && echo "PASS: no fragment written when there is no hardware" \
    || { echo "FAIL: fragment written despite no network device"; fails=$((fails + 1)); }

if [ "$fails" -eq 0 ]; then
    echo "OK: all detect_target_devices checks passed"
else
    echo "$fails check(s) FAILED"
    exit 1
fi
