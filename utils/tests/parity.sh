#!/bin/bash
# parity.sh — the runnable half of the replacement evidence.
#
# `base-system-and-init` D4 admits a replacement only with usage evidence,
# parity evidence captured from the incumbent, and a rollback that has been
# executed. This script produces what a machine without an NVIDIA card can
# produce: every bmc-* binary driven against the same canned driver output the
# shell incumbents consume, with the exit code asserted, plus a record of what
# the incumbent does with that same input.
#
# It is not the GPU verification. Thresholds against a real driver, the field
# names against a real driver branch, and the rollback rehearsal stay `(gpu)`
# tasks (maintenance-and-updates 6.3, 7.6; gpu-stack 7.4).
#
# Run: bash utils/tests/parity.sh   (from anywhere; no network, no GPU)
set -u

here="$(cd "$(dirname "$0")" && pwd)"
root="$(dirname "$(dirname "$here")")"
utils="$root/utils"
fake="$here/fake-nvidia-smi"
chmod +x "$fake"

# A bin dir holding `nvidia-smi` as the fake, so the shell incumbents can be
# run against the same answers without touching this machine's PATH.
bin="$(mktemp -d)"
ln -sf "$fake" "$bin/nvidia-smi"

pass=0
fail=0

# expect <name> <expected-exit> <pattern> <env...> -- <cmd...>
expect() {
    local name="$1" want="$2" pattern="$3"
    shift 3
    local out code
    out="$(env "$@" 2>&1)"
    code=$?
    if [ "$code" != "$want" ]; then
        echo "FAIL  $name: exit $code, wanted $want"
        echo "$out" | sed 's/^/        /'
        fail=$((fail + 1))
    elif ! grep -q -- "$pattern" <<<"$out"; then
        echo "FAIL  $name: output has no \"$pattern\""
        echo "$out" | sed 's/^/        /'
        fail=$((fail + 1))
    else
        echo "ok    $name"
        pass=$((pass + 1))
    fi
}

record() {
    echo "note  $1"
}

echo "== build (offline, zero external dependencies) =="
if ! cargo build --offline --quiet --manifest-path "$utils/Cargo.toml" --workspace; then
    echo "FAIL  workspace does not build offline"
    exit 1
fi
tree="$(cargo tree --offline --manifest-path "$utils/Cargo.toml" --workspace --prefix none | grep -v '^$')"
# A path dependency prints as `name vversion (/path)`; anything from a registry
# prints without the parenthesised path. An empty tree means cargo printed
# nothing, which is a check that cannot fail, so it is a failure too.
external="$(grep -v '(' <<<"$tree" || true)"
if [ -z "$tree" ]; then
    echo "FAIL  cargo tree printed nothing; the dependency check cannot fail"
    fail=$((fail + 1))
elif [ -n "$external" ]; then
    echo "FAIL  the dependency graph is not empty, so --offline is a claim, not a fact:"
    echo "$external"
    fail=$((fail + 1))
else
    echo "ok    no crate outside the workspace"
    pass=$((pass + 1))
fi

health="$utils/target/debug/bmc-health"
vram="$utils/target/debug/bmc-vram"
fan="$utils/target/debug/bmc-fan"
run="$utils/target/debug/bmc-run"

echo
echo "== the driver is absent: nothing may be reported as healthy =="
expect "bmc-health exits 3 without nvidia-smi" 3 UNAVAILABLE \
    BMC_NVIDIA_SMI="$bin/nope" "$health"

echo
echo "== canned driver: the checks that the shell version could not fail =="
if [ "$(uname)" = Linux ]; then
    # The module/kernel record is written at install time, so a fresh host has
    # none and "healthy driver" alone cannot exit 0 here — the pairing stays
    # unknown. BMC_DRIVER_RECORD supplies one for the same reason the driver is
    # overridable. The mismatch case is the one `maintenance-and-updates` D3
    # exists for, so it is asserted too: a kernel that moved out from under the
    # module must be a PROBLEM, not a pass.
    rec="$(mktemp)"
    uname -r > "$rec"
    expect "healthy driver, module built for the running kernel: exits 0" 0 "module built for" \
        BMC_NVIDIA_SMI="$fake" FAKE_SMI_SCENARIO=healthy BMC_DRIVER_RECORD="$rec" "$health"
    echo "6.0.0-not-the-running-kernel" > "$rec"
    expect "module built for another kernel: exits 1 naming both" 1 \
        "but the GPU module was built for 6.0.0-not-the-running-kernel" \
        BMC_NVIDIA_SMI="$fake" FAKE_SMI_SCENARIO=healthy BMC_DRIVER_RECORD="$rec" "$health"
    rm -f "$rec"
