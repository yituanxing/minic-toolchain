#!/usr/bin/env bash
# Compare GNU and MiniC's real x86 __put_user_N private register ABI with
# pointer (not just integer) RAX payloads, including the nocheck spelling.
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
mini=${1:-${MINIC_CC:-"$root/build/release/bin/minic-cc"}}
fixture="$root/tests/compiler/c0/x86_linux_put_user_pointer.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { printf 'X86_PUT_PTR_NATIVE=FAIL reason=%s\n' "$*" >&2; exit 1; }
test -x "$mini" || fail 'compiler missing'

cc -std=gnu11 -E -P -x c "$fixture" -o "$work/source.i"
"$mini" --target=x86_64-linux -S "$work/source.i" -o "$work/mini.s" \
    || fail 'MiniC frontend/Core/M1 rejected C source'
cc -std=gnu11 -O0 -no-pie -x c "$work/source.i" -o "$work/gnu"
cc -no-pie -x assembler "$work/mini.s" -o "$work/mini"

set +e
"$work/gnu"
gnu_status=$?
"$work/mini"
mini_status=$?
set -e
test "$gnu_status" -eq 42 || fail "GNU ABI returned $gnu_status"
test "$mini_status" -eq 42 || fail "MiniC ABI returned $mini_status"
test "$(grep -c 'call __put_user_' "$work/mini.s")" -eq 2 \
    || fail 'MiniC omitted or invented private x86 helper calls'
grep -q 'call __put_user_nocheck_8' "$work/mini.s" \
    || fail 'nocheck helper must retain its exact symbol'
test "$(grep -Fc 'movq %rbx, (%rsp)' "$work/mini.s")" -eq 2 \
    || fail 'RBX save missing around one of the helper calls'
test "$(grep -Fc 'movq (%rsp), %rbx' "$work/mini.s")" -eq 2 \
    || fail 'RBX restore missing around one of the helper calls'
printf '%s\n' 'X86_PUT_PTR_NATIVE=PASS GNU_status=42 MiniC_status=42 private_calls=2 pointer_rax=8B rbX_preserved=1'
