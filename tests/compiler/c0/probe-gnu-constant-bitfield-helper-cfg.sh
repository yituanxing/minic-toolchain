#!/bin/sh
# Strict GNU C preprocessing -> MiniC -S -> GNU AS/GNU cc native executable.
# The GNU -O2 product is an oracle, not a replacement C compiler phase.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${MINIC_CC:-"$root/build/runtime-progress/bin/minic-cc"}
host=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
source_file="$root/tests/compiler/c0/gnu_constant_bitfield_helper_cfg.c"

"$host" -E -P -std=gnu11 "$source_file" -o "$work/gnu.i"
"$host" -O2 -S -std=gnu11 "$source_file" -o "$work/gcc.s"
if grep -Eq '\b(invalid_mask|value_does_not_fit)\b' "$work/gcc.s"; then
    echo 'GNU_CONST_BITFIELD_HELPER_CFG=FAIL GNU -O2 retained forbidden call' >&2
    exit 1
fi
"$mini" --target=x86_64-linux -S "$work/gnu.i" -o "$work/minic.s"
if grep -Eq '\b(invalid_mask|value_does_not_fit)\b' "$work/minic.s"; then
    echo 'GNU_CONST_BITFIELD_HELPER_CFG=RED MiniC retained forbidden helper call' >&2
    exit 1
fi
"$host" "$work/minic.s" -o "$work/minic"
"$host" "$work/gcc.s" -o "$work/gcc"
"$work/gcc"
"$work/minic"

# A truly out-of-range compile-time value must keep its diagnostic guard.
# GCC rejects the source; MiniC must not delete the reachable call, and GNU
# linking MiniC assembly must fail on that unresolved external symbol.
sed 's/encode(3UL, 0x1cUL)/encode(8UL, 0x1cUL)/' \
    "$source_file" > "$work/invalid.c"
"$host" -E -P -std=gnu11 "$work/invalid.c" -o "$work/invalid.i"
if "$host" -O2 -S -std=gnu11 "$work/invalid.c" -o "$work/invalid-gcc.s" \
    >"$work/invalid-gcc.log" 2>&1; then
    echo 'GNU_CONST_BITFIELD_HELPER_CFG=FAIL GNU accepted oversized constant' >&2
    exit 1
fi
grep -Fq 'value does not fit' "$work/invalid-gcc.log"
"$mini" --target=x86_64-linux -S "$work/invalid.i" -o "$work/invalid-minic.s"
if ! grep -Eq '\bvalue_does_not_fit\b' "$work/invalid-minic.s"; then
    echo 'GNU_CONST_BITFIELD_HELPER_CFG=FAIL MiniC deleted reachable guard' >&2
    exit 1
fi
if "$host" "$work/invalid-minic.s" -o "$work/invalid" \
    >"$work/invalid-ld.log" 2>&1; then
    echo 'GNU_CONST_BITFIELD_HELPER_CFG=FAIL invalid program unexpectedly linked' >&2
    exit 1
fi
grep -Fq 'value_does_not_fit' "$work/invalid-ld.log"

# Independently guard the malformed-mask path in the same helper chain.
sed 's/0x1cUL/0x1aUL/g' "$source_file" > "$work/invalid-mask.c"
"$host" -E -P -std=gnu11 "$work/invalid-mask.c" -o "$work/invalid-mask.i"
if "$host" -O2 -S -std=gnu11 "$work/invalid-mask.c" \
    -o "$work/invalid-mask-gcc.s" >"$work/invalid-mask-gcc.log" 2>&1; then
    echo 'GNU_CONST_BITFIELD_HELPER_CFG=FAIL GNU accepted invalid field mask' >&2
    exit 1
fi
grep -Fq 'invalid mask' "$work/invalid-mask-gcc.log"
"$mini" --target=x86_64-linux -S "$work/invalid-mask.i" \
    -o "$work/invalid-mask-minic.s"
if ! grep -Eq '\binvalid_mask\b' "$work/invalid-mask-minic.s"; then
    echo 'GNU_CONST_BITFIELD_HELPER_CFG=FAIL MiniC deleted invalid-mask guard' >&2
    exit 1
fi
if "$host" "$work/invalid-mask-minic.s" -o "$work/invalid-mask" \
    >"$work/invalid-mask-ld.log" 2>&1; then
    echo 'GNU_CONST_BITFIELD_HELPER_CFG=FAIL invalid-mask program linked' >&2
    exit 1
fi
grep -Fq 'invalid_mask' "$work/invalid-mask-ld.log"
echo 'GNU_CONST_BITFIELD_HELPER_CFG=PASS GNU_O2_ASM=1 MiniC_ASM=1 GNU_LINK=1 native_runtime=1 invalid_guards=2 side_effect=1'
