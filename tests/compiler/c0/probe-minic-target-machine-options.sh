#!/bin/sh
# Keep target ISA/ABI option semantics in registered target policy, not CLI.
set -eu
: "${MINIC_CC:?MINIC_CC must identify built minic-cc}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

${HOST_CC:-cc} -std=gnu11 -E -P -x c \
    "$root/tests/compiler/c0/static_array_redeclaration.c" \
    -o "$work/smoke.i"

"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
    -S "$work/smoke.i" -o "$work/rv32.s"
"$MINIC_CC" --target=riscv64-linux -march=rv64i -mabi=lp64 \
    -S "$work/smoke.i" -o "$work/rv64i.s"
"$MINIC_CC" --target=riscv64-linux -march=rv64imafd -mabi=lp64d \
    -S "$work/smoke.i" -o "$work/rv64d.s"
"$MINIC_CC" --target=x86_64-linux -S "$work/smoke.i" -o "$work/x86.s"
for target in rv32 rv64i rv64d x86; do
    test -s "$work/$target.s"
done

if "$MINIC_CC" --target=x86_64-linux -march=rv64i -mabi=lp64 \
    -S "$work/smoke.i" -o "$work/bad-x86.s" 2>"$work/bad-x86.err"; then
    echo 'MACHINE_OPTIONS=FAIL cross-family option accepted' >&2
    exit 1
fi
grep -F -- '-march/-mabi currently require RISC-V' "$work/bad-x86.err" >/dev/null
test ! -e "$work/bad-x86.s"

if "$MINIC_CC" --target=riscv32-linux -march=rv64i -mabi=ilp32 \
    -S "$work/smoke.i" -o "$work/bad-rv32.s" 2>"$work/bad-rv32.err"; then
    echo 'MACHINE_OPTIONS=FAIL RV32 XLEN mismatch accepted' >&2
    exit 1
fi
grep -F 'invalid RISC-V configuration' "$work/bad-rv32.err" >/dev/null
test ! -e "$work/bad-rv32.s"

"$MINIC_CC" --help > "$work/help.txt"
grep -F -- '-march=<target-isa>' "$work/help.txt" >/dev/null
if grep -Eq 'target/riscv/|MINIC_TARGET_ISA_FAMILY_|\.riscv_isa' \
    "$root/tools/minic-cc/main.c"; then
    echo 'MACHINE_OPTIONS=FAIL target ISA leaked into driver' >&2
    exit 1
fi
echo 'MINIC_MACHINE_OPTIONS=PASS cli_target_opaque=1 rv32=1 rv64i=1 rv64d=1 x86=1 invalid=2'
