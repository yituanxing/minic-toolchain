#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/gnu-pointer-signedness
mkdir -p "$work"

# GNU continuation is intentionally frontend-only: the core assignment relation
# remains strict, while static pointer relocation keeps the validated address bits.
positive=gnu_pointer_signedness_static
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/$positive.c" -o "$work/$positive.i"
"$minic" -S "$work/$positive.i" -o "$work/$positive.s"
test -s "$work/$positive.s"

for negative in gnu_pointer_signedness_const_drop gnu_pointer_signedness_rank_mismatch; do
  "$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/$negative.c" -o "$work/$negative.i"
  if "$minic" -S "$work/$negative.i" -o "$work/$negative.s" >"$work/$negative.stdout" 2>"$work/$negative.stderr"; then
    printf '%s\n' "FAIL compiler/c0/$negative: compilation unexpectedly succeeded" >&2
    exit 1
  fi
  grep -F "static pointer initializer type mismatch" "$work/$negative.stderr" >/dev/null
done

printf '%s\n' 'PASS compiler/c0/gnu-pointer-signedness accepted=same-rank rejected=qualifier-drop,rank-mismatch'
