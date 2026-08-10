#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-extern-incomplete-record-object
assembly="$work/extern_incomplete_record_object.s"

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/extern_incomplete_record_object.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$assembly"
test -s "$assembly"
grep -F '.extern opaque_object' "$assembly" >/dev/null
grep -F 'opaque_address:' "$assembly" >/dev/null
printf '%s\n' 'PASS compiler/c0/extern_incomplete_record_object forward-record=1 extern-object=declaration-only addressable=1 storage=none'
