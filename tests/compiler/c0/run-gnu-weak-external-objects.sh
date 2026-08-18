#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/gnu-weak-external-objects
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_weak_external_objects.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
grep -q '^\.weak weak_record$' "$work/output.s"
grep -q '^\.weak weak_banner$' "$work/output.s"
grep -q '^\.weak weak_defined$' "$work/output.s"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/invalid_gnu_weak_internal_object.c" -o "$work/invalid.i"
if "$minic" -S "$work/invalid.i" -o "$work/invalid.s" >"$work/invalid.out" 2>"$work/invalid.err"; then
  echo 'expected internal weak object rejection' >&2
  exit 1
fi
