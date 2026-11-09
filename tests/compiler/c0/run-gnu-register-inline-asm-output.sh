#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-register-inline-asm-output
assembly="$work/gnu_register_inline_asm_output.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_register_inline_asm_output.c" \
    -o "$work/gnu_register_inline_asm_output.i"
"$minic" -S "$work/gnu_register_inline_asm_output.i" -o "$assembly"

test -s "$assembly"
grep -F 'read_cycle_like:' "$assembly" >/dev/null
grep -F 'call read_cycle_like' "$assembly" >/dev/null
# Only GNU's =r identity matters: the chosen target register must receive
# the CSR value and then be written back through the Core local address.
awk '
/^read_cycle_like:$/ { in_function = 1; next }
/^\.size read_cycle_like,/ { in_function = 0 }
!in_function { next }
/MINIC_RV64I_ASM_END/ { after_asm = 1; next }
$1 == "csrr" {
    reg = $2
    sub(/,$/, "", reg)
    if ($3 != "0xc01" || reg !~ /^(a[0-7]|t[0-6]|s([0-9]|10|11))$/) bad++
    found++
}
after_asm && $1 == "sd" && $2 == reg "," && $3 ~ /^0\([a-z][0-9]+\)$/ {
    stored++
}
END { if (found != 1 || stored != 1 || bad) exit 1 }
' "$assembly"
if grep -F '%0' "$assembly" >/dev/null; then
    printf '%s\n' 'unsubstituted GNU asm operand placeholder in emitted assembly' >&2
    exit 1
fi

printf '%s\n' 'PASS compiler/c0/gnu_register_inline_asm_output storage=register output==r placeholder=%0 register-identity=PASS writeback=local memory-clobber=1 reachable=YES'
