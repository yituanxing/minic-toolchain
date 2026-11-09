#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${1:-${MINIC_CC:-"$root/build/release/bin/minic-cc"}}
host=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
src="$root/tests/compiler/c0/aarch64_linux_kvm_at_fixup.c"

"$host" -E -P -std=gnu11 "$src" -o "$work/owner.i"
MINIC_BACKEND_CENSUS=1 "$mini" --target=aarch64-linux -S \
    "$work/owner.i" -o "$work/minic.s" 2>"$work/owner.log"
grep -Fq 'MINIC_BACKEND_CENSUS arch=aarch64 scanned=1 rejected=0' \
    "$work/owner.log"
grep -Fq 'mrs x10, spsr_el2' "$work/minic.s"
grep -Fq 'mrs x11, elr_el2' "$work/minic.s"
grep -Fq 'at s1e1r, x12' "$work/minic.s"
grep -Fq 'msr spsr_el2, x10' "$work/minic.s"
grep -Fq 'msr elr_el2, x11' "$work/minic.s"
grep -Fq 'mov w9, #-14' "$work/minic.s"
grep -Fq '.pushsection __kvm_ex_table, "a"' "$work/minic.s"
grep -Fq '.long (1b - .), (2b - .)' "$work/minic.s"

# A changed exception handler, an invalid PREL32 entry, or an unexpected
# immediate must fail at target admission; do not silently emit GNU text.
sed 's/msr elr_el2/msr far_el2/' "$src" >"$work/sysreg.c"
sed 's/\.long (1b - \.), (2b - \.)/.long (1b - .), (3b - .)/' \
    "$src" >"$work/prel32.c"
sed 's/"i"(-14)/"i"(-13)/' "$src" >"$work/immediate.c"
sed 's/s1e1r/s1e1w/' "$src" >"$work/operation.c"
sed 's/"+r"(err)/"=r"(err)/' "$src" >"$work/constraint.c"
for bad in sysreg prel32 immediate operation constraint; do
    "$host" -E -P -std=gnu11 "$work/$bad.c" -o "$work/$bad.i"
    if "$mini" --target=aarch64-linux -S "$work/$bad.i" \
        -o "$work/$bad.s" >"$work/$bad.out" 2>"$work/$bad.err"; then
        echo "AARCH_KVM_AT_FIXUP=FAIL accepted=$bad" >&2
        exit 1
    fi
    test ! -s "$work/$bad.s"
done

if command -v aarch64-linux-gnu-as >/dev/null 2>&1; then
    aarch64-linux-gnu-as "$work/minic.s" -o "$work/owner.o"
    echo 'AARCH_KVM_AT_FIXUP=PASS reloc_pair=2 negatives=5 GNU_AS=PASS'
else
    echo 'AARCH_KVM_AT_FIXUP=PASS reloc_pair=2 negatives=5 GNU_AS=SKIP(toolchain-unavailable)'
fi
