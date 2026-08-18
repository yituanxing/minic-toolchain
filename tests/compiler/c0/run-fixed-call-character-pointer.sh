#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/fixed-call-character-pointer
mkdir -p "$work"
"$minic" -S "$root/tests/compiler/c0/fixed_call_character_pointer.c" -o "$work/output.s"
test -s "$work/output.s"
