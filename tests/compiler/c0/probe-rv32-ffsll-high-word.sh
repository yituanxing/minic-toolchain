#!/bin/sh
set -eu
: "${MINIC_CC:?MINIC_CC path required}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

${CC:-cc} -E -P -x c "$root/tests/compiler/c0/rv32_ffsll_high_word.c" \
    -o "$work/ffsll.i"
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
    -S "$work/ffsll.i" -o "$work/rv32.s"
# The physical MachineIR selector no longer uses the legacy _rv32_ctz64_*
# loop labels or fixed t0/t3/t4 registers. Verify the relevant RV32I
# semantics within the actual ffsll body, independent of register assignment:
# consume ABI a1 (upper 32 bits), add 32 iff the low half is zero, select
# the scan word with complementary masks, then perform the trailing-zero scan.
awk '
    /^ffsll_high_word:$/ { body = 1 }
    body { print }
    body && /^\.size ffsll_high_word,/ { exit }
' "$work/rv32.s" > "$work/rv32-ffsll-body.s"
check_rv32() {
    if ! grep -Eq "$1" "$work/rv32-ffsll-body.s"; then
        echo "RV32_FFSLL=FAIL missing typed RV32I $2" >&2
        exit 1
    fi
}
check_rv32 '^[[:space:]]*mv x[0-9]+, x11$' 'upper-half ABI input'
check_rv32 '^[[:space:]]*slli x[0-9]+, x[0-9]+, 5$' 'upper-half 32-bit offset'
check_rv32 '^[[:space:]]*snez x[0-9]+, x[0-9]+$' 'lower-half nonzero selector'
check_rv32 '^[[:space:]]*sub x[0-9]+, x0, x[0-9]+$' 'complementary select mask'
check_rv32 '^[[:space:]]*not x[0-9]+, x[0-9]+$' 'inverse select mask'
check_rv32 '^[[:space:]]*and x[0-9]+, x[0-9]+, x[0-9]+$' 'masked half selection'
check_rv32 '^[[:space:]]*srl x[0-9]+, x[0-9]+, x[0-9]+$' 'trailing-zero scan'

"$MINIC_CC" --target=x86_64-linux -S "$work/ffsll.i" -o "$work/x86.s"
${CC:-cc} -no-pie "$work/x86.s" -o "$work/x86"
"$work/x86"
echo 'RV32_FFSLL=PASS high_32_bit_preserved=1 typed_rv32i_scan=1 x86_native_oracle=1'
