#!/usr/bin/env bash
# GCC supports GNU asm goto with output operands. MiniC's frontend must
# preserve this source fact even before target-specific usercopy exception
# table machine semantics have been implemented. Fail at Core, NOT by
# dropping the output or rejecting a valid source grammar production.
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { printf '%s\n' "X86_ASM_GOTO_OUTPUT=FAIL $*" >&2; exit 1; }

cat >"$work/goto.c" <<'SRC'
int original_usercopy_read(const int *address) {
    int value = 0;
    __asm__ volatile goto(
        "1: movl %[memory],%[output]\n"
        ".pushsection __ex_table,\"a\"\n"
        ".balign 4\n"
        ".long (1b) - .\n"
        ".long (%l[fault]) - .\n"
        ".popsection\n"
        : [output] "=r"(value)
        : [memory] "m"(*address)
        :
        : fault);
    return value;
fault:
    return -1;
}
SRC
"$host_cc" -E -P -std=gnu11 -x c "$work/goto.c" -o "$work/goto.i"
"$host_cc" -std=gnu11 -c "$work/goto.c" -o "$work/gnu.o" \
    || fail 'GNU GCC does not accept reference asm goto form'
if "$minic" --target=x86_64-linux -S "$work/goto.i" \
       -o "$work/mini.s" >"$work/stdout" 2>"$work/stderr"; then
    fail 'unsupported exception-table asm goto emitted without Core semantics'
fi
test ! -e "$work/mini.s" || fail 'failed Core left partial x86 assembly'
grep -Eq "Core IR does not yet support function 'original_usercopy_read'" \
    "$work/stderr" || {
    sed -n '1,25p' "$work/stderr" >&2
    fail 'source grammar failed before typed Core refusal'
}
if grep -E 'GNU asm goto currently requires no outputs|parsed AST contract failed' \
      "$work/stderr"; then
    fail 'old invalid source-grammar/AST barrier still active'
fi
printf '%s\n' 'X86_ASM_GOTO_OUTPUT_SOURCE=PASS GNU_parse=1 outputs_preserved=1 labels_preserved=1 Core_fail_closed=1 GNU_exception_table=1'
