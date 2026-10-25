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
int bit_clz(U a) { return __builtin_clzll(a); }
int bit_ctz(U a) { return __builtin_ctzll(a); }
SRC
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
  -S "$work/rv32.i" -o "$work/rv32.s"
test -s "$work/rv32.s"
for pattern in \
  'sltu t5, t4, t0' 'sltu t5, t0, t2' \
  'snez t4, t0' 'not t0, t0' 'not t1, t1' \
  'xor t0, t0, t2' 'and t1, t1, t3' 'or t1, t1, t3' \
  '_rv32_clz_loop_' '_rv32_ctz_loop_' 'add t1, t1, t4'; do
  grep -Fq "$pattern" "$work/rv32.s" || {
    echo "RISCV_SHARED_WORD_OPS=FAIL rv32-missing:$pattern" >&2
    exit 1
  }
done

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
int bit_clz(unsigned long a) { return __builtin_clzl(a); }
int bit_ctz(unsigned long a) { return __builtin_ctzl(a); }
SRC
"$MINIC_CC" --target=riscv64-linux -march=rv64i -mabi=lp64 \
  -S "$work/rv64.i" -o "$work/rv64.s"
test -s "$work/rv64.s"
for pattern in \
  'sltu t5, t4, t0' 'sltu t5, t0, t2' \
  'snez t4, t0' 'not t0, t0' 'not t1, t1' \
  'xor t0, t0, t2' 'and t1, t1, t3' 'or t1, t1, t3' \
  '_core_clz_loop_' '_core_ctz_loop_'; do
  grep -Fq "$pattern" "$work/rv64.s" || {
    echo "RISCV_SHARED_WORD_OPS=FAIL rv64-missing:$pattern" >&2
    exit 1
  }
done
if grep -Eq '^[[:space:]]+(mul|mulw|div|rem|clz|ctz)[[:space:]]' \
   "$work/rv32.s" "$work/rv64.s"; then
  echo "RISCV_SHARED_WORD_OPS=FAIL unexpected non-I instruction" >&2
  exit 1
fi
echo 'RISCV_SHARED_WORD_OPS=PASS rv32=64bit-pair rv64=128bit-pair clz_ctz=xlen-based'
