#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/enum-forward-completion
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/enum_forward_completion.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"
grep -F '.dword 1099511627776' "$work/output.s" >/dev/null
grep -F 'global_wide:' "$work/output.s" >/dev/null
grep -F 'read_wide:' "$work/output.s" >/dev/null
printf '%s
' 'PASS compiler/c0/enum-forward-completion identity=enum-id compatible=canonical no-program-refresh=1'
