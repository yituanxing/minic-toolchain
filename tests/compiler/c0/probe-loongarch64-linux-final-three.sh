#!/bin/sh
# Exact LoongArch Linux 6.6.143 last original native-defconfig ASM forms,
# and UDF's size_t-constant/u32 -> signed-i32 checked-addition semantics.
set -eu
: "${MINIC_CC:?point MINIC_CC at the built compiler}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

asm=tests/compiler/c0/loongarch64_linux_final_opaque_asm.c
udf=tests/compiler/c0/loongarch64_linux_udf_current_aext_overflow.c

gcc -E -P -nostdinc -x c "$asm" -o "$work/asm.i"
"$MINIC_CC" --target=loongarch64-linux -fPIE -S \
    "$work/asm.i" -o "$work/asm.s"
test "$(grep -Fc '.pushsection __bug_table, "aw"' "$work/asm.s")" -eq 4
test "$(grep -Fc '.short 413; .short 0;' "$work/asm.s")" -eq 2
test "$(grep -Fc '.short 435; .short 0;' "$work/asm.s")" -eq 2
test "$(grep -Fc 'ibar 0' "$work/asm.s")" -eq 1
test "$(grep -Fc 'dbar 0' "$work/asm.s")" -eq 3
test "$(grep -Fc 'idle 0' "$work/asm.s")" -eq 1
test "$(grep -Fc 'nop' "$work/asm.s")" -eq 1
echo 'MINIC_LOONG_FINAL_OPAQUE=PASS smp_bug=2 smp_barrier_idle_nop=3 relocate_ibar_dbar=1'

gcc -E -P -nostdinc -x c "$udf" -o "$work/udf.i"
"$MINIC_CC" --target=loongarch64-linux -fPIE -S \
    "$work/udf.i" -o "$work/udf.s"
test "$(grep -Fc '  xor $t4, $t3, $t0' "$work/udf.s")" -ge 5
test "$(grep -Fc '  st.w $t0, $t2, 0' "$work/udf.s")" -ge 5
echo 'MINIC_LOONG_FINAL_UDF=PASS exact_sizeof_u32_i32=2 i32_u32_both_orders=2'

gcc -std=gnu11 -O2 -Wall -Wextra -Werror -DMINIC_GNU_NATIVE_ORACLE \
    "$udf" -o "$work/oracle"
"$work/oracle"

gcc -E -P -nostdinc -x c -DMINIC_LOONG_UNSUPPORTED_DIFFERENT_SHAPE \
    "$udf" -o "$work/negative-udf.i"
if "$MINIC_CC" --target=loongarch64-linux -fPIE -S \
    "$work/negative-udf.i" -o "$work/negative-udf.s" \
    2>"$work/negative-udf.err"; then
    echo 'MINIC_LOONG_FINAL_NEGATIVE=FAIL unsupported overflow shape admitted' >&2
    exit 1
fi
test ! -e "$work/negative-udf.s"

gcc -E -P -nostdinc -x c -DMINIC_LOONG_UNSUPPORTED_RAW_ASM \
    "$asm" -o "$work/negative-asm.i"
if "$MINIC_CC" --target=loongarch64-linux -fPIE -S \
    "$work/negative-asm.i" -o "$work/negative-asm.s" \
    2>"$work/negative-asm.err"; then
    echo 'MINIC_LOONG_FINAL_NEGATIVE=FAIL arbitrary multiline ASM admitted' >&2
    exit 1
fi
test ! -e "$work/negative-asm.s"
echo 'MINIC_LOONG_FINAL_NEGATIVE=PASS unsupported_arithmetic_and_raw_asm=rejected'
