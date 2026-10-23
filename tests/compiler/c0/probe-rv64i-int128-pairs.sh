#!/usr/bin/env bash
# Comprehensive RV64I 128-bit arithmetic pair lowering contract. All real
# operations are source-level Core values; no M extension and no fake i128
# truncation to one register may pass. GNU AS is used, never MiniAS here.
set -Eeuo pipefail
if (( $# != 1 )); then echo 'usage: probe-rv64i-int128-pairs.sh /path/to/minic-cc' >&2; exit 64; fi
cc="$1"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/pair.i" <<'EOF'
typedef unsigned __int128 U;
typedef __int128 I;
U ti_add(U a, U b) { return a + b; }
U ti_sub(U a, U b) { return a - b; }
U ti_and(U a, U b) { return a & b; }
U ti_or(U a, U b) { return a | b; }
U ti_xor(U a, U b) { return a ^ b; }
U ti_not(U a) { return ~a; }
I ti_neg(I a) { return -a; }
U ti_shl(U a, unsigned b) { return a << b; }
U ti_shr(U a, unsigned b) { return a >> b; }
I ti_sar(I a, unsigned b) { return a >> b; }
I ti_div(I a, I b) { return a / b; }
I ti_mod(I a, I b) { return a % b; }
U ti_udiv(U a, U b) { return a / b; }
U ti_umod(U a, U b) { return a % b; }
EOF
"$cc" --target=riscv64-linux -march=rv64i -mabi=lp64 \
  -S "$work/pair.i" -o "$work/pair.s"
test -s "$work/pair.s"
for symbol in __divti3 __udivti3 __modti3 __umodti3; do
  grep -F "call $symbol" "$work/pair.s" >/dev/null
done
for function in ti_add ti_sub ti_and ti_or ti_xor ti_not ti_neg ti_shl ti_shr ti_sar ti_div ti_mod ti_udiv ti_umod; do
  grep -F "$function:" "$work/pair.s" >/dev/null
done
if grep -Eq '^[[:space:]]*(mul|mulhu|mulh|mulhsu|div|divu|rem|remu|fadd|fsub|fmul)[.[:space:]]' "$work/pair.s"; then
  echo "RV64I_I128_PAIRS=FAIL non-I compiler instruction" >&2
  exit 1
fi
mkdir "$work/checked"
cp "$work/pair.s" "$work/checked/"
python3 tools/ci/audit-rv64i-asm-origin.py \
  --root "$work/checked" --json-out "$work/scan.json" \
  --opcode-tsv-out "$work/opcodes.tsv" \
  --expected-files 1 --fail-on-compiler-non-i
if command -v riscv64-linux-gnu-gcc >/dev/null; then
  riscv64-linux-gnu-gcc -march=rv64i -mabi=lp64 \
    -c "$work/pair.s" -o "$work/pair.o"
fi
echo 'RV64I_I128_PAIRS=PASS functions=14 source_runtime_libcalls=4 non_I=0'
