#!/usr/bin/env bash
# Linux arm64 assembler.h mov_q: GAS uses logical shifts of two's-complement
# numeric constants when deciding MOVZ/MOVK lanes and writing their bits.
# A wrong branch changes early-boot __cpu_setup layout; a wrong lane changes
# TCR feature values even when both objects still link successfully.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
: "${AARCH64_AS:=aarch64-linux-gnu-as}"
command -v "$AARCH64_AS" >/dev/null
command -v python3 >/dev/null
python3 -c 'import elftools' >/dev/null
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

cat >"$work/linux-movq.s" <<'ASM'
.section .idmap.text, "ax"
.macro mov_q, reg, val
    .if (((\val) >> 31) == 0 || ((\val) >> 31) == 0x1ffffffff)
        movz \reg, :abs_g1_s:\val
    .else
        .if (((\val) >> 47) == 0 || ((\val) >> 47) == 0x1ffff)
            movz \reg, :abs_g2_s:\val
        .else
            movz \reg, :abs_g3:\val
            movk \reg, :abs_g2_nc:\val
        .endif
        movk \reg, :abs_g1_nc:\val
    .endif
    movk \reg, :abs_g0_nc:\val
.endm
.globl __cpu_setup
__cpu_setup:
    // Real tcr_clear_errata_bits computes a high-bit negative mask.
    mov_q x5, (~(((1) << 20) | (0)))
    // Real PI/POE TCR expressions mask a negative arithmetic intermediate
    // by a logically shifted ~0; signed arithmetic shift is incorrect.
    mov_q x0, (((~0) - (1 << 57) + 1) & ((~0) >> (64 - 1 - 57)))
    // Keep an unsigned high-bit literal and a checked signed MOVW lane.
    mov_q x6, (0x8000000000001234 | 0x56)
    ret
ASM

"$AARCH64_AS" -o "$work/gnu.o" "$work/linux-movq.s"
"$MINIAS" --target=aarch64 -o "$work/mini.o" "$work/linux-movq.s"
python3 "$(dirname "$0")/elf_semantic_compare.py" --strict \
    "$work/gnu.o" "$work/mini.o" >"$work/diff.json" || {
    echo 'MINIAS_AARCH64_LINUX_MOVQ=FAIL GNU_GAS_strict_diff' >&2
    python3 -m json.tool "$work/diff.json" >&2
    exit 1
}
echo 'MINIAS_AARCH64_LINUX_MOVQ=PASS GNU_ELF_exact mask_two_instr=1 logical_shift=1 high_unsigned=1'
