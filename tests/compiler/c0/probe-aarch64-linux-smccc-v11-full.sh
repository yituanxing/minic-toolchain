#!/bin/sh
# Original SMCCCv1.1 fixed-register x0..x7 ABI; never execute EL1 HVC/SMC.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
src="$root/tests/compiler/c0/aarch64_linux_smccc_v11_full.c"
"${HOST_CC:-cc}" -E -P -std=gnu11 "$src" -o "$work/real.i"
"$mini" --target=aarch64-linux -S "$work/real.i" -o "$work/real.s"
test "$(grep -Ec '^  hvc #0$' "$work/real.s")" -eq 6
test "$(grep -Ec '^  smc #0$' "$work/real.s")" -eq 3
grep -Eq '^  ldr x7, \[sp, #[0-9]+\]$' "$work/real.s"
grep -Eq '^  str x3, \[x12\]$' "$work/real.s"
for target in x86_64 loongarch64; do
    if "$mini" --target="$target-linux" -S "$work/real.i" \
        -o "$work/foreign.s" >/dev/null 2>&1; then
        echo "AARCH64_SMCCC=FAIL foreign=$target" >&2; exit 1
    fi
done
sed 's/register unsigned long arg7 asm("r7")/register unsigned long arg7 asm("r6")/' \
    "$src" >"$work/bad-binding.c"
sed 's/: "memory");/: "cc");/g' "$src" >"$work/bad-clobber.c"
sed 's/#0/#1/g' "$src" >"$work/bad-opcode.c"
sed 's/"=r" (r0)/"=m" (r0)/' "$src" >"$work/bad-output.c"
sed 's/register void \*arg1 asm("r1")/register void *arg1 asm("r2")/' \
    "$src" >"$work/bad-pointer-binding.c"
sed 's/register unsigned long long arg1 asm("r1")/register unsigned long long arg1 asm("r2")/' \
    "$src" >"$work/bad-phys-binding.c"
sed 's/register enum minic_hyp_vector arg1 asm("r1")/register enum minic_hyp_vector arg1 asm("r2")/' \
    "$src" >"$work/bad-enum-binding.c"
sed 's/register const unsigned long long arg2 asm("r2")/register const unsigned long long arg2 asm("r3")/' \
    "$src" >"$work/bad-const-binding.c"
for variant in bad-binding bad-clobber bad-opcode bad-output bad-pointer-binding bad-phys-binding bad-enum-binding bad-const-binding; do
    "${HOST_CC:-cc}" -E -P -std=gnu11 "$work/$variant.c" -o "$work/$variant.i"
    if "$mini" --target=aarch64-linux -S "$work/$variant.i" \
        -o "$work/$variant.s" >/dev/null 2>&1; then
        echo "AARCH64_SMCCC=FAIL unsafe-$variant" >&2; exit 1
    fi
    test ! -e "$work/$variant.s"
done
if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    aarch64-linux-gnu-gcc -c "$work/real.s" -o "$work/mini.o"
    aarch64-linux-gnu-gcc -c "$src" -o "$work/gnu.o"
    echo 'AARCH64_SMCCC=PASS call_forms=9 in=1,2,4,8 ptr=1 u64=1 enum=1 const_u64=1 out=4 negative=8 GNU_GAS=PASS'
else
    echo 'AARCH64_SMCCC=PASS call_forms=9 in=1,2,4,8 ptr=1 u64=1 enum=1 const_u64=1 out=4 negative=8 GNU_GAS=SKIP'
fi
