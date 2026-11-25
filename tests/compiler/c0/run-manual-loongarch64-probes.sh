#!/bin/sh
# Named manual probes; shared initialization is exact, case assertions are unchanged.
if [ "${1:-}" = "--list" ]; then
    printf '%s\n' 'probe-loongarch64-linux-cpucfg' 'probe-loongarch64-linux-final-three' 'probe-loongarch64-linux-mixed-mul-overflow' 'probe-loongarch64-pie-got-local'
    exit 0
fi
__minic_probe_case=${1:?probe case required; use --list}
shift
case "$__minic_probe_case" in
  probe-loongarch64-linux-cpucfg|probe-loongarch64-linux-final-three|probe-loongarch64-linux-mixed-mul-overflow|probe-loongarch64-pie-got-local) ;;
  *) printf "%s\n" "unknown probe: $__minic_probe_case" >&2; exit 64 ;;
esac
set -eu
: "${MINIC_CC:?point MINIC_CC at the built compiler}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
case "$__minic_probe_case" in
probe-loongarch64-linux-cpucfg)
# The real Linux lsdc_probe CPUCFG query is a register-to-register LoongArch
# ISA operation. Qualify only the exact GNU template, preserve its runtime
# input and early-clobber output, and reject unknown privileged templates.
cat >"$work/cpucfg.i" <<'SRC'
unsigned int loongson_cpu_get_prid(void) {
    unsigned int prid;
    __asm__ volatile("cpucfg %0, %1\n\t" : "=&r"(prid) : "r"(0));
    return prid;
}
unsigned int loongson_cpu_get_cfg(unsigned int index) {
    unsigned int cfg;
    __asm__ volatile("cpucfg %0, %1\n\t" : "=&r"(cfg) : "r"(index));
    return cfg;
}
SRC
"$MINIC_CC" --target=loongarch64-linux -fPIE -S \
  "$work/cpucfg.i" -o "$work/cpucfg.s"
test "$(grep -Fc '  cpucfg $t0, $t1' "$work/cpucfg.s")" -eq 2
# A physical lvalue pointer may be retained in t7 or rematerialized in t1.
# Either way the early-clobber cpucfg result must be stored as the exact
# 32-bit t0 value, after both scalar input variants have executed.
# Other C stack/local stores may also happen to use t0. Verify each GNU
# source occurrence and its output writeback within its own function.
for fn in loongson_cpu_get_prid loongson_cpu_get_cfg; do
    sed -n "/^${fn}:/,/^\\.size ${fn},/p" "$work/cpucfg.s" >"$work/$fn.s"
    test "$(grep -Fc '  cpucfg $t0, $t1' "$work/$fn.s")" -eq 1
    grep -Eq '^  st\.w \$t0, \$(t7|t1), 0$' "$work/$fn.s"
done
echo 'MINIC_LOONGARCH64_CPUCFG_SHAPE=PASS constant_index=1 runtime_index=1'

cross=${LOONGARCH64_CC:-loongarch64-linux-gnu-gcc-13}
if command -v "$cross" >/dev/null 2>&1; then
    "$cross" -c -x assembler "$work/cpucfg.s" -o "$work/cpucfg.o"
    echo 'MINIC_LOONGARCH64_CPUCFG_GNU_AS=PASS'
else
    echo 'MINIC_LOONGARCH64_CPUCFG_GNU_AS=SKIP missing Loong GNU assembler'
fi

sed 's/cpucfg %0, %1/cpucfg %0, %1; syscall 0/' \
  "$work/cpucfg.i" >"$work/invalid.i"
if "$MINIC_CC" --target=loongarch64-linux -fPIE -S \
    "$work/invalid.i" -o "$work/invalid.s" >/dev/null 2>"$work/invalid.err"; then
    echo 'MINIC_LOONGARCH64_CPUCFG=FAIL unknown instruction accepted' >&2
    exit 1
fi
test ! -e "$work/invalid.s"
echo 'MINIC_LOONGARCH64_CPUCFG_NEGATIVE=PASS unknown_instruction=rejected'
;;
probe-loongarch64-linux-final-three)
# Exact LoongArch Linux 6.6.143 last original native-defconfig ASM forms,
# and UDF's size_t-constant/u32 -> signed-i32 checked-addition semantics.

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
;;
probe-loongarch64-linux-mixed-mul-overflow)
# The frozen Linux drm_mode_vrefresh and fbcon_set_font emit checked mixed
# signed/unsigned 32-bit multiplication with differing result signedness.
src=tests/compiler/c0/loongarch64_linux_mixed_mul_overflow.c
host=${HOST_CC:-cc}
"$host" -E -P -nostdinc "$src" -o "$work/mixed.i"
"$MINIC_CC" --target=loongarch64-linux -fPIE -S \
    "$work/mixed.i" -o "$work/mixed.s"
