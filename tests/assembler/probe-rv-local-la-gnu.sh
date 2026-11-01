#!/usr/bin/env bash
# GNU's nonrelaxable same-section LOCAL LA/LLA contract: resolve the AUIPC+
# ADDI pair during assembly, without a synthetic anchor or PCREL relocation.
# Keep link-time mutable and cross-section references as paired fixups.
set -Eeuo pipefail
: "${MINIAS:?exact MiniAS binary required}"

for program in riscv64-linux-gnu-as riscv64-linux-gnu-objcopy readelf; do
    if ! command -v "$program" >/dev/null 2>&1; then
        echo "MINIAS_RV_LOCAL_LA=SKIP GNU_tool_missing=$program"
        exit 0
    fi
done

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
march=rv64ima_zicsr_zifencei_zihintpause
mabi=lp64

assemble() {
    local stem=$1
    riscv64-linux-gnu-as "-march=$march" "-mabi=$mabi" \
        -mno-relax -o "$work/$stem.gnu.o" "$work/$stem.s"
    "$MINIAS" "-march=$march" "-mabi=$mabi" \
        -o "$work/$stem.mini.o" "$work/$stem.s"
}

compare_text() {
    local stem=$1
    riscv64-linux-gnu-objcopy -j .text -O binary \
        "$work/$stem.gnu.o" "$work/$stem.gnu.bin"
    riscv64-linux-gnu-objcopy -j .text -O binary \
        "$work/$stem.mini.o" "$work/$stem.mini.bin"
    cmp "$work/$stem.gnu.bin" "$work/$stem.mini.bin"
}

cat >"$work/local.s" <<'ASM'
.option norelax
.text
.Lprevious:
  nop
.globl local_probe
.type local_probe,@function
local_probe:
  lla a0, .Lforward
  la a1, .Lprevious
.option push
.option pic
  lla a2, .Lforward
.option pop
  la a3, .Lforward+8
  ret
  .space 2048
.Lforward:
  ret
.size local_probe,.-local_probe
ASM
assemble local
compare_text local
for producer in gnu mini; do
    if readelf -Wr "$work/local.$producer.o" | grep -q 'R_RISCV_'; then
        echo "MINIAS_RV_LOCAL_LA=FAIL local-$producer-relocation" >&2
        exit 1
    fi
done
if readelf -Ws "$work/local.mini.o" | grep -q '\.Lminias_private_'; then
    echo 'MINIAS_RV_LOCAL_LA=FAIL unneeded-private-anchor' >&2
    exit 1
fi

# Exercise both sides of the PCREL HI20 rounding boundary, including
# negative displacements. All instruction sites remain 4-byte aligned:
# sites are 2048/2056/2064/2072 and targets -1/8/4112/4119.
cat >"$work/rounding.s" <<'ASM'
.option norelax
.text
.Llow:
  .word 0
  .space 2044
  lla a0, .Llow-1
  lla a1, .Llow+8
  lla a2, .Lforward
  lla a3, .Lforward+7
  .space 2032
.Lforward:
  .dword 0
ASM
assemble rounding
compare_text rounding
for producer in gnu mini; do
    if readelf -Wr "$work/rounding.$producer.o" | grep -q 'R_RISCV_'; then
        echo "MINIAS_RV_LOCAL_LA=FAIL rounding-$producer-relocation" >&2
        exit 1
    fi
done

cat >"$work/retained.s" <<'ASM'
.option norelax
.text
.globl retained_probe
retained_probe:
  lla a0, .Lcross_section
  lla a1, external_undefined
  lla a2, weak_local
.option push
.option pic
  la a3, external_got
.option pop
  ret
.weak weak_local
weak_local:
  ret
.section .rodata
.Lcross_section:
  .word 42
ASM
assemble retained
compare_text retained
for producer in gnu mini; do
    readelf -Wr "$work/retained.$producer.o" >"$work/retained.$producer.relocs"
    test "$(grep -c 'R_RISCV_PCREL_HI20' "$work/retained.$producer.relocs")" -eq 3
    test "$(grep -c 'R_RISCV_PCREL_LO12_I' "$work/retained.$producer.relocs")" -eq 4
    test "$(grep -c 'R_RISCV_GOT_HI20' "$work/retained.$producer.relocs")" -eq 1
    grep -q 'R_RISCV_PCREL_HI20.*external_undefined' "$work/retained.$producer.relocs"
    grep -q 'R_RISCV_PCREL_HI20.*weak_local' "$work/retained.$producer.relocs"
    grep -q 'R_RISCV_PCREL_HI20.*\.Lcross_section' "$work/retained.$producer.relocs"
done

# An ELF STB_LOCAL named source symbol is not a GNU disposable `.L` label:
# GAS retains the same-section PCREL pair. A `.set alias,.Ltemporary` target
# must likewise keep its relocation rather than be folded as an `.L` label.
cat >"$work/named.s" <<'ASM'
.option norelax
.text
.globl named_probe
named_probe:
  lla a0, named_local
  lla a1, alias_local
  ret
named_local:
  nop
.Lalias_target:
  nop
.set alias_local, .Lalias_target
ASM
assemble named
for producer in gnu mini; do
    readelf -Wr "$work/named.$producer.o" >"$work/named.$producer.relocs"
    test "$(grep -c 'R_RISCV_PCREL_HI20' "$work/named.$producer.relocs")" -eq 2
    test "$(grep -c 'R_RISCV_PCREL_LO12_I' "$work/named.$producer.relocs")" -eq 2
    grep -q 'R_RISCV_PCREL_HI20.*named_local' "$work/named.$producer.relocs"
