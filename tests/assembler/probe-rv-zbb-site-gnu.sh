#!/usr/bin/env bash
# GNU .option arch,+zbb is a site-local instruction permission, not an ELF
# Tag_RISCV_arch upgrade. push/pop must restore it even under explicit -march.
set -Eeuo pipefail
: "${MINIAS:?exact MiniAS binary required}"
for program in riscv64-linux-gnu-as riscv64-linux-gnu-objcopy readelf cmp; do
    if ! command -v "$program" >/dev/null 2>&1; then
        echo "MINIAS_RV_ZBB_SITE=SKIP missing=$program"
        exit 0
    fi
done
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat >"$work/region.s" <<'ASM'
.option norvc
.text
.option push
.option arch, +zbb
.globl zbb_region
zbb_region:
  ctz a0, a0
  orc.b a1, a1
.option push
  rev8 a2, a2
.option pop
  ctz a3, a3
.option pop
  add a0, a0, a1
  ret
ASM
cat >"$work/baseline.s" <<'ASM'
.option norvc
.text
.globl zbb_region
zbb_region:
  add a0, a0, a1
  ret
ASM
cat >"$work/outside.s" <<'ASM'
.option norvc
.text
.option push
.option arch,+zbb
  ctz a0,a0
.option pop
  ctz a1,a1
ASM
cat >"$work/no-region.s" <<'ASM'
.option norvc
.text
  ctz a0,a0
ASM

for xlen in 32 64; do
    if test "$xlen" = 32; then
        march=rv32imafdc_zicsr_zifencei
        mabi=ilp32
    else
        march=rv64imafdc_zicsr_zifencei_zihintpause
        mabi=lp64
    fi
    for stem in region baseline; do
        riscv64-linux-gnu-as "-march=$march" "-mabi=$mabi" \
            -mno-relax -o "$work/$xlen-$stem.gnu.o" "$work/$stem.s"
        "$MINIAS" "-march=$march" "-mabi=$mabi" \
            -o "$work/$xlen-$stem.mini.o" "$work/$stem.s"
    done
    for producer in gnu mini; do
        readelf -A "$work/$xlen-region.$producer.o" \
            >"$work/$xlen-region.$producer.attr"
        readelf -A "$work/$xlen-baseline.$producer.o" \
            >"$work/$xlen-baseline.$producer.attr"
        cmp "$work/$xlen-region.$producer.attr" \
            "$work/$xlen-baseline.$producer.attr"
        grep -q 'Tag_RISCV_arch:' "$work/$xlen-region.$producer.attr"
        if grep -q 'zbb' "$work/$xlen-region.$producer.attr"; then
            echo "MINIAS_RV_ZBB_SITE=FAIL file-wide-Zbb-$xlen-$producer" >&2
            exit 1
        fi
        if readelf -Wr "$work/$xlen-region.$producer.o" | \
                grep -q 'R_RISCV_'; then
            echo "MINIAS_RV_ZBB_SITE=FAIL unexpected-reloc-$xlen-$producer" >&2
            exit 1
        fi
        riscv64-linux-gnu-objcopy -O binary -j .text \
            "$work/$xlen-region.$producer.o" \
            "$work/$xlen-region.$producer.bin"
    done
    cmp "$work/$xlen-region.gnu.bin" "$work/$xlen-region.mini.bin"

    # The pop restores the explicit base ISA. No Zbb instruction outside
    # the region, or in a source with no region, may pass either assembler.
    for stem in outside no-region; do
        if riscv64-linux-gnu-as "-march=$march" "-mabi=$mabi" \
            -mno-relax -o "$work/$xlen-$stem.gnu.o" "$work/$stem.s" \
            >"$work/$xlen-$stem.gnu.out" \
            2>"$work/$xlen-$stem.gnu.err"; then
            echo "MINIAS_RV_ZBB_SITE=FAIL GNU-accepted-$xlen-$stem" >&2
            exit 1
        fi
        if "$MINIAS" "-march=$march" "-mabi=$mabi" \
            -o "$work/$xlen-$stem.mini.o" "$work/$stem.s" \
            >"$work/$xlen-$stem.mini.out" \
            2>"$work/$xlen-$stem.mini.err"; then
            echo "MINIAS_RV_ZBB_SITE=FAIL MiniAS-accepted-$xlen-$stem" >&2
            exit 1
        fi
        test ! -e "$work/$xlen-$stem.mini.o"
        grep -q 'unsupported-isa-feature:ctz' \
            "$work/$xlen-$stem.mini.err"
    done

    # A local +zbb grant cannot turn into a generic arbitrary ISA escape.
    # +v still requires a separate verified site policy, despite GNU's
    # broader support; unknown extensions and +m remain fail-closed.
    for option in 'arch,+v' 'arch,+m' 'arch,+xunknown' 'arch,-zbb'; do
        printf '.text\n.option %s\n' "$option" >"$work/unsupported.s"
        if "$MINIAS" "-march=$march" "-mabi=$mabi" \
            -o "$work/$xlen-unsupported.mini.o" "$work/unsupported.s" \
            >"$work/$xlen-unsupported.mini.out" \
            2>"$work/$xlen-unsupported.mini.err"; then
            echo "MINIAS_RV_ZBB_SITE=FAIL unexpected-$xlen-$option" >&2
            exit 1
        fi
        test ! -e "$work/$xlen-unsupported.mini.o"
        grep -q 'unsupported-isa-option:' "$work/$xlen-unsupported.mini.err"
    done
