#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-pointer-bool
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/pointer_to_bool_conversion.c" -o "$work/valid.i"
"$minic" -S "$work/valid.i" -o "$work/valid.s"

# Return and assignment boundaries already normalize integer targets through
# the target type. Pointer-to-bool therefore must produce real 0/1 values.
snez_count=$(grep -E -c '^[[:space:]]+snez[[:space:]]+' "$work/valid.s" || true)
seqz_count=$(grep -E -c '^[[:space:]]+seqz[[:space:]]+' "$work/valid.s" || true)
if test "$snez_count" -lt 4; then
    # Core represents pointer truth as is_zero(is_zero(pointer)); requiring two
    # seqz operations per boundary proves canonical 0/1 rather than mere nonzero.
    test "$seqz_count" -ge 8
fi

# A fixed bool parameter is another assignment-conversion boundary. Require
# normalization in the caller before the direct call, not merely AST acceptance.
awk '
  /pass_function_pointer:/ { in_fn=1; snez=0; seqz=0 }
  in_fn && /^[[:space:]]+snez[[:space:]]+/ { snez=1 }
  in_fn && /^[[:space:]]+seqz[[:space:]]+/ { seqz++ }
  in_fn && /call accept_bool/ { exit (snez || seqz >= 2) ? 0 : 1 }
  in_fn && /^\.size[[:space:]]+pass_function_pointer/ { exit 1 }
  END { if (!in_fn) exit 1 }
' "$work/valid.s"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_pointer_to_int_return.c" -o "$work/invalid.i"
if "$minic" -S "$work/invalid.i" -o "$work/invalid.s" >"$work/invalid.out" 2>"$work/invalid.err"; then
    echo 'expected pointer-to-int return to remain rejected' >&2
    exit 1
fi
grep -F 'return expression does not match function return type' "$work/invalid.err" >/dev/null

printf '%s\n' 'PASS compiler/c0/pointer_to_bool_conversion return=function+object assignment=function+object fixed-call=normalized pointer-to-int=reject'
