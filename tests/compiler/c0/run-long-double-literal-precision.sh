#!/usr/bin/env bash
set -Eeuo pipefail

minic=${1:?usage: run-long-double-literal-precision.sh path/to/minic-cc}
root=$(cd "$(dirname "$0")/../../.." && pwd)
source_file="$root/tests/compiler/c0/long_double_literal_precision.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cc -std=gnu11 -Wall -Wextra -Werror -fsyntax-only "$source_file"
cc -std=gnu11 -no-pie "$source_file" -o "$work/gnu.elf"
cc -E -P -x c "$source_file" -o "$work/source.i"

# RV32's existing ILP32 Core frame does not yet materialize long-double
# locals; the literal parser itself is exercised independently below.
for target in x86_64 aarch64 loongarch64 riscv64; do
    "$minic" --target="$target"-linux -S "$work/source.i" \
        -o "$work/$target.s"
    test -s "$work/$target.s"
done

cc -no-pie -x assembler "$work/x86_64.s" -o "$work/minic.elf"
for impl in gnu minic; do
    set +e
    "$work/$impl.elf"
    actual=$?
    set -e
    test "$actual" -eq 42 || {
        printf '%s long-double literal result: %s (expected 42)\n' \
            "$impl" "$actual" >&2
        exit 1
    }
done

rv64_result=unavailable
if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1 &&
   command -v qemu-riscv64 >/dev/null 2>&1; then
    rv64_source="$root/tests/compiler/c0/long_double_binary128_tail.c"
    cc -E -P -x c "$rv64_source" -o "$work/rv64-tail.i"
    riscv64-linux-gnu-gcc -std=gnu11 -O0 -ffreestanding -c \
        "$rv64_source" -o "$work/rv64-gnu.o"
    "$minic" --target=riscv64-linux -S "$work/rv64-tail.i" \
        -o "$work/rv64-mini.s"
    # No target glibc sysroot is required. Use a tiny Linux exit(2) _start
    # and the installed target libgcc binary128 comparison helper only.
    riscv64-linux-gnu-gcc -c -x assembler - -o "$work/rv64-start.o" <<'ASM'
.text
.globl _start
_start:
    call main
    li a7, 93
    ecall
ASM
    for impl in gnu mini; do
        input="$work/rv64-$impl.o"
        if [ "$impl" = mini ]; then input="$work/rv64-mini.s"; fi
        riscv64-linux-gnu-gcc -nostdlib -static "$work/rv64-start.o" \
            "$input" -lgcc -o "$work/rv64-$impl.elf"
        set +e
        qemu-riscv64 "$work/rv64-$impl.elf"
        actual=$?
        set -e
        test "$actual" -eq 42 || {
            printf '%s RV64 binary128 literal result: %s (expected 42)\n' \
                "$impl" "$actual" >&2
            exit 1
        }
    done
    rv64_result=gnu_mini_qemu_42
fi
echo "LONG_DOUBLE_LITERAL_PRECISION=PASS gcc_oracle=1 four_ISA_S=4 x86_native=42 rv64=$rv64_result rv32_frame_unsupported=1"
