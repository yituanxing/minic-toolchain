#!/usr/bin/env bash
# RV32 ELF32 Linux differential: GNU preprocess/GNU AS/GNU LD/QEMU are the
# original tool owners; only the selected C translation unit uses MiniC.
# Do not equate this freestanding Linux userspace probe with kernel P0/P1.
set -Eeuo pipefail

root=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
gnu_cc=${RISCV_CC:-riscv64-linux-gnu-gcc}
gnu_ld=${RISCV_LD:-riscv64-linux-gnu-ld}
gnu_readelf=${RISCV_READELF:-riscv64-linux-gnu-readelf}
qemu=${QEMU_RISCV32:-qemu-riscv32}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
source_file="$root/tests/compiler/c0/elf_hidden_undefined_pic.c"
owner_file="$root/tests/compiler/c0/elf_hidden_undefined_pic_owner.c"
rv32=(-march=rv32i -mabi=ilp32)

for tool in "$minic_cc" "$gnu_cc" "$gnu_ld" "$gnu_readelf" "$qemu"; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "RV32_PIC_ELF_QEMU=NOT_RUN missing_tool=$tool" >&2
        exit 2
    }
done

"$gnu_cc" "${rv32[@]}" -E -P -ffreestanding -std=gnu11 \
    "$source_file" -o "$work/selected.i"
"$gnu_cc" "${rv32[@]}" -O0 -ffreestanding -fno-builtin \
    -c "$owner_file" -o "$work/owner.o"

cat > "$work/start.S" <<'START'
    .text
    .globl _start
_start:
    call main
    li a7, 93
    ecall
START
"$gnu_cc" "${rv32[@]}" -c "$work/start.S" -o "$work/start.o"

for mode in pic pie static; do
    case "$mode" in
        pic) flag=-fPIC ;;
        pie) flag=-fPIE ;;
        static) flag=-fno-pie ;;
    esac
    "$minic_cc" --target=riscv32-linux "${rv32[@]}" "$flag" -S \
        "$work/selected.i" -o "$work/mini-$mode.s"
    "$gnu_cc" "${rv32[@]}" -c "$work/mini-$mode.s" -o "$work/mini-$mode.o"
    "$gnu_cc" "${rv32[@]}" -O0 -ffreestanding -fno-builtin "$flag" \
        -x c -c "$work/selected.i" -o "$work/gnu-$mode.o"

    "$gnu_readelf" -Wr "$work/mini-$mode.o" > "$work/mini-$mode.reloc"
    "$gnu_readelf" -Ws "$work/mini-$mode.o" > "$work/mini-$mode.symbols"
    for symbol in hidden_data hidden_function; do
        grep -Eq "GLOBAL[[:space:]]+HIDDEN[[:space:]]+UND[[:space:]]+$symbol$" \
            "$work/mini-$mode.symbols"
        grep -Eq "R_RISCV_PCREL_HI20[[:space:]]+[^[:space:]]+[[:space:]]+$symbol([[:space:]]|$)" \
            "$work/mini-$mode.reloc"
        if grep -Eq "R_RISCV_GOT_HI20[[:space:]]+[^[:space:]]+[[:space:]]+$symbol([[:space:]]|$)" \
            "$work/mini-$mode.reloc"; then
            echo "RV32_PIC_ELF_QEMU=FAIL $mode hidden_got=$symbol" >&2
            exit 1
        fi
    done
    if [[ "$mode" == static ]]; then
        if grep -q 'R_RISCV_GOT_HI20' "$work/mini-$mode.reloc"; then
            echo 'RV32_PIC_ELF_QEMU=FAIL static_kernel_GOT' >&2
            exit 1
        fi
    else
        for symbol in default_data default_function; do
            grep -Eq "R_RISCV_GOT_HI20[[:space:]]+[^[:space:]]+[[:space:]]+$symbol([[:space:]]|$)" \
                "$work/mini-$mode.reloc"
        done
    fi

    for compiler in mini gnu; do
        "$gnu_ld" -m elf32lriscv -static -e _start \
            -o "$work/$compiler-$mode.elf" "$work/start.o" \
            "$work/$compiler-$mode.o" "$work/owner.o"
        if ! timeout 30s "$qemu" "$work/$compiler-$mode.elf"; then
            echo "RV32_PIC_ELF_QEMU=FAIL compiler=$compiler mode=$mode" >&2
            exit 1
        fi
    done
    echo "RV32_PIC_ELF_QEMU_MODE=PASS mode=$mode gcc=0 minic=0"
done

echo 'RV32_PIC_ELF_QEMU=PASS ELF32=1 GNU_PP=1 GNU_AS=1 GNU_LD=1 QEMU_LINUX_USER=1 hidden_PCREL=1 default_GOT=1 static_no_GOT=1 gcc_minic=3/3'
