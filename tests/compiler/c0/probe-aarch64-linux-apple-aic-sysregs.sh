#!/bin/sh
# Exact Apple M1 AIC SYS_* encodings; strict source/role and unknown-tuple gates.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
src="$root/tests/compiler/c0/aarch64_linux_apple_aic_sysregs.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
"${HOST_CC:-cc}" -std=gnu11 -E -P "$src" -o "$work/positive.i"
"$mini" --target=aarch64-linux -S "$work/positive.i" -o "$work/positive.s"
test "$(grep -Ec '^  \.inst 0xd5' "$work/positive.s")" -eq 6
for target in x86_64 loongarch64; do
    if "$mini" --target="$target-linux" -S "$work/positive.i" \
        -o "$work/foreign.s" >/dev/null 2>&1; then
        echo "APPLE_AIC_SYSREG=FAIL foreign=$target" >&2; exit 1
    fi
done
sed 's/WRITE_AIC_REG(ipi_rr_local,   5, 15, 0, 0)/WRITE_AIC_REG(ipi_rr_local,   5, 15, 7, 7)/' \
    "$src" > "$work/bad-tuple.c"
sed 's/"rZ" (value)/"m" (value)/' "$src" > "$work/bad-constraint.c"
sed 's/msr_s, sreg, rt/msr_bad, sreg, rt/' \
    "$src" > "$work/bad-macro.c"
sed 's/0xd5000000/0xd5100000/' \
    "$src" > "$work/bad-opcode.c"
for variant in bad-tuple bad-constraint bad-macro bad-opcode; do
    "${HOST_CC:-cc}" -std=gnu11 -E -P "$work/$variant.c" \
        -o "$work/$variant.i"
    if "$mini" --target=aarch64-linux -S "$work/$variant.i" \
        -o "$work/$variant.s" >/dev/null 2>&1; then
        echo "APPLE_AIC_SYSREG=FAIL unsafe=$variant" >&2; exit 1
    fi
done
if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    aarch64-linux-gnu-gcc -c "$work/positive.s" -o "$work/minic.o"
    aarch64-linux-gnu-gcc -c "$src" -o "$work/gnu.o"
    echo 'APPLE_AIC_SYSREG=PASS writes=6 foreign=2 negative=4 GAS=PASS'
else
    echo 'APPLE_AIC_SYSREG=PASS writes=6 foreign=2 negative=4 GAS=SKIP'
fi
