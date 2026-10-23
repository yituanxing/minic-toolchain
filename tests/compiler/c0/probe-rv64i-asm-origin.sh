#!/usr/bin/env bash
# Host-only provenance/checker sanity before spending seven Linux runners.
set -Eeuo pipefail
if (( $# != 1 )); then echo 'usage: probe-rv64i-asm-origin.sh MINIC_CC' >&2; exit 64; fi
minic_cc="$1"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/probe.i" <<'EOF'
long test(long a, long b) {
    long c = a * b;
    __asm__ __volatile__("mul t0, t1, t2");
    __asm__ __volatile__("amoadd.w t0, t1, (t2)");
    __asm__ __volatile__("csrr t0, sstatus");
    __asm__ __volatile__("fence.i");
    return c;
}
EOF
# The normal strict profile MUST keep rejecting arbitrary inline asm.
if "$minic_cc" --target=riscv64-linux -march=rv64i -mabi=lp64 \
  -S "$work/probe.i" -o "$work/unqualified.s" >/dev/null 2>"$work/strict.err"; then
    echo "RV64I_ORIGIN_PROBE=FAIL normal strict mode admitted user inline asm" >&2
    exit 1
fi
grep -q 'inline assembly is not yet ISA-qualified' "$work/strict.err"
mkdir -p "$work/inputs"
MINIC_RV64I_ASM_AUDIT=1 "$minic_cc" --target=riscv64-linux \
  -march=rv64i -mabi=lp64 -S "$work/probe.i" -o "$work/inputs/probe.s"
python3 - "$work/inputs/probe.s" <<'PY'
import importlib.util
from pathlib import Path
import sys
spec = importlib.util.spec_from_file_location("origin_audit",
    "tools/ci/audit-rv64i-asm-origin.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
path = Path(sys.argv[1])
fast, reference = module.inspect(path), module.inspect_reference(path)
for key in ("compiler", "inline", "inline_regions", "marker_errors",
            "unverified_directives", "compiler_non_i"):
    assert fast[key] == reference[key], (key, fast[key], reference[key])
print("RV64I_ORIGIN_FAST_REFERENCE=PASS")
PY
python3 tools/ci/audit-rv64i-asm-origin.py --root "$work/inputs" \
  --json-out "$work/valid.json" --require-markers \
  --fail-on-compiler-non-i --expected-files 1
python3 - "$work/valid.json" <<'PY'
import json, sys
s=json.load(open(sys.argv[1]))["summary"]
assert s["inline_regions"] == 4, s
assert s["compiler_non_i"] == 0, s
for ext in ("M", "A", "Zicsr", "Zifencei"):
    assert s["inline"][ext] >= 1, (ext, s)
assert s["compiler"]["I"] >= 1, s
PY
# Test a failure intentionally placed outside marked provenance.
sed '1i\mul a0,a1,a2' "$work/inputs/probe.s" >"$work/inputs/invalid.s"
if python3 tools/ci/audit-rv64i-asm-origin.py --root "$work/inputs" \
  --json-out "$work/invalid.json" --fail-on-compiler-non-i >/dev/null 2>&1; then
    echo "RV64I_ORIGIN_PROBE=FAIL scanner missed compiler-origin MUL" >&2
    exit 1
fi
echo "RV64I_ORIGIN_PROBE=PASS asm_regions=4 compiler_code=rv64i fail_closed=1"
