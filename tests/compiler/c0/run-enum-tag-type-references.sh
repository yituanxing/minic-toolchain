#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-enum-tag-type-references
assembly="$work/enum_tag_type_references.s"

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/enum_tag_type_references.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$assembly"
test -s "$assembly"
grep -F 'normalize_state:' "$assembly" >/dev/null
printf '%s\n' 'PASS compiler/c0/enum_tag_type_references definition=tagged reference=parameter+return top-level-bare-return=1 representation=int unknown-tag=reject-by-registry'
