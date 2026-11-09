#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
src="$root/tests/compiler/c0/aarch64_linux_smmu_dynamic_dmb.c"

"$host" -E -P -std=gnu11 "$src" -o "$work/real.i"
"$mini" --target=aarch64-linux -S "$work/real.i" -o "$work/real.s"
# Both fixed and dynamically-sized frames retain the ISA memory barrier.
test "$(grep -c '^  dmb oshst$' "$work/real.s")" -eq 2
sed -n '/^smmu_dynamic_barrier:/,/^.size smmu_dynamic_barrier/p' \
    "$work/real.s" | grep -Eq '^  sub sp, sp, x[0-9]+'

# A foreign ISA must not interpret the AArch64 hardware barrier as a
# compiler-only instruction, even though the C source itself is valid.
for target in x86_64 loongarch64; do
    if "$mini" --target="$target-linux" -S "$work/real.i" \
        -o "$work/foreign.s" >/dev/null 2>&1; then
        echo "SMMU_DYNAMIC_DMB=FAIL foreign=$target" >&2; exit 1
    fi
done

# Mutating the exact privileged instruction or inventing a GNU input role
# must not widen admission for variable-frame structured asm.
sed 's/dmb oshst/dmb oshst; nop/g' "$src" >"$work/extra.c"
"$host" -E -P -std=gnu11 "$work/extra.c" -o "$work/extra.i"
"$host" -E -P -std=gnu11 -DMINIC_DMB_BAD_INPUT \
    "$src" -o "$work/input.i"
for case_name in extra input; do
    if "$mini" --target=aarch64-linux -S "$work/$case_name.i" \
        -o "$work/$case_name.s" >/dev/null 2>&1; then
        echo "SMMU_DYNAMIC_DMB=FAIL invalid=$case_name" >&2; exit 1
    fi
done

if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    aarch64-linux-gnu-gcc -c "$work/real.s" -o "$work/real.o"
    echo 'SMMU_DYNAMIC_DMB=PASS barriers=2 dynamic_frame=1 foreign=2 negative=2 GNU_GAS=PASS'
else
    echo 'SMMU_DYNAMIC_DMB=PASS barriers=2 dynamic_frame=1 foreign=2 negative=2 GNU_GAS=SKIP'
fi
