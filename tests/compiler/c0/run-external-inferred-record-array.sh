#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-external-inferred-record-array
asm="$work/external_inferred_record_array.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/external_inferred_record_array.c" \
    -o "$work/external_inferred_record_array.i"
"$minic" -S "$work/external_inferred_record_array.i" -o "$asm"

grep -F 'riscv_isa_ext:' "$asm" >/dev/null
grep -F '.size riscv_isa_ext, 48' "$asm" >/dev/null
grep -F 'kvm_stats:' "$asm" >/dev/null
grep -F '.size kvm_stats, 72' "$asm" >/dev/null
grep -F 'fixed_record_array:' "$asm" >/dev/null
grep -F '.size fixed_record_array, 24' "$asm" >/dev/null

test "$(grep -c '^  \.dword \.Lminic_string_' "$asm")" -ge 4

after_label=$(sed -n '/^kvm_stats:/,/^.size kvm_stats, 72/p' "$asm")
printf '%s\n' "$after_label" | grep -F '104' >/dev/null
printf '%s\n' "$after_label" | grep -F '97' >/dev/null

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/invalid_external_inferred_record_array_brace_elision.c" \
    -o "$work/invalid.i"
if "$minic" -S "$work/invalid.i" -o "$work/invalid.s" \
    >"$work/invalid.stdout" 2>"$work/invalid.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/invalid_external_inferred_record_array_brace_elision: unexpectedly succeeded' >&2
    exit 1
fi
grep -F 'inferred external record array requires braced record elements' \
    "$work/invalid.stderr" >/dev/null

printf '%s\n' \
    'PASS compiler/c0/external_inferred_record_array bound=shape-prepass record=nested+designated pointer-reloc=1 char-array-string=1 fixed-path=unchanged brace-elision=fail-closed'