test "$(grep -Fc '  mul.d $t3, $t0, $t1' "$work/mixed.s")" -eq 3
test "$(grep -Fc '  srli.d $t4, $t3, 32' "$work/mixed.s")" -eq 2
test "$(grep -Fc '  xor $t4, $t3, $t0' "$work/mixed.s")" -eq 1
grep -Fq '  bstrpick.d $t0, $t0, 31, 0' "$work/mixed.s"
grep -Fq '  bstrpick.d $t1, $t1, 31, 0' "$work/mixed.s"
echo 'MINIC_LOONGARCH64_MIXED_MUL=PASS unsigned_result=2 signed_result=1 64bit_exact=3'

cross=${LOONGARCH64_CC:-loongarch64-linux-gnu-gcc-13}
if command -v "$cross" >/dev/null 2>&1; then
    "$cross" -c -x assembler "$work/mixed.s" -o "$work/mixed.o"
    echo 'MINIC_LOONGARCH64_MIXED_MUL_GNU_AS=PASS'
else
    echo 'MINIC_LOONGARCH64_MIXED_MUL_GNU_AS=SKIP missing Loong GNU assembler'
fi

if command -v "$host" >/dev/null 2>&1; then
    "$host" -DMINI_MIXED_OVERFLOW_HOST_TEST -O2 "$src" -o "$work/reference"
    "$work/reference"
    echo 'MINIC_LOONGARCH64_MIXED_MUL_GNU_ORACLE=PASS combinations=72'
else
    echo 'MINIC_LOONGARCH64_MIXED_MUL_GNU_ORACLE=SKIP missing host C compiler'
fi
;;
probe-loongarch64-pie-got-local)
# A native CONFIG_RELOCATABLE LoongArch Linux kernel emits -fPIE. Verify that
# its TU-local symbols avoid GOT indirection and preemptible symbols use GOT.
# The shape gate runs without cross tools; GNU AS/LD/QEMU comparison is added
# when the cross compiler and emulator are available.
cat >"$work/pic.i" <<'SRC'
extern int external_value;
extern int external_add(int);
static int local_value = 17;
static int local_add(int x) { return x + 3; }
int *get_local_object(void) { return &local_value; }
int *get_external_object(void) { return &external_value; }
int (*get_local_function(void))(int) { return &local_add; }
int (*get_external_function(void))(int) { return &external_add; }
int loong_pie_entry(int x) {
    return *get_local_object() + *get_external_object() +
           get_local_function()(x) + get_external_function()(x);
}
SRC
for flag in -fPIE -fPIC; do
  "$MINIC_CC" --target=loongarch64-linux "$flag" -S \
    "$work/pic.i" -o "$work/minic.s"
  grep -Fq 'la.local $t0, local_value' "$work/minic.s"
  grep -Fq 'la.local $t0, local_add' "$work/minic.s"
  grep -Fq 'la.global $t0, external_value' "$work/minic.s"
  grep -Fq 'la.global $t0, external_add' "$work/minic.s"
  cp "$work/minic.s" "$work/${flag#-}.s"
done
cmp "$work/fPIE.s" "$work/fPIC.s"
echo 'MINIC_LOONGARCH64_PIE_SHAPE=PASS local_pcrel=2 external_got=2 PIE=1 PIC=1'

cross=${LOONGARCH64_CC:-loongarch64-linux-gnu-gcc-13}
qemu=${QEMU_LOONGARCH64:-qemu-loongarch64}
if ! command -v "$cross" >/dev/null 2>&1 ||
   ! command -v "$qemu" >/dev/null 2>&1; then
  echo 'MINIC_LOONGARCH64_PIE_RUNTIME=SKIP missing GNU cross compiler/QEMU'
  exit 0
fi
cat >"$work/main.c" <<'SRC'
extern int loong_pie_entry(int);
int external_value = 19;
int external_add(int x) { return x + 7; }
int main(void) { return loong_pie_entry(5) == (17+19+8+12) ? 0 : 3; }
SRC
"$cross" -c -fPIE -x c "$work/pic.i" -o "$work/gnu.o"
"$cross" -c -fPIE "$work/main.c" -o "$work/main.o"
"$cross" -c -x assembler "$work/fPIE.s" -o "$work/minic.o"
for provider in gnu minic; do
  "$cross" -fPIE -pie "$work/$provider.o" "$work/main.o" \
    -o "$work/$provider.elf"
  timeout 45s "$qemu" -L /usr/loongarch64-linux-gnu \
    "$work/$provider.elf" >"$work/$provider.out" 2>"$work/$provider.err"
done
cmp "$work/gnu.out" "$work/minic.out"
cmp "$work/gnu.err" "$work/minic.err"
echo 'MINIC_LOONGARCH64_PIE_RUNTIME=PASS GNU_AS=1 GNU_LD=1 QEMU_GNU_diff=1'
;;
esac
