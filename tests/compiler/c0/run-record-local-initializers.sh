#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-record-local-initializers

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/record_local_initializer.c" \
    -o "$work/record_local_initializer.i"
"$minic" -S \
    "$work/record_local_initializer.i" \
    -o "$work/record_local_initializer.s"

grep -F '.type copy_packet, @function' "$work/record_local_initializer.s" >/dev/null
grep -F '.type initialize_guard, @function' "$work/record_local_initializer.s" >/dev/null
grep -F '  lbu t0, 0(t2)' "$work/record_local_initializer.s" >/dev/null
grep -F '  sb t0, 0(t3)' "$work/record_local_initializer.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/record_local_initializer source=dereference whole-record-copy=yes designated=pointer+integer zero-fill=unspecified member-selector=shared multi-declarator=1 suffix-object-attribute=unused'
