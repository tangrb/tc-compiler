#!/bin/sh
set -e

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VM_BIN="$ROOT/build/vm/bin/tc-vm"
STRESS="$ROOT/tests/stress/massive_vars.tc"
BASELINE="$ROOT/tests/stress/bench_baseline.tc"

if [ ! -x "$VM_BIN" ]; then
    echo "error: tc-vm not found at $VM_BIN (run: make vm)" >&2
    exit 1
fi

bench_one() {
    label="$1"
    file="$2"
    echo "=== $label ==="
    TC_BENCH=1 "$VM_BIN" --check "$file" </dev/null 2>&1 | grep '^bench ' || true
    TC_BENCH=1 "$VM_BIN" "$file" </dev/null 2>&1 | grep '^bench ' || true
    echo ""
}

echo "TC-VM performance benchmark"
echo "Host: $(uname -srm 2>/dev/null || echo unknown)"
echo ""

bench_one "stress/massive_vars.tc (~200 vars)" "$STRESS"

if [ -f "$BASELINE" ]; then
    bench_one "stress/bench_baseline.tc (1000 vars)" "$BASELINE"
else
    echo "=== stress/bench_baseline.tc (skipped, file missing) ==="
    echo ""
fi

echo "Tip: set TC_BENCH=1 to print parse/analyze/execute phase timings on stderr."