else
    # /proc/sys/kernel/osrelease is Linux. Off the target class the module and
    # kernel pairing is genuinely unknown, and unknown is exit 2, not a pass.
    expect "healthy driver on a non-Linux host: only the pairing is unknown" 2 \
        "running kernel release unknown" \
        BMC_NVIDIA_SMI="$fake" FAKE_SMI_SCENARIO=healthy "$health"
fi
expect "a hot GPU exits 1 and names the value" 1 "temperature 91C exceeds 85C" \
    BMC_NVIDIA_SMI="$fake" FAKE_SMI_SCENARIO=hot "$health"
expect "uncorrected ECC exits 1" 1 "ECC uncorrected 3" \
    BMC_NVIDIA_SMI="$fake" FAKE_SMI_SCENARIO=ecc "$health"
expect "an unreadable persistence field exits 2, not 0" 2 "persistence mode field unavailable" \
    BMC_NVIDIA_SMI="$fake" FAKE_SMI_SCENARIO=nopersist "$health"
expect "no devices visible exits 1" 1 "no GPU devices visible" \
    BMC_NVIDIA_SMI="$fake" FAKE_SMI_SCENARIO=empty "$health"

echo
echo "== the incumbent, same canned input, for the record =="
(export PATH="$bin:$PATH" FAKE_SMI_SCENARIO=hot
 cd "$root" && bash scripts/maintenance/check_gpu_health.sh >/dev/null 2>&1)
record "the incumbent check_gpu_health.sh reports a 91C GPU and exits $? — the defect bmc-health exists to fix"

echo
echo "== bmc-vram: dry run by default, and nothing signalled =="
expect "no holders exits 0" 0 "no processes hold device memory" \
    BMC_NVIDIA_SMI="$fake" FAKE_SMI_SCENARIO=healthy "$vram"
expect "holders without --apply is a dry run" 0 "DRY RUN" \
    BMC_NVIDIA_SMI="$fake" FAKE_SMI_SCENARIO=healthy FAKE_SMI_APPS="4101:train.py:1000" "$vram"
expect "--apply --all acts only where ownership is established" 2 "owner unknown" \
    BMC_NVIDIA_SMI="$fake" FAKE_SMI_SCENARIO=healthy FAKE_SMI_APPS="4101:train.py:1000" \
    "$vram" --apply --all
if [ "$(uname)" = Linux ]; then
    record "on Linux this run refuses a foreign owner instead; see maintenance-and-updates 7.6 for the (gpu) demonstration"
fi

echo
echo "== bmc-fan: success requires an accepted write (gpu-stack 7.4) =="
expect "--check reads the driver without writing" 0 "fan speed 40" \
    BMC_NVIDIA_SMI="$fake" FAKE_SMI_SCENARIO=healthy "$fan" --check
expect "an accepted write exits 0" 0 "accepted fan speed 80" \
    BMC_NVIDIA_SMI="$fake" FAKE_SMI_SCENARIO=healthy "$fan" --percent 80
expect "a rejected write exits 1 and quotes the driver" 1 "REFUSED" \
    BMC_NVIDIA_SMI="$fake" FAKE_SMI_SCENARIO=nofan "$fan" --percent 80
expect "an out-of-range speed never reaches the driver" 1 "out of range" \
    BMC_NVIDIA_SMI="$fake" FAKE_SMI_SCENARIO=healthy "$fan" --percent 101

echo
echo "== bmc-run: the plan is resolved before anything is started =="
job="$bin/train.py"
echo "print('job')" >"$job"
venvpy="$bin/python"
printf '#!/bin/sh\necho fake-python\n' >"$venvpy"
chmod +x "$venvpy"
expect "a device that does not exist is refused" 1 "device 7 requested but 2 device" \
    BMC_NVIDIA_SMI="$fake" FAKE_SMI_SCENARIO=healthy \
    "$run" --script "$job" --python "$venvpy" --gpus 0,7 --dry-run
expect "a duplicate device is refused" 1 "listed twice" \
    BMC_NVIDIA_SMI="$fake" FAKE_SMI_SCENARIO=healthy \
    "$run" --script "$job" --python "$venvpy" --gpus 1,1 --dry-run
expect "no device list anywhere is refused, not defaulted" 1 "no device list" \
    BMC_NVIDIA_SMI="$fake" FAKE_SMI_SCENARIO=healthy \
    "$run" --script "$job" --python "$venvpy" --dry-run
expect "a resolved plan prints and starts nothing" 0 "DRY RUN" \
    BMC_NVIDIA_SMI="$fake" FAKE_SMI_SCENARIO=healthy \
    "$run" --script "$job" --python "$venvpy" --gpus 0,1 --dry-run
expect "a missing venv is refused with the way to make one" 1 "Create the environment" \
    BMC_NVIDIA_SMI="$fake" FAKE_SMI_SCENARIO=healthy \
    "$run" --script "$job" --project nosuchproject --gpus 0,1 --dry-run

rm -rf "$bin"
echo
echo "== $pass passed, $fail failed =="
[ "$fail" = 0 ]
