#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
src="$root/tests/compiler/c0/aarch64_linux_tegra_ndiv_sysreg.c"
"${HOST_CC:-cc}" -E -P -std=gnu11 "$src" -o "$work/real.i"
"$mini" --target=aarch64-linux -S "$work/real.i" -o "$work/real.s"
grep -q '  mrs x9, s3_0_c15_c0_4' "$work/real.s"
grep -q '  msr s3_0_c15_c0_4, x9' "$work/real.s"
for target in x86_64 loongarch64; do
    if "$mini" --target="$target-linux" -S "$work/real.i" \
        -o "$work/foreign.s" >/dev/null 2>&1; then
        echo "TEGRA_NDIV=FAIL foreign=$target" >&2; exit 1
    fi
done
sed 's/s3_0_c15_c0_4/s3_0_c15_c0_5/g' "$src" >"$work/bad.c"
"${HOST_CC:-cc}" -E -P -std=gnu11 "$work/bad.c" -o "$work/bad.i"
if "$mini" --target=aarch64-linux -S "$work/bad.i" -o "$work/bad.s" \
        >/dev/null 2>&1; then
    echo 'TEGRA_NDIV=FAIL unknown sysreg accepted' >&2; exit 1
fi
if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    aarch64-linux-gnu-gcc -c "$work/real.s" -o "$work/real.o"
    echo 'TEGRA_NDIV=PASS read=1 write=1 foreign=2 negative=1 GAS=PASS'
else
    echo 'TEGRA_NDIV=PASS read=1 write=1 foreign=2 negative=1 GAS=SKIP'
fi
