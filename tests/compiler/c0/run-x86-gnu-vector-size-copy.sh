#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/release"}/tests/compiler-c0-x86-gnu-vector-copy
mkdir -p "$work"

source="$root/tests/compiler/c0/x86_gnu_vector_size_copy.c"
"$host_cc" -std=gnu11 -Wall -Wextra -Werror -mno-sse -mno-sse2 \
    -mno-mmx -mno-avx -fsyntax-only "$source"
"$host_cc" -std=gnu11 -E -P "$source" -o "$work/copy.i"
MINIC_CORE_IR=strict "$minic_cc" --target=x86_64-linux -mno-sse \
    -S "$work/copy.i" -o "$work/copy.minic.s"
test -s "$work/copy.minic.s"
grep -F 'movq 8(%r11), %rdx' "$work/copy.minic.s" >/dev/null
if grep -E '%(xmm|ymm|zmm|mm)[0-9]+' "$work/copy.minic.s" >/dev/null; then
    echo 'FAIL MiniC emitted SIMD/MMX registers under -mno-sse' >&2
    exit 1
fi
"$host_cc" -std=gnu11 -O2 -mno-sse -mno-sse2 -mno-mmx -mno-avx \
    "$source" -no-pie -o "$work/copy.gnu.elf"
"$host_cc" -c "$work/copy.minic.s" -o "$work/copy.minic.o"
"$host_cc" -no-pie "$work/copy.minic.o" -o "$work/copy.minic.elf"
for compiler in gnu minic; do
    "$work/copy.$compiler.elf"
done

for mode in BAD_ARITHMETIC BAD_VOLATILE BAD_EIGHT_BYTES BAD_BYVALUE; do
    negative="$root/tests/compiler/c0/invalid_x86_gnu_vector_size_copy.c"
    "$host_cc" -std=gnu11 -fsyntax-only "-D$mode" "$negative"
    "$host_cc" -std=gnu11 -E -P "-D$mode" "$negative" -o "$work/$mode.i"
    if "$minic_cc" --target=x86_64-linux -S "$work/$mode.i" \
        -o "$work/$mode.s" >"$work/$mode.out" 2>"$work/$mode.err"; then
        echo "FAIL MiniC accepted unsupported vector operation: $mode" >&2
        exit 1
    fi
    test -s "$work/$mode.err"
    case "$mode" in
        BAD_ARITHMETIC) grep -F 'unsupported pointer arithmetic operands' "$work/$mode.err" >/dev/null ;;
        BAD_BYVALUE) grep -F 'unsupported function return type' "$work/$mode.err" >/dev/null ;;
        BAD_VOLATILE|BAD_EIGHT_BYTES)
            grep -F "Core IR does not yet support function 'forbidden'" "$work/$mode.err" >/dev/null ;;
    esac
done

printf '%s\n' 'PASS x86 GNU vector16 copy: GNU/MiniC ELF runtime=2, strict Core=1, unsupported=4, -mno-sse=1'
