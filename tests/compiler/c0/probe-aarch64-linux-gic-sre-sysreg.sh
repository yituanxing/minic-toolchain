#!/usr/bin/env bash
# GNU/Linux ICC_SRE_EL1 original encoded read+write ISA; prove actual MSR/MRS
# instruction encodings with GNU GAS/objdump, but never execute at EL0.
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
mini=${1:-${MINIC_CC:-"$root/build/release/bin/minic-cc"}}
cc=${AARCH64_CC:-aarch64-linux-gnu-gcc}
objdump=${AARCH64_OBJDUMP:-aarch64-linux-gnu-objdump}
fixture="$root/tests/compiler/c0/aarch64_linux_gic_sre_sysreg.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { printf 'AARCH64_GIC_SRE=FAIL %s\n' "$*" >&2; exit 1; }

test -x "$mini" || fail 'MiniC compiler absent'
test -s "$fixture" || fail 'original sysreg macro source absent'
gcc -E -P -std=gnu11 -x c "$fixture" -o "$work/gnu.i"
"$mini" --target=aarch64-linux -S "$work/gnu.i" -o "$work/mini.s" \
    || fail 'exact original MRS/MSR GNU macro not admitted'
test -s "$work/mini.s" || fail 'no generated assembly'

# The source .macro was specialized to a real physical register x9 by the
# AArch64 target. Do not accept an invented C call or compiler-only barrier.
# The two distinct opcode bases separate read and write completely.
grep -Eq '^  \.inst 0xd53[0-9a-f]{5}$' "$work/mini.s" ||
    fail 'MRS encoded hardware instruction missing'
grep -Eq '^  \.inst 0xd51[0-9a-f]{5}$' "$work/mini.s" ||
    fail 'MSR encoded hardware instruction missing'
grep -Eq '^  ldr x9, \[sp, #[0-9]+\]$' "$work/mini.s" ||
    fail 'MSR incoming typed value not loaded into x9'

# Three independent dangerous edits plus foreign target must fail closed.
sed 's/((5) << 5)/((4) << 5)/g' "$fixture" >"$work/wrong-reg.c"
sed 's/"rZ" (value)/"r" (value)/' "$fixture" >"$work/wrong-constraint.c"
sed 's/\.purgem\\tmsr_s/.purgem\\tmsr_y/' \
    "$fixture" >"$work/wrong-macro.c"
sed 's/0xd5000000/0xd5200000/g' "$fixture" >"$work/wrong-opcode.c"
for variant in wrong-reg wrong-constraint wrong-macro wrong-opcode; do
    cmp -s "$fixture" "$work/$variant.c" &&
        fail "negative mutation ineffective: $variant"
    gcc -E -P -std=gnu11 -x c "$work/$variant.c" -o "$work/$variant.i"
    if "$mini" --target=aarch64-linux -S "$work/$variant.i" \
        -o "$work/$variant.s" >"$work/$variant.out" \
        2>"$work/$variant.err"; then
        fail "unsafe GNU MSR macro accepted: $variant"
    fi
    test ! -e "$work/$variant.s" || fail "partial output: $variant"
done
if "$mini" --target=loongarch64-linux -S "$work/gnu.i" \
    -o "$work/foreign.s" >"$work/foreign.out" 2>"$work/foreign.err"; then
    fail 'AArch64 privileged SYSREG accepted in LoongArch ISA'
fi
test ! -e "$work/foreign.s" || fail 'partial foreign-ISA output'
printf '%s\n' 'AARCH64_GIC_SRE_SOURCE=PASS kernel_macro=2 hardware=2 typed_value=1 negative=5'

if ! command -v "$cc" >/dev/null 2>&1 ||
   ! command -v "$objdump" >/dev/null 2>&1; then
    if [[ ${MINIC_REQUIRE_AARCH64_GIC_SRE:-0} == 1 ]]; then
        fail 'required GNU AArch64 GCC/objdump absent'
    fi
    printf '%s\n' 'AARCH64_GIC_SRE_GNU_AS=SKIP independent AArch64 tools absent'
    exit 0
fi
"$cc" -O0 -c -x c "$fixture" -o "$work/gnu.o"
"$cc" -c -x assembler "$work/mini.s" -o "$work/mini.o"
"$objdump" -dr "$work/gnu.o" >"$work/gnu.dump"
"$objdump" -dr "$work/mini.o" >"$work/mini.dump"
for producer in gnu mini; do
    grep -Eq '[[:space:]]mrs[[:space:]]+x[0-9]+,' "$work/$producer.dump" ||
        fail "$producer does not contain encoded MRS"
    grep -Eq '[[:space:]]msr[[:space:]]+' "$work/$producer.dump" ||
        fail "$producer does not contain encoded MSR"
done
printf '%s\n' 'AARCH64_GIC_SRE_GNU_AS=PASS original_GAS=1 MiniC_GAS=1 ISA_MRS=1 ISA_MSR=1 privileged_EL0_execution=0'
