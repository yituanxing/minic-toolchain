#!/bin/sh
# Verify direct case-entry and real fallthrough local-fact semantics against GCC.
set -eu
: "${MINIC:?Set MINIC to the absolute path of minic-cc}"
: "${CC:=cc}"

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$root/build/core-switch-case-fact-isolation"}
mkdir -p "$work"

source_file="$root/tests/compiler/c0/core_switch_case_fact_isolation.c"
"$CC" -std=gnu11 -O0 "$source_file" -o "$work/gnu"
"$CC" -std=gnu11 -E -P "$source_file" -o "$work/test.i"
"$MINIC" --target=x86_64-linux -S "$work/test.i" -o "$work/minic.s"
"$CC" -no-pie "$work/minic.s" -o "$work/minic"
"$work/gnu"
"$work/minic"
printf 'PASS core switch case facts (GNU/MiniC direct entry and fallthrough)\n'