done

# PIC `la` uses GOT for defined locals, undefined symbols and .L labels,
# including symbol addends. RV32 loads a GOT slot with LW; RV64 uses LD.
# PIC `lla` stays PCREL+ADDI rather than silently switching to GOT.
cat >"$work/pic-matrix.s" <<'ASM'
.option norelax
.text
.Lpic_temp:
  nop
named_pic:
.option pic
  la a0, named_pic
  la a1, external_pic
  la a2, named_pic+8
  la a3, external_pic+4
  la a6, .Lpic_temp
  lla a4, named_pic
  lla a5, external_pic
  ret
ASM
for xlen in 32 64; do
    if test "$xlen" = 32; then
        pic_march=rv32ima_zicsr_zifencei
        pic_mabi=ilp32
        pic_load=lw
    else
        pic_march=$march
        pic_mabi=$mabi
        pic_load=ld
    fi
    riscv64-linux-gnu-as "-march=$pic_march" "-mabi=$pic_mabi" \
        -mno-relax -o "$work/pic-$xlen.gnu.o" "$work/pic-matrix.s"
    "$MINIAS" "-march=$pic_march" "-mabi=$pic_mabi" \
        -o "$work/pic-$xlen.mini.o" "$work/pic-matrix.s"
    for producer in gnu mini; do
        readelf -Wr "$work/pic-$xlen.$producer.o" \
            >"$work/pic-$xlen.$producer.relocs"
        test "$(grep -c 'R_RISCV_GOT_HI20' "$work/pic-$xlen.$producer.relocs")" -eq 5
        test "$(grep -c 'R_RISCV_PCREL_HI20' "$work/pic-$xlen.$producer.relocs")" -eq 2
        test "$(grep -c 'R_RISCV_PCREL_LO12_I' "$work/pic-$xlen.$producer.relocs")" -eq 7
        grep -Eq 'R_RISCV_GOT_HI20.*named_pic \+ 8' \
            "$work/pic-$xlen.$producer.relocs"
        grep -Eq 'R_RISCV_GOT_HI20.*external_pic \+ 4' \
            "$work/pic-$xlen.$producer.relocs"
        riscv64-linux-gnu-objdump -d "$work/pic-$xlen.$producer.o" \
            >"$work/pic-$xlen.$producer.disasm"
        test "$(grep -Ec "[[:space:]]$pic_load[[:space:]]" \
            "$work/pic-$xlen.$producer.disasm")" -eq 5
        riscv64-linux-gnu-objcopy -j .text -O binary \
            "$work/pic-$xlen.$producer.o" "$work/pic-$xlen.$producer.bin"
    done
    # The first 44 bytes are the five GOT pairs plus one preceding NOP.
    # Later named-local LLA relocation input bits are a separate ABI gap.
    cmp <(head -c 44 "$work/pic-$xlen.gnu.bin") \
        <(head -c 44 "$work/pic-$xlen.mini.bin")
done

# A same-section addend beyond the signed PCREL AUIPC/ADDI range must not
# silently truncate into an immediate. GAS 2.40 rejects it during assembly;
# MiniAS conservatively retains its link-visible HI20+LO12 relocations.
cat >"$work/overflow.s" <<'ASM'
.option norelax
.text
.Loverflow_base:
  lla a0, .Loverflow_base + 2147485697
  ret
ASM
if riscv64-linux-gnu-as "-march=$march" "-mabi=$mabi" -mno-relax \
    -o "$work/overflow.gnu.o" "$work/overflow.s" \
    >"$work/overflow.gnu.stdout" 2>"$work/overflow.gnu.stderr"; then
    readelf -Wr "$work/overflow.gnu.o" | grep -q 'R_RISCV_PCREL_HI20' || {
        echo 'MINIAS_RV_LOCAL_LA=FAIL GAS silently folded PCREL overflow' >&2
        exit 1
    }
fi
"$MINIAS" "-march=$march" "-mabi=$mabi" \
    -o "$work/overflow.mini.o" "$work/overflow.s"
readelf -Wr "$work/overflow.mini.o" >"$work/overflow.mini.relocs"
grep -Eq 'R_RISCV_PCREL_HI20.*\+[[:space:]]*80000801' \
    "$work/overflow.mini.relocs"
grep -q 'R_RISCV_PCREL_LO12_I' "$work/overflow.mini.relocs"

# Positive GNU relaxation deliberately retains both PCREL relocations plus
# R_RISCV_RELAX markers; the semantic oracle runs GAS with -mno-relax.
cat >"$work/relax.s" <<'ASM'
.text
.Lrelax_base:
  lla a0, .Lrelax_target
.Lrelax_target:
  ret
ASM
riscv64-linux-gnu-as "-march=$march" "-mabi=$mabi" \
    -o "$work/relax.gnu.o" "$work/relax.s"
test "$(readelf -Wr "$work/relax.gnu.o" | grep -c 'R_RISCV_RELAX')" -eq 2

echo 'MINIAS_RV_LOCAL_LA=PASS GNU_norelax_text_exact=3 local_fixups=0 retained_fixups=8 named_local=preserved alias=preserved PIC_RV32_RV64_GOT=exact rounding=4 overflow=preserved relax_policy=distinguished'
