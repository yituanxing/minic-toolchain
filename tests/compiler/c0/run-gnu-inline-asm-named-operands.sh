#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-inline-asm-named-operands
assembly="$work/gnu_inline_asm_named_operands.s"

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_inline_asm_named_operands.c" \
    -o "$work/gnu_inline_asm_named_operands.i"
"$minic" -S "$work/gnu_inline_asm_named_operands.i" -o "$assembly"

test -s "$assembly"
# GNU named operands define register relationships, not particular temporary
# names. Validate the complete lr/beq/add/sc loop and early-clobber separation
# against the actual registers chosen by RV64's shared allocation plan.
awk '
/MINIC_RV64I_ASM_BEGIN/ { in_asm = 1; next }
/MINIC_RV64I_ASM_END/ { in_asm = 0 }
!in_asm { next }
{
    line = $0
    gsub(/[(),]/, " ", line)
    sub(/^[[:space:]]+/, "", line)
    n = split(line, word, /[[:space:]]+/)
    if (n >= 4 && word[1] == "0:" && word[2] == "lr.w") {
        previous = word[3]
        address = word[4]
        have_lr++
    } else if (n == 4 && word[1] == "beq") {
        if (word[2] != previous || word[4] != "1f") bad++
        upper = word[3]
        have_beq++
    } else if (n == 4 && word[1] == "add") {
        if (word[3] != previous) bad++
        retry = word[2]
        increment = word[4]
        have_add++
    } else if (n >= 4 && word[1] == "sc.w.rl") {
        if (word[2] != retry || word[3] != retry || word[4] != address) bad++
        have_sc++
    } else if (n == 3 && word[1] == "bnez") {
        if (word[2] != retry || word[3] != "0b") bad++
        have_bnez++
    }
}
END {
    if (have_lr != 1 || have_beq != 1 || have_add != 1 ||
        have_sc != 1 || have_bnez != 1 || bad ||
        previous == "" || retry == "" || address == "" ||
        increment == "" || upper == "" ||
        previous == retry || previous == increment || previous == upper ||
        retry == increment || retry == upper ||
        address == previous || address == retry ||
        address == increment || address == upper) exit 1
}' "$assembly"
if grep -F '%[' "$assembly" >/dev/null; then
    printf '%s\n' 'unexpected named GNU asm placeholder in emitted assembly' >&2
    exit 1
fi
printf '%s\n' 'PASS compiler/c0/gnu_inline_asm_named_operands names=p,rc,c,a,u early-clobber==&r address=+A input=r register-identity=PASS target=RV64'
