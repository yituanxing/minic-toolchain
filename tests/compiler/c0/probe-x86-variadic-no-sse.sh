#!/usr/bin/env bash
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
cc=${HOST_CC:-gcc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { echo "X86_VARIADIC_NO_SSE=FAIL $*" >&2; exit 1; }
src="$root/tests/compiler/c0/x86_variadic_no_sse.c"

"$cc" -E -P -std=gnu11 "$src" -o "$work/integers.i" \
    || fail 'GNU preprocess integer fixture'
"$minic" -S "$work/integers.i" --target=x86_64-linux -fno-PIE \
    -mno-sse -o "$work/no-sse.s" || fail 'MiniC -mno-sse'
"$minic" -S "$work/integers.i" --target=x86_64-linux -fno-PIE \
    -o "$work/default.s" || fail 'MiniC default x86'
sed -n '/^minic_x86_integer_varargs:$/,/^\.size minic_x86_integer_varargs,/p' \
    "$work/no-sse.s" > "$work/no-sse-function.s"
sed -n '/^minic_x86_integer_varargs:$/,/^\.size minic_x86_integer_varargs,/p' \
    "$work/default.s" > "$work/default-function.s"
test -s "$work/no-sse-function.s" || fail 'missing integer variadic function'
if grep -Eq '(%xmm[0-9]+|movdqu|movaps|movups)' "$work/no-sse-function.s"; then
    fail 'no-SSE integer variadic prologue touched an XMM register'
fi
grep -Eq 'testb %al, %al' "$work/default-function.s" \
    || fail 'ordinary SysV variadic prologue did not guard optional XMM spill'
grep -Eq 'movdqu %xmm0,' "$work/default-function.s" \
    || fail 'ordinary SysV variadic prologue lost FP register save'
"$cc" -no-pie -x assembler "$work/no-sse.s" -o "$work/no-sse" \
    || fail 'GNU link no-SSE variant'
"$work/no-sse" || fail 'no-SSE integer variadic runtime'
"$cc" -no-pie -x assembler "$work/default.s" -o "$work/default" \
    || fail 'GNU link default variant'
"$work/default" || fail 'default integer variadic runtime'

"$cc" -E -P -std=gnu11 -DMINIC_X86_TEST_FLOAT_VARARGS "$src" \
    -o "$work/float.i" || fail 'GNU preprocess FP fixture'
"$minic" -S "$work/float.i" --target=x86_64-linux -fno-PIE \
    -o "$work/float.s" || fail 'MiniC FP variadic fixture'
if "$minic" -S "$work/float.i" --target=x86_64-linux -fno-PIE \
    -mno-sse -o "$work/illegal-float.s" > "$work/no-sse-float.log" 2>&1; then
    fail 'no-SSE target silently allowed a reachable floating operation'
fi
grep -q 'x86-64 -mno-sse rejects reachable floating' "$work/no-sse-float.log" \
    || fail 'no-SSE FP rejection did not explain unsupported ABI'
"$cc" -no-pie -x assembler "$work/float.s" -o "$work/float" \
    || fail 'GNU link FP variadic fixture'
"$work/float" || fail 'ordinary SysV FP variadic runtime'
echo 'X86_VARIADIC_NO_SSE=PASS no_sse_xmm=0 sysv_al_guard=PASS integers=PASS fp=PASS no_sse_fp_fail_closed=PASS'
