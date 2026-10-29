#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-x86-security-function-attributes
fixture="$root/tests/compiler/c0/x86_security_function_attributes.c"

mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 "$fixture" -o "$work/positive.i"
"$minic" --target=x86_64-linux -S "$work/positive.i" -o "$work/positive.s"
for symbol in x86_no_endbr x86_keep_branch x86_bmi2_fallback x86_default_fallback x86_security_probe; do
    grep -F "$symbol:" "$work/positive.s" >/dev/null
done
# nocf_check explicitly opts out of ENDBR instrumentation. MiniC has no
# full-kernel IBT instrumentation today; the generated function must not
# unexpectedly gain an ENDBR prologue in this front-end-only regression.
if grep -E '^ *endbr(32|64) *$' "$work/positive.s" >/dev/null; then
    printf '%s\n' 'FAIL x86 security attribute: emitted unexpected ENDBR' >&2
    exit 1
fi

for target in riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
    if "$minic" --target="$target" -S "$work/positive.i" \
        -o "$work/rejected-$target.s" >"$work/rejected-$target.out" \
        2>"$work/rejected-$target.err"; then
        printf '%s\n' "FAIL x86 security attribute: foreign target $target accepted" >&2
        exit 1
    fi
    grep -F 'GNU x86 function-security attribute requires x86_64 target support' \
        "$work/rejected-$target.err" >/dev/null
done

for invalid in \
    REJECT_NOCF_ARGUMENT REJECT_INDIRECT_THUNK REJECT_INDIRECT_THUNK_EXTERN \
    REJECT_INDIRECT_NUMBER REJECT_TARGET_VECTOR REJECT_TARGET_ABI \
    REJECT_TARGET_NUMBER REJECT_TARGET_MIXED; do
    "$host_cc" -E -P -std=gnu11 "-D$invalid" "$fixture" -o "$work/$invalid.i"
    if "$minic" --target=x86_64-linux -S "$work/$invalid.i" \
        -o "$work/$invalid.s" >"$work/$invalid.out" \
        2>"$work/$invalid.err"; then
        printf '%s\n' "FAIL x86 security attribute: $invalid policy accepted" >&2
        exit 1
    fi
    grep -E 'GNU x86 |GNU indirect_branch|GNU nocf_check|GNU attribute has an invalid number' \
        "$work/$invalid.err" >/dev/null
done

printf '%s\n' 'PASS compiler/c0/x86_security_function_attributes positive=4 reject_foreign_targets=4 reject_unsupported_policies=8 backend_ibt_not_claimed=1'
