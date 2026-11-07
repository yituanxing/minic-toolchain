#!/bin/sh
set -eu

: "${MINIC_CC:?MINIC_CC must name the compiler under test}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

for case_id in 0 1 2 3 4 5 6 7; do
    cc -std=gnu11 -E -P -DP24_CASE="$case_id" -x c \
        "$root/tests/frontend/p24_asm_goto_cleanup_cases.c" >"$work/case.i"
    if [ "$case_id" -eq 0 ] || [ "$case_id" -eq 6 ] || [ "$case_id" -eq 7 ]; then
        "$MINIC_CC" --target=riscv64-linux -S "$work/case.i" -o "$work/ok.s"
        grep -Fq '.word 33' "$work/ok.s"
        continue
    fi
    if "$MINIC_CC" --target=riscv64-linux -S "$work/case.i" \
            -o "$work/bad.s" 2>"$work/fail.log"; then
        echo "FAIL P24 cleanup-crossing label case $case_id admitted" >&2
        exit 1
    fi
    if [ "$case_id" -eq 4 ]; then
        grep -Fq 'goto cannot enter a different GNU cleanup lifetime' "$work/fail.log"
    else
        grep -Fq 'GNU asm goto cannot cross a GNU cleanup lifetime' "$work/fail.log"
    fi
    test ! -e "$work/bad.s"
done
echo 'MINIC_FRONTEND_ASM_GOTO_LABEL_LIFETIME=PASS same_scope_forward_local_backward=3 invalid_enter_leave_local_backward=5'
