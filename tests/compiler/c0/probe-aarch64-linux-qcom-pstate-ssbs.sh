#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
src="$root/tests/compiler/c0/aarch64_linux_qcom_pstate_ssbs.c"

"$host" -E -P -std=gnu11 "$src" -o "$work/owner.i"
MINIC_BACKEND_CENSUS=1 "$mini" --target=aarch64-linux -S \
    "$work/owner.i" -o "$work/minic.s" 2>"$work/owner.log"
grep -Fq 'MINIC_BACKEND_CENSUS arch=aarch64 scanned=2 rejected=0' \
    "$work/owner.log"
grep -Fq 'mov x9, x30' "$work/minic.s"
grep -Fq 'mov x30, x9' "$work/minic.s"
grep -Fq '.rept 16' "$work/minic.s"
grep -Fq 'bl . + 4' "$work/minic.s"
test "$(grep -cF '.inst (0xd500401f' "$work/minic.s")" -eq 2

# Every exact source-ISA entry remains closed: one altered branch target,
# omitted BL, or unknown privileged selector fails before producing .s.
sed 's/\.rept 16/.rept 15/' "$src" >"$work/rept.c"
sed 's/bl \. + 4/bl . + 8/' "$src" >"$work/branch.c"
sed 's/((3) << 16 | (1) << 5)/((3) << 16 | (7) << 5)/' \
    "$src" >"$work/selector.c"
for bad in rept branch selector; do
    "$host" -E -P -std=gnu11 "$work/$bad.c" -o "$work/$bad.i"
    if "$mini" --target=aarch64-linux -S "$work/$bad.i" \
        -o "$work/$bad.s" >"$work/$bad.out" 2>"$work/$bad.err"; then
        echo "AARCH_QCOM_SSBS=FAIL accepted=$bad" >&2
        exit 1
    fi
    test ! -s "$work/$bad.s"
done
# Other architectures may deliberately forward original source inline asm
# to their GNU assembler; only AArch64 target admission is certified here.
if command -v aarch64-linux-gnu-as >/dev/null 2>&1; then
    aarch64-linux-gnu-as "$work/minic.s" -o "$work/owner.o"
    echo 'AARCH_QCOM_SSBS=PASS qcom_bl=16 pstate=2 negatives=3 GNU_AS=PASS'
else
    echo 'AARCH_QCOM_SSBS=PASS qcom_bl=16 pstate=2 negatives=3 GNU_AS=SKIP(toolchain-unavailable)'
fi
