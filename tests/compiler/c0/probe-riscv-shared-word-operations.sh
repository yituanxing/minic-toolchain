#!/bin/sh
# Apt-free end-to-end Core -> RISC-V family word-operation lowering.
# Existing GNU/QEMU owner acceptance remains the final certification.
set -eu
: "${MINIC_CC:?set MINIC_CC}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/rv32.i" <<'SRC'
typedef unsigned long long U;
typedef long long I;
U pair_add(U a, U b) { return a + b; }
U pair_sub(U a, U b) { return a - b; }
U pair_xor(U a, U b) { return a ^ b; }
U pair_and(U a, U b) { return a & b; }
U pair_or(U a, U b) { return a | b; }
U pair_not(U a) { return ~a; }
I pair_neg(I a) { return -a; }
U pair_shl(U a, unsigned k) { return a << k; }
U pair_shr(U a, unsigned k) { return a >> k; }
I pair_sar(I a, unsigned k) { return a >> k; }
U pair_shl_wide_count(U a, U k) { return a << k; }
int bit_clz(U a) { return __builtin_clzll(a); }
int bit_ctz(U a) { return __builtin_ctzll(a); }
SRC
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
  -S "$work/rv32.i" -o "$work/rv32.s"
test -s "$work/rv32.s"
# MachineIR is the only RV32/RV64 emitter. Inspect each selected function's
# exact Base-I physical register forms (numeric xN and equivalent psABI
# spellings), without tying correctness to removed direct-emitter loop labels.
# The current bit scans and 2*XLEN shifts are deliberately branch-free.
require_form() {
  target=$1
  fn=$2
  form=$3
  # POSIX awk reserves `function`; choose an ordinary selector name.
  awk -v selected_fn="$fn" '
    $0 == selected_fn ":" { found = 1 }
    found { print }
    found && $1 == ".size" && $2 == selected_fn "," { exit }
  ' "$work/$target.s" | grep -Eq "$form" || {
    printf 'RISCV_SHARED_WORD_OPS=FAIL %s function=%s missing_MachineIR_form=%s\n' \
      "$target" "$fn" "$form" >&2
    exit 1
  }
}
require_function_set() {
  target=$1
  for fn in pair_add pair_sub pair_xor pair_and pair_or pair_not pair_neg \
            pair_shl pair_shr pair_sar bit_clz bit_ctz; do
    count=$(grep -Ec "^$fn:$" "$work/$target.s" || :)
    if [ "$count" -ne 1 ]; then
      printf 'RISCV_SHARED_WORD_OPS=FAIL %s function=%s count=%s\n' \
        "$target" "$fn" "$count" >&2
      exit 1
    fi
  done
}
require_pair_forms() {
  target=$1
  require_function_set "$target"
  # Wide ADD carry, SUB borrow and both 2*XLEN bitwise lanes.
  require_form "$target" pair_add '^  sltu (x30|t5), (x29|t4), (x5|t0)$'
  require_form "$target" pair_add '^  add (x6|t1), (x6|t1), (x30|t5)$'
  require_form "$target" pair_sub '^  sltu (x30|t5), (x5|t0), (x7|t2)$'
  require_form "$target" pair_xor '^  xor (x5|t0), (x5|t0), (x7|t2)$'
  require_form "$target" pair_and '^  and (x6|t1), (x6|t1), (x28|t3)$'
  require_form "$target" pair_or '^  or (x6|t1), (x6|t1), (x28|t3)$'
  require_form "$target" pair_not '^  not (x5|t0), (x5|t0)$'
  require_form "$target" pair_not '^  not (x6|t1), (x6|t1)$'
  require_form "$target" pair_neg '^  snez (x7|t2), (x5|t0)$'
  # Select the [0,XLEN), [XLEN,2*XLEN), >=2*XLEN cases with
  # register comparisons/masks, not deleted _wide_ge* branch islands.
  require_form "$target" pair_shl '^  sltu (x12|a2), (x7|t2), (x14|a4)$'
  require_form "$target" pair_shr '^  sltu (x13|a3), (x7|t2), (x14|a4)$'
  require_form "$target" pair_sar '^  sra (x16|a6), (x6|t1), (x10|a0)$'
  # CLZ/CTZ use an unrolled bit-search with Core SSA, not stale named loops.
  require_form "$target" bit_clz '^  srl (x5|t0), (x5|t0), (x28|t3)$'
  require_form "$target" bit_ctz '^  srl (x5|t0), (x5|t0), (x28|t3)$'
}
require_pair_forms rv32
test "$(grep -Ec '^pair_shl_wide_count:$' "$work/rv32.s" || :)" -eq 1
# A two-word shift count must mask out all counts with nonzero upper word.
require_form rv32 pair_shl_wide_count '^  seqz (x14|a4), (x30|t5)$'

cat > "$work/rv64.i" <<'SRC'
typedef unsigned __int128 U;
typedef __int128 I;
U pair_add(U a, U b) { return a + b; }
U pair_sub(U a, U b) { return a - b; }
U pair_xor(U a, U b) { return a ^ b; }
U pair_and(U a, U b) { return a & b; }
U pair_or(U a, U b) { return a | b; }
U pair_not(U a) { return ~a; }
I pair_neg(I a) { return -a; }
U pair_shl(U a, unsigned k) { return a << k; }
U pair_shr(U a, unsigned k) { return a >> k; }
I pair_sar(I a, unsigned k) { return a >> k; }
int bit_clz(unsigned long a) { return __builtin_clzl(a); }
int bit_ctz(unsigned long a) { return __builtin_ctzl(a); }
SRC
"$MINIC_CC" --target=riscv64-linux -march=rv64i -mabi=lp64 \
  -S "$work/rv64.i" -o "$work/rv64.s"
test -s "$work/rv64.s"
require_pair_forms rv64
if grep -Eq '^[[:space:]]+(mul|mulw|div|rem|clz|ctz)[[:space:]]' \
   "$work/rv32.s" "$work/rv64.s"; then
  echo "RISCV_SHARED_WORD_OPS=FAIL unexpected non-I instruction" >&2
  exit 1
fi
echo 'RISCV_SHARED_WORD_OPS=PASS rv32=64bit-pair rv64=128bit-pair clz_ctz=xlen-based shifts=zero_1_limb_2limb_highcount'
