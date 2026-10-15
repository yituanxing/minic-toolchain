#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-local-fixed-array

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/static_local_fixed_array.c" \
    -o "$work/static_local_fixed_array.i"
"$minic" -S "$work/static_local_fixed_array.i" \
    -o "$work/static_local_fixed_array.s"

grep -F '.section .rodata' "$work/static_local_fixed_array.s" >/dev/null
grep -F '.type __minic_static_local_' "$work/static_local_fixed_array.s" >/dev/null
grep -F '  .byte 1' "$work/static_local_fixed_array.s" >/dev/null
grep -F '  .byte 2' "$work/static_local_fixed_array.s" >/dev/null
grep -F '  .byte 3' "$work/static_local_fixed_array.s" >/dev/null
# The shared static-storage initializer owner may materialize implicit tail zeroes
# as explicit typed slots instead of relying on the emitter's .zero compression.
# Keep this gate semantic: both spellings represent the same two-byte zero tail.
if ! grep -F '  .zero 2' "$work/static_local_fixed_array.s" >/dev/null; then
    explicit_zero_count=$(grep -c '^  \.byte 0$' "$work/static_local_fixed_array.s" || true)
    if [ "$explicit_zero_count" -lt 2 ]; then
        echo 'fixed static local array is missing its two-byte zero tail' >&2
        cat "$work/static_local_fixed_array.s" >&2
        exit 1
    fi
fi
if grep -F '.globl __minic_static_local_' "$work/static_local_fixed_array.s" >/dev/null; then
    echo 'fixed static local array leaked external linkage' >&2
    exit 1
fi
printf '%s\n' 'PASS compiler/c0/static_local_fixed_array bound=5 initializers=3 zero-fill=2 storage=internal-rodata'
