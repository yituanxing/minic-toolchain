#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/release"}/tests/compiler-c0-x86-gnu-vector-storage

mkdir -p "$work"

source="$root/tests/compiler/c0/x86_gnu_vector_size_storage.c"
"$host_cc" -std=gnu11 -Wall -Wextra -Werror -mno-sse -mno-sse2 \
    -mno-mmx -mno-avx -fsyntax-only "$source"
"$host_cc" -std=gnu11 -E -P "$source" -o "$work/storage.i"
MINIC_CORE_IR=strict "$minic_cc" --target=x86_64-linux -mno-sse \
    -S "$work/storage.i" -o "$work/storage.s"
test -s "$work/storage.s"
"$host_cc" -std=gnu11 -O2 -mno-sse -mno-sse2 -mno-mmx -mno-avx \
    -c "$source" -o "$work/storage.gnu.o"
"$host_cc" -c "$work/storage.s" -o "$work/storage.minic.o"
for compiler in gnu minic; do
    "$host_cc" -no-pie "$work/storage.$compiler.o" \
        "$root/tests/compiler/c0/x86_gnu_vector_size_storage_driver.c" \
        -o "$work/storage.$compiler.elf"
    "$work/storage.$compiler.elf"
done

for target in riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
    if "$minic_cc" --target="$target" -S "$work/storage.i" \
        -o "$work/$target.s" >"$work/$target.out" 2>"$work/$target.err"; then
        echo "FAIL foreign target admitted GNU vector_size storage without policy: $target" >&2
        exit 1
    fi
    grep -F 'GNU vector_size storage is unsupported on this target' \
        "$work/$target.err" >/dev/null
done

for mode in BAD_SIZE BAD_ELEMENT BAD_NONCONST; do
    negative="$root/tests/compiler/c0/invalid_x86_gnu_vector_size.c"
    if "$host_cc" -std=gnu11 -Werror -fsyntax-only "-D$mode" "$negative" \
        >"$work/$mode.gnu.out" 2>"$work/$mode.gnu.err"; then
        echo "FAIL GNU accepted malformed vector_size: $mode" >&2
        exit 1
    fi
    "$host_cc" -std=gnu11 -E -P "-D$mode" "$negative" -o "$work/$mode.i"
    if "$minic_cc" --target=x86_64-linux -S "$work/$mode.i" \
        -o "$work/$mode.s" >"$work/$mode.out" 2>"$work/$mode.err"; then
        echo "FAIL MiniC accepted malformed vector_size: $mode" >&2
        exit 1
    fi
    grep -E 'vector_size|GNU vector size' "$work/$mode.err" >/dev/null
done

# GNU supports 256-bit vectors; this MiniC storage tranche does not. It
# must fail *explicitly*, never accept bytes and then truncate to 128 bits.
negative="$root/tests/compiler/c0/invalid_x86_gnu_vector_size.c"
"$host_cc" -std=gnu11 -Werror -fsyntax-only -DUNSUPPORTED_WIDTH "$negative"
"$host_cc" -std=gnu11 -E -P -DUNSUPPORTED_WIDTH "$negative" \
    -o "$work/UNSUPPORTED_WIDTH.i"
if "$minic_cc" --target=x86_64-linux -S "$work/UNSUPPORTED_WIDTH.i" \
    -o "$work/UNSUPPORTED_WIDTH.s" >"$work/UNSUPPORTED_WIDTH.out" \
    2>"$work/UNSUPPORTED_WIDTH.err"; then
    echo 'FAIL MiniC accepted unimplemented 256-bit vector storage' >&2
    exit 1
fi
grep -F 'unsupported by target storage layout' "$work/UNSUPPORTED_WIDTH.err" >/dev/null

printf '%s\n' 'PASS x86 GNU vector_size storage: GNU/MiniC compiled ELF runtime=2, layout, foreign gate=4, invalid=3, width boundary=1, no-SSE=1'
