#!/usr/bin/env bash
# Apt-free RV64I strict positive/negative gate, optional independent GNU AS.
set -Eeuo pipefail
: "$MINIC_CC"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cat >"$tmp/add.i" <<'SRC'
long add(long a, long b) { return a + b; }
long test(long a) { return add(a, 17) - 4; }
SRC
"$MINIC_CC" --target=riscv64-linux -march=rv64i -mabi=lp64 \
    -S "$tmp/add.i" -o "$tmp/add.s"
test -s "$tmp/add.s"
"$MINIC_CC" --target=riscv64-linux -S "$tmp/add.i" -o "$tmp/default.s"
"$MINIC_CC" --target=riscv64-linux -mabi=lp64d \
    -S "$tmp/add.i" -o "$tmp/default-explicit.s"
cmp "$tmp/default.s" "$tmp/default-explicit.s"
cat >"$tmp/mul.i" <<'SRC'
long multiply(long a, long b) { return a * b; }
SRC
if "$MINIC_CC" --target=riscv64-linux -march=rv64i -mabi=lp64 \
    -S "$tmp/mul.i" -o "$tmp/mul.s" >"$tmp/mul.out" 2>"$tmp/mul.err"; then
  echo "RV64I_STRICT=FAIL hardware_mul_accepted" >&2; exit 1
fi
grep -Eq "instruction '(mul|mulh|mulhu)' requires M" "$tmp/mul.err"
test ! -e "$tmp/mul.s"
cat >"$tmp/float.i" <<'SRC'
double addf(double a, double b) { return a + b; }
SRC
if "$MINIC_CC" --target=riscv64-linux -march=rv64i -mabi=lp64 \
    -S "$tmp/float.i" -o "$tmp/float.s" >"$tmp/float.out" 2>"$tmp/float.err"; then
  echo "RV64I_STRICT=FAIL hardware_fp_accepted" >&2; exit 1
fi
grep -q 'requires F/D' "$tmp/float.err"
test ! -e "$tmp/float.s"
if "$MINIC_CC" --target=riscv64-linux -march=rv64i -mabi=lp64d \
    -S "$tmp/add.i" -o "$tmp/invalid.s" >"$tmp/invalid.out" 2>"$tmp/invalid.err"; then
  echo "RV64I_STRICT=FAIL incompatible_abi" >&2; exit 1
fi
grep -q 'rv64i requires -mabi=lp64' "$tmp/invalid.err"
if "$MINIC_CC" --target=riscv64-linux -march=rv64im -mabi=lp64 \
    -S "$tmp/add.i" -o "$tmp/invalid-im.s" >"$tmp/im.out" 2>"$tmp/im.err"; then
  echo "RV64I_STRICT=FAIL unqualified_isa" >&2; exit 1
fi
grep -q 'only -march=rv64i is qualified' "$tmp/im.err"
if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
  riscv64-linux-gnu-gcc -march=rv64i -mabi=lp64 -c \
      "$tmp/add.s" -o "$tmp/add.o"
  test -s "$tmp/add.o"
  echo "RV64I_STRICT_AS=PASS target=rv64i abi=lp64"
fi
echo "RV64I_STRICT=PASS positive=1 rejected_mul=1 rejected_fp=1 rejected_abi=1 rejected_unqualified_isa=1 legacy_byte_exact=1"
