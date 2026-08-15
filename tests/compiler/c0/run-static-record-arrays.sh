#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-record-array

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/static_record_array.c" \
    -o "$work/static_record_array.i"
"$minic" -S "$work/static_record_array.i" \
    -o "$work/static_record_array.s"

grep -Fx '.section .rodata' "$work/static_record_array.s" >/dev/null
grep -F '.type priority, @object' "$work/static_record_array.s" >/dev/null
grep -F '.size priority, 6' "$work/static_record_array.s" >/dev/null
grep -F '  .byte 10' "$work/static_record_array.s" >/dev/null
grep -F '  .byte 6' "$work/static_record_array.s" >/dev/null
grep -F '  .byte 4' "$work/static_record_array.s" >/dev/null
if grep -F '.globl priority' "$work/static_record_array.s" >/dev/null; then
    echo 'static record array leaked external linkage' >&2
    exit 1
fi
grep -F 'read_priority:' "$work/static_record_array.s" >/dev/null
grep -F '.type sched_core_sysctls_like, @object' "$work/static_record_array.s" >/dev/null
grep -F '.size sched_core_sysctls_like, 32' "$work/static_record_array.s" >/dev/null
if grep -F '.globl sched_core_sysctls_like' "$work/static_record_array.s" >/dev/null; then
    echo 'complex static record array leaked external linkage' >&2
    exit 1
fi
grep -F 'read_sysctl_size:' "$work/static_record_array.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/static_record_array inferred-count=3 fields=2 missing-field-zero=1 size=6 complex-empty=1 complex-size=32 shared-owner=1 internal-rodata=1'
