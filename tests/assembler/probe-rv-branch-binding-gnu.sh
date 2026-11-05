#!/usr/bin/env bash
# GNU GAS/LD oracle for RISC-V branches: local/global/weak/undefined,
# cross-section and far same-section branches, including explicit addends.
# The same Target resolver must govern both MCLayout growth and final RELA.
set -Eeuo pipefail
: "${MINIAS:?exact-head MiniAS binary required}"
gas=${RISCV_GAS:-riscv64-linux-gnu-as}
copy=${RISCV_OBJCOPY:-riscv64-linux-gnu-objcopy}
ld=${RISCV_LD:-riscv64-linux-gnu-ld}
readelf=${READELF:-readelf}
for tool in "$gas" "$copy" "$ld" "$readelf" cmp diff sed awk; do
    command -v "$tool" >/dev/null || {
        echo "MINIAS_RV_BRANCH_BINDING=SKIP missing $tool" >&2
        exit 0
    }
done
base=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
fixture="$base/fixtures/rv-branch-binding-gnu.S"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

for profile in rv32i:norvc:ilp32 rv32ic:rvc:ilp32 \
               rv64i:norvc:lp64 rv64ic:rvc:lp64 \
               rv64gc:norvc:lp64d rv64gc:rvc:lp64d; do
    IFS=: read -r march cmode abi <<<"$profile"
    linker_mode=()
    if [[ $march == rv32* ]]; then linker_mode=(-m elf32lriscv); fi
    sed "s/MODE_PLACEHOLDER/$cmode/" "$fixture" >"$work/source.s"
    "$MINIAS" "-march=$march" "-mabi=$abi" \
        -o "$work/mini.o" "$work/source.s"
    "$gas" "-march=$march" "-mabi=$abi" \
        -o "$work/gnu.o" "$work/source.s"

    for section in .text .text.more; do
        "$copy" -O binary -j "$section" "$work/gnu.o" "$work/gnu.bin"
        "$copy" -O binary -j "$section" "$work/mini.o" "$work/mini.bin"
        cmp "$work/gnu.bin" "$work/mini.bin"
    done
    "$readelf" -rW "$work/gnu.o" |
        awk '$3 ~ /^R_RISCV_/ {print $1,$3,$5,$6,$7}' >"$work/gnu.reloc"
    "$readelf" -rW "$work/mini.o" |
        awk '$3 ~ /^R_RISCV_/ {print $1,$3,$5,$6,$7}' >"$work/mini.reloc"
    diff -u "$work/gnu.reloc" "$work/mini.reloc"

    # Negative: the unresolved B/J references must not have been folded
    # away by the assembler. Linking without their provider must fail.
    if "$ld" "${linker_mode[@]}" --no-relax -e strong \
            -o "$work/missing.elf" "$work/mini.o" \
            >"$work/missing.log" 2>&1; then
        echo "Unresolved RISC-V branch survived link ($march)" >&2
        exit 1
    fi
    grep -q 'undefined reference to' "$work/missing.log"

    # Link-time strong override must replace a same-section WEAK definition.
    # The undefined symbol is independently resolved. Both are P11 hazards:
    # resolving them at assembly time would discard linker mutability.
    printf '.text\n.globl weak_symbol\nweak_symbol:\n nop\n.globl external_symbol\nexternal_symbol:\n nop\n' \
        >"$work/override.s"
    "$gas" "-march=$march" "-mabi=$abi" -o "$work/override.o" \
        "$work/override.s"
    "$ld" "${linker_mode[@]}" --no-relax -e strong -o "$work/gnu.elf" \
        "$work/gnu.o" "$work/override.o"
    "$ld" "${linker_mode[@]}" --no-relax -e strong -o "$work/mini.elf" \
        "$work/mini.o" "$work/override.o"
    "$copy" -O binary -j .text "$work/gnu.elf" "$work/gnu.linked.bin"
    "$copy" -O binary -j .text "$work/mini.elf" "$work/mini.linked.bin"
    cmp "$work/gnu.linked.bin" "$work/mini.linked.bin"

    count=$(wc -l <"$work/mini.reloc")
    printf 'MINIAS_RV_BRANCH_BINDING=PASS march=%s mode=%s GNU_bytes=exact RELA=%s linked_override=exact missing_symbol=rejected\n' \
        "$march" "$cmode" "$count"
done
