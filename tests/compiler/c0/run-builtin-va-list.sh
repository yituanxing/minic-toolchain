#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-${MINIC:-"$root/build/debug/bin/minic-cc"}}
host_cc=${HOST_CC:-${CC:-cc}}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

"$host_cc" -E -P -x c "$root/tests/compiler/c0/builtin_va_list.c" \
    -o "$work/builtin_va_list.i"

# RV32/RV64/Loong use a pointer-shaped C va_list, so an old pointer
# roundtrip is valid for those targets only.
for target in riscv32-linux riscv64-linux loongarch64-linux; do
    "$minic" --target="$target" -S "$work/builtin_va_list.i" \
        -o "$work/$target.s"
    test -s "$work/$target.s"
    grep -F 'builtin_va_list_as_pointer:' "$work/$target.s" >/dev/null
    grep -F 'builtin_va_list_roundtrip:' "$work/$target.s" >/dev/null
done

# Structured ABIs cannot return a va_list array or silently reinterpret it
# as void*. The new source-object fixtures exercise their real C layouts.
for target in x86_64-linux aarch64-linux; do
    if "$minic" --target="$target" -S "$work/builtin_va_list.i" \
        -o "$work/$target-old-proxy.s" \
        >"$work/$target.out" 2>"$work/$target.err"; then
        echo "invalid pointer-shaped va_list accepted for $target" >&2
        exit 1
    fi
    test ! -s "$work/$target-old-proxy.s"
done

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/x86_va_cursor_source_basic_runtime.c" \
    -o "$work/x86-source.i"
"$minic" --target=x86_64-linux -S "$work/x86-source.i" \
    -o "$work/x86-source.s"
"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/aarch64_va_cursor_source_object_runtime.c" \
    -o "$work/aarch64-source.i"
"$minic" --target=aarch64-linux -S "$work/aarch64-source.i" \
    -o "$work/aarch64-source.s"

printf '%s\n' 'PASS compiler/c0/builtin_va_list linear_pointer=3 structured_proxy_rejected=2 x86_source_array_24B=1 aarch64_source_record_32B=1'
