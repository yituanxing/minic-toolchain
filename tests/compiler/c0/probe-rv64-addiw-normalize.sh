#!/bin/sh
# RV64I signed-i32 extension is one ADDIW, while unsigned Core i32 remains
# zero-extended and RV32I must never receive an RV64 word instruction.
set -eu
: "${MINIC_CC:?MINIC_CC path required}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat > "$work/normalize.i" <<'SRC'
int signed32_from_long(long value) { return (int)value; }
unsigned long unsigned32_to_long(unsigned int value) { return value; }
unsigned int unsigned32_return(unsigned int value) { return value; }
signed char signed8_from_long(long value) { return (signed char)value; }
SRC
"$MINIC_CC" --target=riscv64-linux -march=rv64i -mabi=lp64 \
    -S "$work/normalize.i" -o "$work/rv64.s"
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
    -S "$work/normalize.i" -o "$work/rv32.s"
test -s "$work/rv64.s"
test -s "$work/rv32.s"
awk '/^signed32_from_long:/{inside=1} inside{print} /^\.size signed32_from_long,/{if(inside)exit}' \
    "$work/rv64.s" > "$work/signed32.s"
awk '/^unsigned32_to_long:/{inside=1} inside{print} /^\.size unsigned32_to_long,/{if(inside)exit}' \
    "$work/rv64.s" > "$work/unsigned32.s"
awk '/^signed8_from_long:/{inside=1} inside{print} /^\.size signed8_from_long,/{if(inside)exit}' \
    "$work/rv64.s" > "$work/signed8.s"
grep -Eq '^[[:space:]]+addiw[[:space:]]' "$work/signed32.s"
! grep -Eq '^[[:space:]]+srai[[:space:]].*,[[:space:]]*32$' "$work/signed32.s"
# Core's unsigned 32-bit widening cannot use sign-extending ADDIW; its
# signedness is different from the psABI's outgoing unsigned-i32 convention.
grep -Eq '^[[:space:]]+srli[[:space:]].*,[[:space:]]*32$' "$work/unsigned32.s"
! grep -Eq '^[[:space:]]+addiw[[:space:]]' "$work/unsigned32.s"
grep -Eq '^[[:space:]]+srai[[:space:]].*,[[:space:]]*56$' "$work/signed8.s"
if grep -Eq '^[[:space:]]+addiw[[:space:]]' "$work/rv32.s"; then
    echo 'RV64_ADDIW_NORMALIZE=FAIL RV32I_contains_ADDIW' >&2
    exit 1
fi
echo 'RV64_ADDIW_NORMALIZE=PASS signed_i32=ADDIW unsigned_core_i32=zero_extend signed_i8=shift_pair rv32i=no_ADDIW'
