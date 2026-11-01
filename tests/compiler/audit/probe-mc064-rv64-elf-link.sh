#!/bin/sh
# Known-negative MC-064 ELF/link audit, not a check-fast green regression.
# Requires RV64 GNU binutils/GCC and qemu-riscv64; stdout is a factual result.
# Once the bug is fixed, flip the expected negative and promote the probe.
set -eu
: "${MINIC_CC:?set MINIC_CC to the compiler under test}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
source="$root/tests/compiler/audit/mc064_rv64_asm_static_symbol.c"

riscv64-linux-gnu-as -march=rv64i \
    "$root/tests/compiler/audit/mc064_rv64_start.S" -o "$work/start.o"

for variant in asm_only direct_call static_object; do
    case "$variant" in
        asm_only) define='' ; expected=11 ; symbol=asm_only_target ;;
        direct_call) define='-DMC064_DIRECT_CALL' ; expected=23 ; symbol=asm_only_target ;;
        static_object) define='-DMC064_STATIC_OBJECT' ; expected=11 ; symbol=asm_only_object ;;
    esac

    riscv64-linux-gnu-gcc -march=rv64i -mabi=lp64 -std=gnu11 \
        -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -E -P -x c $define \
        "$source" -o "$work/$variant.i"
    # Both compilers consume exactly this single preprocessed translation unit.
    riscv64-linux-gnu-gcc -march=rv64i -mabi=lp64 -std=gnu11 -O0 \
        -Wall -Wextra -Werror \
        -ffreestanding -fno-builtin -c "$work/$variant.i" \
        -o "$work/$variant.gnu.o"
    "$MINIC_CC" --target=riscv64-linux -march=rv64i -mabi=lp64 \
        -S "$work/$variant.i" -o "$work/$variant.minic.s"
    riscv64-linux-gnu-as -march=rv64i "$work/$variant.minic.s" \
        -o "$work/$variant.minic.o"

    # Confirm a real 64-bit ELF relocation survived assembly on both paths;
    # native exit alone would not notice an incorrectly dropped ASM effect.
    for producer in gnu minic; do
        if ! riscv64-linux-gnu-readelf -r "$work/$variant.$producer.o" |
            grep -Fq 'R_RISCV_64'; then
            echo "MC064_AUDIT=FAIL missing ELF relocation $variant/$producer" >&2
            exit 1
        fi
    done
    # Also check symbol definition/ref status, not only linker return codes.
    if ! riscv64-linux-gnu-nm --defined-only "$work/$variant.gnu.o" |
        grep -Eq " [a-zA-Z] $symbol\$"; then
        echo "MC064_AUDIT=FAIL GNU did not define $symbol in $variant" >&2
        exit 1
    fi
    riscv64-linux-gnu-nm -u "$work/$variant.minic.o" > "$work/$variant.undefined"
    riscv64-linux-gnu-ld -m elf64lriscv -static -e _start \
        "$work/start.o" "$work/$variant.gnu.o" \
        -o "$work/$variant.gnu.elf"
    set +e
    qemu-riscv64 "$work/$variant.gnu.elf"
    gnu_exit=$?
    set -e
    if [ "$gnu_exit" -ne "$expected" ]; then
        echo "MC064_AUDIT=FAIL $variant GNU native exit=$gnu_exit expected=$expected" >&2
        exit 1
    fi

    if [ "$variant" = asm_only ]; then
        if ! grep -Eq "U $symbol\$" "$work/$variant.undefined"; then
            echo "MC064_AUDIT=FAIL expected MiniC undefined $symbol" >&2
            exit 1
        fi
        if riscv64-linux-gnu-ld -m elf64lriscv -static -e _start \
            "$work/start.o" "$work/$variant.minic.o" \
            -o "$work/$variant.minic.elf" 2> "$work/$variant.ld.err"; then
            echo 'MC064_AUDIT=CHANGED MiniC asm_only now links; promote to positive regression' >&2
            exit 1
        fi
        if ! grep -Fq "undefined reference to \`$symbol'" "$work/$variant.ld.err"; then
            echo 'MC064_AUDIT=FAIL unexpected MiniC negative link diagnostic' >&2
            sed -n '1,10p' "$work/$variant.ld.err" >&2
            exit 1
        fi
    else
        if grep -Eq "U $symbol\$" "$work/$variant.undefined"; then
            echo "MC064_AUDIT=FAIL positive control undefined symbol=$symbol" >&2
            exit 1
        fi
        riscv64-linux-gnu-ld -m elf64lriscv -static -e _start \
            "$work/start.o" "$work/$variant.minic.o" \
            -o "$work/$variant.minic.elf"
        set +e
        qemu-riscv64 "$work/$variant.minic.elf"
        minic_exit=$?
        set -e
        if [ "$minic_exit" -ne "$expected" ]; then
            echo "MC064_AUDIT=FAIL $variant MiniC native exit=$minic_exit expected=$expected" >&2
            exit 1
        fi
    fi
done

echo 'MC064_PREEXISTING_NEGATIVE=CONFIRMED same_preprocessed_input=YES GNU_AS=PASS GNU_ELF_AND_QEMU=PASS MiniC_AS=PASS MiniC_ASM_ONLY_LD=UNDEFINED_STATIC_FUNCTION direct_call_and_object_native=PASS'
