#!/usr/bin/env bash
# RISC-V byte-sized symbolic differences use symmetric ADD8/SUB8 ELF
# relocations. A missing ADD8 capability must not make MCExpr reject every
# pair, or silently fold an external/other-section difference into a byte.
set -Eeuo pipefail
: "${MINIAS:?exact-head MiniAS executable required}"
command -v readelf >/dev/null
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat >"$work/byte-deltas.s" <<'SRC'
.data
.globl byte_delta_table
byte_delta_table:
  .byte external_a-external_b
  .byte external_a-external_b+5
  .byte external_b-external_a
  .byte external_a-external_b-7
  .byte external_b-external_a+3
  .byte external_a-external_b+127
  .byte external_a-.
  .byte external_b-.-3
  .short external_a-external_b
  .long external_a-external_b
  .quad external_a-external_b
SRC

gnu_available=true
if ! command -v riscv64-linux-gnu-as >/dev/null 2>&1; then
    gnu_available=false
fi

for xlen in 32 64; do
    if [[ $xlen == 32 ]]; then abi=ilp32; else abi=lp64; fi
    "$MINIAS" --target=riscv "-march=rv${xlen}i" "-mabi=$abi" \
        -o "$work/mini$xlen.o" "$work/byte-deltas.s"
    readelf -Wr "$work/mini$xlen.o" >"$work/mini$xlen.reloc"
    # Six inter-symbol pairs and two S-P expressions (which need private
    # position anchors), including reversed operands and signed addends.
    # Every ADD8 and SUB8 must target the identical byte offset.
    awk '$3=="R_RISCV_ADD8" {print $1}' "$work/mini$xlen.reloc" \
        >"$work/mini$xlen.add"
    awk '$3=="R_RISCV_SUB8" {print $1}' "$work/mini$xlen.reloc" \
        >"$work/mini$xlen.sub"
    test "$(wc -l <"$work/mini$xlen.add")" -eq 8
    cmp "$work/mini$xlen.add" "$work/mini$xlen.sub"
    # The 16/32/64-bit counterparts must stay present and symmetric too.
    for width in 16 32 64; do
        test "$(grep -c "R_RISCV_ADD$width" "$work/mini$xlen.reloc")" -eq 1
        test "$(grep -c "R_RISCV_SUB$width" "$work/mini$xlen.reloc")" -eq 1
    done

    # Bare ABS8 has no psABI relocation. Nor can a linker relocate a
    # divided S-P byte expression; preserve the target capability boundary.
    for invalid in 'external_a' '(external_a-.)/2'; do
        printf '.data\n.byte %s\n' "$invalid" >"$work/invalid.s"
        if "$MINIAS" --target=riscv "-march=rv${xlen}i" "-mabi=$abi" \
            -o "$work/invalid$xlen.o" "$work/invalid.s" \
            >"$work/invalid$xlen.out" 2>"$work/invalid$xlen.err"; then
            echo "RISC-V accepted unsupported byte relocation: RV$xlen $invalid" >&2
            exit 1
        fi
    done

    if "$gnu_available"; then
        riscv64-linux-gnu-as "-march=rv${xlen}i" "-mabi=$abi" \
            -mno-relax -o "$work/gnu$xlen.o" "$work/byte-deltas.s"
        readelf -Wr "$work/gnu$xlen.o" >"$work/gnu$xlen.reloc"
        for who in gnu mini; do
            awk '$3 ~ /^R_RISCV_(ADD|SUB)(8|16|32|64)$/ \
                 {print $1,$3}' "$work/$who$xlen.reloc" \
                >"$work/$who$xlen.pairs"
        done
        cmp "$work/gnu$xlen.pairs" "$work/mini$xlen.pairs"
    fi
done

if "$gnu_available"; then
    echo 'MINIAS_RV_ADD8_SUB8=PASS RV32=8 RV64=8 negatives=4 GNU_types_offsets=exact'
else
    echo 'MINIAS_RV_ADD8_SUB8=PASS RV32=8 RV64=8 negatives=4 GNU=SKIP(cross-as-unavailable)'
fi