done

# The strongest case is an explicit pure Base-I profile with no other
# extensions: a lexical +zbb may admit CTZ, but its pop must restore I-only.
cat >"$work/base-i.s" <<'ASM'
.text
.option push
.option arch,+zbb
  ctz a0,a0
.option pop
  addi a0,a0,1
  ret
ASM
cat >"$work/base-i-baseline.s" <<'ASM'
.text
  addi a0,a0,1
  ret
ASM
for xlen in 32 64; do
    march=rv${xlen}i
    if test "$xlen" = 32; then mabi=ilp32; else mabi=lp64; fi
    for stem in base-i base-i-baseline; do
        riscv64-linux-gnu-as "-march=$march" "-mabi=$mabi" -mno-relax \
            -o "$work/$xlen-$stem.gnu.o" "$work/$stem.s"
        "$MINIAS" "-march=$march" "-mabi=$mabi" \
            -o "$work/$xlen-$stem.mini.o" "$work/$stem.s"
    done
    for producer in gnu mini; do
        readelf -A "$work/$xlen-base-i.$producer.o" \
            >"$work/$xlen-base-i.$producer.attr"
        readelf -A "$work/$xlen-base-i-baseline.$producer.o" \
            >"$work/$xlen-base-i-baseline.$producer.attr"
        cmp "$work/$xlen-base-i.$producer.attr" \
            "$work/$xlen-base-i-baseline.$producer.attr"
        if grep -q 'zbb' "$work/$xlen-base-i.$producer.attr"; then
            echo "MINIAS_RV_ZBB_SITE=FAIL base-i-filewide-zbb-$xlen" >&2
            exit 1
        fi
        if readelf -Wr "$work/$xlen-base-i.$producer.o" | grep -q 'R_RISCV_'; then
            echo "MINIAS_RV_ZBB_SITE=FAIL base-i-reloc-$xlen-$producer" >&2
            exit 1
        fi
        riscv64-linux-gnu-objcopy -O binary -j .text \
            "$work/$xlen-base-i.$producer.o" \
            "$work/$xlen-base-i.$producer.bin"
    done
    cmp "$work/$xlen-base-i.gnu.bin" "$work/$xlen-base-i.mini.bin"
    if riscv64-linux-gnu-as "-march=$march" "-mabi=$mabi" -mno-relax \
        -o "$work/$xlen-base-i-outside.gnu.o" "$work/outside.s" \
        >"$work/base-i-outside.gnu.out" 2>"$work/base-i-outside.gnu.err"; then
        echo "MINIAS_RV_ZBB_SITE=FAIL GNU-base-i-outside-$xlen" >&2
        exit 1
    fi
    if "$MINIAS" "-march=$march" "-mabi=$mabi" \
        -o "$work/$xlen-base-i-outside.mini.o" "$work/outside.s" \
        >"$work/base-i-outside.mini.out" 2>"$work/base-i-outside.mini.err"; then
        echo "MINIAS_RV_ZBB_SITE=FAIL MiniAS-base-i-outside-$xlen" >&2
        exit 1
    fi
    grep -q 'unsupported-isa-feature:ctz' "$work/base-i-outside.mini.err"
done

echo 'MINIAS_RV_ZBB_SITE=PASS RV32_RV64_GNU_text=exact reloc=0 file_arch=unchanged nested_push_pop=2 outside_rejected=4 unsupported_options=8 pure_I_scopes=2'
