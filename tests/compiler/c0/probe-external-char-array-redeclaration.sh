#!/usr/bin/env bash
set -Eeuo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
cc=${HOST_CC:-gcc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { echo "EXTERNAL_CHAR_ARRAY_REDECLARATION=FAIL $*" >&2; exit 1; }

test -x "$minic" || fail 'MiniC binary missing'
"$cc" -std=gnu11 -E -P \
    "$root/tests/compiler/c0/external_char_array_redeclaration.c" \
    -o "$work/positive.i" || fail 'GNU preprocess positive'
"$cc" -std=gnu11 -Werror -O0 -no-pie -x c "$work/positive.i" \
    -o "$work/gnu" || fail 'GNU compile positive'
"$minic" --target=x86_64-linux -S "$work/positive.i" \
    -o "$work/minic.s" || fail 'MiniC compile positive'
"$cc" -no-pie -x assembler "$work/minic.s" \
    -o "$work/minic" || fail 'GNU assemble/link MiniC positive'
"$work/gnu" || fail 'GNU runtime positive'
"$work/minic" || fail 'MiniC runtime differs from GNU'

"$cc" -std=gnu11 -E -P \
    "$root/tests/compiler/c0/external_char_array_redeclaration_too_long.c" \
    -o "$work/negative.i" || fail 'GNU preprocess negative'
if "$cc" -std=gnu11 -Werror -fsyntax-only -x c "$work/negative.i" \
    >"$work/gnu-negative.log" 2>&1; then
    fail 'GNU unexpectedly accepted overlong fixed array'
fi
if "$minic" --target=x86_64-linux -S "$work/negative.i" \
    -o "$work/negative.s" >"$work/minic-negative.log" 2>&1; then
    fail 'MiniC unexpectedly accepted overlong fixed array'
fi
test ! -e "$work/negative.s" || fail 'MiniC left partial output on rejection'
echo 'EXTERNAL_CHAR_ARRAY_REDECLARATION=PASS GNU_MiniC_runtime_and_bounded_reject'
