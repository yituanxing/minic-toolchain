#!/bin/sh
# AArch64-only, whole-family Core TF -> MachineIR -> AAPCS64 source assembly.
# No runtime helper name is inferred from a user-defined C function spelling.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host=${HOST_CC:-gcc}
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT HUP INT TERM

"$host" -std=gnu11 -E -P \
    "$root/tests/compiler/c0/aarch64_typed_extended_float_abi.c" \
    -o "$work/typed.i"
"$mini" --target=aarch64-linux -S "$work/typed.i" -o "$work/a64.s"

for symbol in __addtf3 __subtf3 __multf3 __divtf3 __negtf2 \
    __eqtf2 __lttf2 __letf2 __extendsftf2 __extenddftf2 \
    __trunctfsf2 __trunctfdf2 __floatsitf __floatunsitf \
    __floatditf __floatunditf __fixtfsi __fixunstfsi \
    __fixtfdi __fixunstfdi; do
    if ! grep -Fq "  bl $symbol" "$work/a64.s"; then
        echo "A64_TF_ABI=FAIL lost typed runtime helper $symbol" >&2
        exit 1
    fi
done
for physical in '  str q0,' '  str q1,' '  ldr q0,' '  ldr q1,'; do
    if ! grep -Fq "$physical" "$work/a64.s"; then
        echo "A64_TF_ABI=FAIL lost Q-register ABI transport: $physical" >&2
        exit 1
    fi
done
for condition in eq lt le; do
    if ! grep -Fq "  cset w0, $condition" "$work/a64.s"; then
        echo "A64_TF_ABI=FAIL lost ordered predicate $condition" >&2
        exit 1
    fi
done
if grep -Fq '@core.float.' "$work/a64.s"; then
    echo 'A64_TF_ABI=FAIL leaked Core intrinsic symbol into GNU assembly' >&2
    exit 1
fi

# The ordinary user function has the very same public spelling as the
# runtime helper. Core's separate identity must select it as a normal call.
"$host" -std=gnu11 -E -P \
    "$root/tests/compiler/c0/probe-typed-extended-float-runtime.c" \
    -o "$work/named.i"
"$mini" --target=aarch64-linux -S "$work/named.i" -o "$work/named.s"
grep -Fq '.globl __addtf3' "$work/named.s"
grep -Fq '  bl __addtf3' "$work/named.s"
for condition in eq lt le; do
    grep -Fq "  cset w0, $condition" "$work/named.s"
done

gnu_as_status=SKIP
cross_as=${AARCH64_GNU_AS:-aarch64-linux-gnu-as}
if command -v "$cross_as" >/dev/null 2>&1; then
    "$cross_as" "$work/a64.s" -o "$work/a64.o"
    "$cross_as" "$work/named.s" -o "$work/named.o"
    gnu_as_status=PASS
fi
echo "A64_TF_ABI=PASS Core_TF=20 AAPCS64_Q=1 ordered_comparisons=3 distinct_user_symbol=1 GNU_AS=$gnu_as_status"
