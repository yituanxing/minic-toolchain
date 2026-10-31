#!/bin/sh
# Test one frozen CALL ABI plan shared by admission, frame and emission.
set -eu
: "${MINIC_CC:?MINIC_CC path required}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
source="$root/tests/compiler/c0/rv64_lp64_softfloat_record_call.c"

${CC:-cc} -std=gnu11 -Werror -fsyntax-only "$source"
${CC:-cc} -E -P -std=gnu11 -x c "$source" -o "$work/record.i"
"$MINIC_CC" --target=riscv64-linux -march=rv64i -mabi=lp64 \
    -S "$work/record.i" -o "$work/soft.s"

for symbol in echo_float echo_double direct_float direct_double \
              indirect_float indirect_double main; do
    grep -Fq "$symbol:" "$work/soft.s" || {
        echo "RV64_LP64_RECORD_CALL=FAIL absent $symbol" >&2; exit 1;
    }
done
if grep -Eq '^  (fadd\.|fsub\.|fmul\.|fdiv\.|flw |fld |fsw |fsd |fmv\.)' \
    "$work/soft.s"; then
    echo 'RV64_LP64_RECORD_CALL=FAIL forbidden F/D instructions' >&2
    exit 1
fi
grep -Fq '  jalr ra, t0, 0' "$work/soft.s" || {
    echo 'RV64_LP64_RECORD_CALL=FAIL no indirect call' >&2; exit 1;
}

if "$MINIC_CC" --target=riscv64-linux -march=rv64imafd -mabi=lp64d \
    -S "$work/record.i" -o "$work/hard.s" >"$work/hard.out" 2>"$work/hard.err"; then
    echo 'RV64_LP64_RECORD_CALL=FAIL LP64D FP aggregate incorrectly accepted' >&2
    exit 1
fi
test ! -s "$work/hard.s"
grep -Fq 'cannot emit RISC-V function' "$work/hard.err"

if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
    riscv64-linux-gnu-gcc -march=rv64i -mabi=lp64 -O0 -S \
        "$source" -o "$work/gnu.s"
    test -s "$work/gnu.s"
    echo 'RV64_LP64_RECORD_GNU=PASS rv64i/lp64 GCC psABI oracle'
else
    echo 'RV64_LP64_RECORD_GNU=SKIP no riscv64-linux-gnu-gcc'
fi
echo 'RV64_LP64_RECORD_CALL=PASS -S direct=2 indirect=2 record_return=4 LP64D=fail_closed'
