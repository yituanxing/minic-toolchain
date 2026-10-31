#!/bin/sh
# GCC -fno-pic emits `%hi(symbol)` and `%lo(symbol)` machine operands, not
# the `la` pseudo's PCREL fixups. Compare their actual relocatable ELF output.
set -eu
: "${MINIAS:?MINIAS must point to minic-as}"
: "${BUILD_DIR:?BUILD_DIR must be set}"

work="$BUILD_DIR/tests/assembler/rv-gcc-absolute"
source=tests/assembler/fixtures/riscv-gcc-absolute-symbol-relocs.S
mkdir -p "$work"
"$MINIAS" --target=riscv -march=rv64gc -mabi=lp64d \
    -o "$work/mini.o" "$source"
readelf -Wr "$work/mini.o" >"$work/mini-relocs.txt"
test "$(grep -c 'R_RISCV_HI20' "$work/mini-relocs.txt")" -eq 2
test "$(grep -c 'R_RISCV_LO12_I' "$work/mini-relocs.txt")" -eq 4
test "$(grep -c 'R_RISCV_LO12_S' "$work/mini-relocs.txt")" -eq 2
grep -Eq 'R_RISCV_HI20.*runtime_bias[[:space:]]*\+[[:space:]]*8' \
    "$work/mini-relocs.txt"

# Modifier shape is part of the ISA relocation contract: an I-form %lo
# cannot be accepted as a U-form %hi, and malformed forms fail closed.
for instruction in \
    'lui a5, %lo(runtime_bias)' \
    'auipc a5, %hi(runtime_bias)' \
    'addi a5, a5, %hi(runtime_bias)' \
    'ld a5, %hi(runtime_bias)(a5)' \
    'sd a5, %hi(runtime_bias)(a5)' \
    'lui a5, %hi(runtime_bias'; do
    printf '.text\n%s\n' "$instruction" >"$work/invalid.s"
    if "$MINIAS" --target=riscv -march=rv64gc -mabi=lp64d \
        -o "$work/invalid.o" "$work/invalid.s" \
        >"$work/invalid.log" 2>&1; then
        echo "MINIAS_RV_ABSOLUTE_RELOCS=FAIL accepted:$instruction" >&2
        exit 1
    fi
done

if command -v riscv64-linux-gnu-as >/dev/null 2>&1 &&
   command -v riscv64-linux-gnu-objcopy >/dev/null 2>&1; then
    riscv64-linux-gnu-as -march=rv64gc -mabi=lp64d -mno-relax \
        -o "$work/gnu.o" "$source"
    riscv64-linux-gnu-objcopy -j .text -O binary \
        "$work/mini.o" "$work/mini.bin"
    riscv64-linux-gnu-objcopy -j .text -O binary \
        "$work/gnu.o" "$work/gnu.bin"
    cmp "$work/gnu.bin" "$work/mini.bin"
    python3 tests/assembler/elf_semantic_compare.py --strict \
        --json-out "$work/strict-elf.json" \
        "$work/gnu.o" "$work/mini.o" >"$work/strict-elf.log"
    echo 'MINIAS_RV_ABSOLUTE_RELOCS=PASS GNU_bytes=1 ELF_semantics=1 relocs=8 negative=6'
else
    echo 'MINIAS_RV_ABSOLUTE_RELOCS=PASS GNU_AS=SKIP(toolchain-unavailable) relocs=8 negative=6'
fi
