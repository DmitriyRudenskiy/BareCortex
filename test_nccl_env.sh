#!/bin/sh
# test_nccl_env.sh — the three NCCL entry points (login shell, init script,
# deploy script) resolve the same exported values from /etc/conf.d/nccl
# (task 6.8), and the export actually happens (D8's load-bearing caveat).
set -u

ROOT=$(cd -- "$(dirname -- "$0")" && pwd)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fails=0

pass() { echo "PASS: $1"; }
fail() { echo "FAIL: $1"; fails=$((fails + 1)); }

mkdir -p "$T/etc/conf.d"
cp "$ROOT/openrc/conf.d/nccl" "$T/etc/conf.d/nccl"
# give the comparison something to compare: one variable with a ledger row
cat >> "$T/etc/conf.d/nccl" <<'EOF'
NCCL_ALGO=Ring  # 2026-07-18: allreduce 14.2 GB/s vs 9.1 GB/s on the default (auto)
EOF

# --- entry point 1: login shell, via the shim the installer writes ------------
# use setup_cuda.sh's actual shim line, path-rewritten to the fake /etc
shim=$(grep 'set -a; \[ -r /etc/conf.d/nccl \]' "$ROOT/ai-stack/cuda/setup_cuda.sh" | head -1)
[ -n "$shim" ] || fail "setup_cuda.sh writes no one-line shim"
printf '%s\n' "$shim" | sed "s|/etc/conf.d/nccl|$T/etc/conf.d/nccl|g" > "$T/shim.sh"
( . "$T/shim.sh" && env ) | sort > "$T/out.login"

# --- entry point 2: the init script's start_pre block --------------------------
awk '/^    set -a$/,/^    set \+a$/' "$ROOT/openrc/init.d/ai-training" \
    | sed "s|/etc/conf.d/nccl|$T/etc/conf.d/nccl|g" > "$T/svc.sh"
lines=$(wc -l < "$T/svc.sh" | tr -d '[:space:]')
[ "$lines" = 3 ] || fail "init script's source block is not exactly three adjacent lines (got $lines)"
( . "$T/svc.sh" && env ) | sort > "$T/out.service"

# --- entry point 3: the deploy script's block ----------------------------------
awk '/^set -a$/,/^set \+a$/' "$ROOT/scripts/deploy/deploy_training_job.sh" \
    | sed "s|/etc/conf.d/nccl|$T/etc/conf.d/nccl|g" > "$T/deploy.sh"
lines=$(wc -l < "$T/deploy.sh" | tr -d '[:space:]')
[ "$lines" = 3 ] || fail "deploy script's source block is not exactly three adjacent lines (got $lines)"
( . "$T/deploy.sh" && env ) | sort > "$T/out.deploy"

# --- the three must agree -------------------------------------------------------
cmp -s "$T/out.login" "$T/out.service" && pass "login shell and init script export the same environment" \
    || fail "login shell and init script disagree"
cmp -s "$T/out.login" "$T/out.deploy" && pass "login shell and deploy script export the same environment" \
    || fail "login shell and deploy script disagree"
grep -q '^NCCL_ALGO=Ring$' "$T/out.login" \
    && pass "the conf.d assignment is exported (reaches the training process)" \
    || fail "conf.d assignment did not reach the environment"

# --- no NCCL variable is still exported inline by a consumer -------------------
if grep -n '^export NCCL_' "$ROOT/openrc/init.d/ai-training" "$ROOT/scripts/deploy/deploy_training_job.sh" \
        "$ROOT/ai-stack/cuda/setup_cuda.sh" 2>/dev/null; then
    fail "a consumer still exports an NCCL variable inline"
else
    pass "no consumer exports an NCCL variable inline"
fi

if [ "$fails" -eq 0 ]; then
    echo "OK: all nccl-env checks passed"
else
    echo "$fails check(s) FAILED"
    exit 1
fi
