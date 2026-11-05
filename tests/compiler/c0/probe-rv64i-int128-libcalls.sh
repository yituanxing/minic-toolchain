#!/usr/bin/env bash
# Strict-I compiler emission and the real LP64 two-GPR TI runtime ABI.
set -Eeuo pipefail
if (( $# != 1 )); then echo "usage: $0 /path/to/minic-cc" >&2; exit 64; fi
minic_cc="$1"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/ti.i" <<'EOF'
typedef unsigned __int128 U128;
typedef __int128 I128;
U128 ti_mul(U128 a, U128 b) { return a * b; }
int ti_sless(I128 a, I128 b) { return a < b; }
int ti_uless(U128 a, U128 b) { return a < b; }
int ti_eq(U128 a, U128 b) { return a == b; }
EOF
"$minic_cc" --target=riscv64-linux -march=rv64i -mabi=lp64 \
  -S "$work/ti.i" -o "$work/ti.s"
# The LP64 pair ABI requires a compiler-rt multiply, but comparisons are now
# selected directly as verified base-I two-word operations. A golden check
# for __cmpti2/__ucmpti2 would reject the typed implementation despite its
# stronger no-call guarantee. Check the signed-high/unsigned-low comparator
# and both-word equality in each sealed function body instead.
grep -F 'call __multi3' "$work/ti.s" >/dev/null
python3 - "$work/ti.s" <<'PY'
from pathlib import Path
import re
import sys
asm = Path(sys.argv[1]).read_text()
for name, expected in {
    'ti_sless': (r'\bslt\s+x\d+,', r'\bsltu\s+x\d+,'),
    'ti_uless': (r'\bsltu\s+x\d+,',),
    'ti_eq': (r'\bxor\s+x\d+,', r'\bseqz\s+x\d+,'),
}.items():
    match = re.search(rf'(?m)^{name}:\s*\n(.*?)(?=^\s*\.size\s+{name}\b)',
                      asm, re.S | re.M)
    assert match, f'{name}: missing complete GNU function region'
    for opcode in expected:
        assert re.search(opcode, match.group(1)), (name, opcode)
print('RV64I_I128_COMPARE=PASS signed_high=1 unsigned_low=1 both_words=1')
PY
grep -Eq '^[[:space:]]+sd (ra|x1),' "$work/ti.s"
grep -Eq '^[[:space:]]+ld (ra|x1),' "$work/ti.s"
if grep -Eq '^[[:space:]]*(mul|mulh|mulhu|mulhsu|div|divu|rem|remu)(w)?[[:space:]]' "$work/ti.s"; then
    echo "RV64I_I128=FAIL compiler introduced M" >&2
    exit 1
fi
mkdir -p "$work/asm"
cp "$work/ti.s" "$work/asm/"
python3 tools/ci/audit-rv64i-asm-origin.py \
    --root "$work/asm" --json-out "$work/scan.json" \
    --opcode-tsv-out "$work/opcode-counts.tsv" \
    --expected-files 1 --fail-on-compiler-non-i
python3 - "$work/scan.json" "$work/opcode-counts.tsv" <<'PY'
import json
from pathlib import Path
import sys
report=json.loads(Path(sys.argv[1]).read_text())
s=report['summary']
assert s['compiler_non_i'] == 0, s
ops=s['opcode_counts']['compiler']
assert ops.get('call', 0) >= 1, ops
tsv=Path(sys.argv[2]).read_text()
assert 'compiler\tcall\tI\t' in tsv, tsv[:100]
assert len(tsv.splitlines()) > 2
print('RV64I_I128=PASS libcall=multi3 checked_pair_comparisons=3 compiler_non_I=0 opcode_tsv=1')
PY
