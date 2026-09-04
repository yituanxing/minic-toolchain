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
require_core_conditional_edges "$work/call-argument.strict.s"
grep -F '  call consume_word' "$work/call-argument.strict.s" >/dev/null

printf '%s\n' 'PASS compiler/c0/record_conditional_materialization record-rvalue=conditional producers=address,call sink=assignment,call-argument strict-call-argument=1'
