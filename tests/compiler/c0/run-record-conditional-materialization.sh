#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/record-conditional-materialization

mkdir -p "$work"

require_core_conditional_edges() {
    file=$1
    if ! grep -E '^[[:space:]]+b(eq|ne)z[[:space:]]+[^,]+,[[:space:]]*(1f|\.L[^[:space:]]+_core_bb[0-9]+)$' "$file" >/dev/null; then
        printf '%s\n' "FAIL record-conditional missing conditional edge: $file" >&2
        exit 1
    fi
    if ! grep -E '^[[:space:]]+j[[:space:]]+\.L[^[:space:]]+_core_bb[0-9]+$' "$file" >/dev/null; then
        grep -E '^[[:space:]]+lla[[:space:]]+t6,[[:space:]]*\.L[^[:space:]]+_core_bb[0-9]+$' "$file" >/dev/null
        grep -F '  jalr zero, t6, 0' "$file" >/dev/null
    fi
}

"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/record_conditional_materialization.c" \
    -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"

minic_cc=$(dirname "$minic")/minic-cc
"$minic_cc" --target=x86_64-linux -S "$work/input.i" \
    -o "$work/record-conditional.x86.s"
"$host_cc" -no-pie "$work/record-conditional.x86.s" \
    -o "$work/record-conditional.x86"
"$work/record-conditional.x86"

# Record conditionals are represented by Core CFG blocks. Accept either the
# direct local jump or the current far-edge lla/jalr lowering.
require_core_conditional_edges "$work/output.s"
grep -F '  call pgprot_from_va' "$work/output.s" >/dev/null
grep -F '  call consume' "$work/output.s" >/dev/null

if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
    riscv64-linux-gnu-gcc -c "$work/output.s" -o "$work/output.o"
fi

"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/record_call_argument_materialization.c" \
    -o "$work/call-argument.i"
MINIC_CORE_IR=strict "$minic" -S "$work/call-argument.i" \
    -o "$work/call-argument.strict.s"
# The call-argument case has a constant false condition. Shared Core is
# required to retain the selected record-producing CALL and its by-value
# materialization, but not a dead conditional machine edge.
test "$(grep -F -c '  call make_word' "$work/call-argument.strict.s")" -eq 1
grep -F '  call consume_word' "$work/call-argument.strict.s" >/dev/null

# Check the actual argument value under the native ABI; a source-level
# constant-branch fold must not skip make_word or pass the dead {3} arm.
"$minic_cc" --target=x86_64-linux -S "$work/call-argument.i" \
    -o "$work/call-argument.x86.s"
"$host_cc" -no-pie "$work/call-argument.x86.s" \
    "$root/tests/compiler/c0/record_conditional_consume_stub.c" \
    -o "$work/call-argument.x86"
"$work/call-argument.x86"

printf '%s\n' 'PASS compiler/c0/record_conditional_materialization record-rvalue=conditional producers=address,call sink=assignment,call-argument,field-projection strict-call-argument=1 GNU-native-value=11 both-branches-and-effects=1'
