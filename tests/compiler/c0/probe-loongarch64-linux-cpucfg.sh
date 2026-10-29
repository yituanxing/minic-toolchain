#!/bin/sh
# The real Linux lsdc_probe CPUCFG query is a register-to-register LoongArch
# ISA operation. Qualify only the exact GNU template, preserve its runtime
# input and early-clobber output, and reject unknown privileged templates.
set -eu
: "${MINIC_CC:?point MINIC_CC at the built compiler}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
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
grep -Fq '  st.w $t0, $t2, 0' "$work/cpucfg.s"
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
