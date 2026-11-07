#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
src="$root/tests/compiler/c0/aarch64_linux_efi_implicit_cache.c"

"$host" -E -P -std=gnu11 "$src" -o "$work/efi.i"
"$mini" --target=aarch64-linux -S "$work/efi.i" -o "$work/efi.s"
test "$(grep -c '^  dc civac, x9$' "$work/efi.s")" -eq 2
test "$(grep -c '^  dc cvau, x9$' "$work/efi.s")" -eq 1
grep -Fq '  ic ialluis' "$work/efi.s"
grep -Fq '  dsb ish' "$work/efi.s"
grep -Fq '  isb' "$work/efi.s"

# Alternate DC/IC cache opcodes retain their original stricter contract;
# only EFI DCTYPE civac/cvau is admitted without an explicit memory clobber.
sed 's/"civac"/"cvac"/' "$src" >"$work/wrong-dctype.c"
sed 's/"cvau"/"ivac"/' "$src" >"$work/wrong-cvau.c"
sed 's/"dc " "civac" ", %0"/"dc " "civac" ", %1"/' \
    "$src" >"$work/wrong-operand.c"
sed 's/"dc " "civac" ", %0"/"dc " "civac" ", %0 ; nop"/' \
    "$src" >"$work/extra-instruction.c"
sed 's/"r"(base)/"m"(base)/' "$src" >"$work/memory-input.c"
sed 's/"r"(base));/"r"(base) : "cc");/' \
    "$src" >"$work/cc-clobber.c"
sed 's/"r"(base));/"r"(base), "r"(base));/' \
    "$src" >"$work/extra-input.c"

for variant in wrong-dctype wrong-cvau wrong-operand extra-instruction \
               memory-input cc-clobber extra-input; do
    cmp -s "$src" "$work/$variant.c" && {
        echo "EFI_CACHE=FAIL mutation_absent=$variant" >&2; exit 1;
    }
    "$host" -E -P -std=gnu11 "$work/$variant.c" -o "$work/$variant.i"
    if "$mini" --target=aarch64-linux -S "$work/$variant.i" \
        -o "$work/$variant.s" >/dev/null 2>&1; then
        echo "EFI_CACHE=FAIL accepted=$variant" >&2; exit 1
    fi
    if test -e "$work/$variant.s"; then
        echo "EFI_CACHE=FAIL partial_output=$variant" >&2; exit 1
    fi
done

for target in x86_64 loongarch64; do
    if "$mini" --target="$target-linux" -S "$work/efi.i" \
        -o "$work/foreign.s" >/dev/null 2>&1; then
        echo "EFI_CACHE=FAIL foreign=$target" >&2; exit 1
    fi
done

if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    aarch64-linux-gnu-gcc -c -x assembler "$work/efi.s" -o "$work/efi.o"
    echo 'EFI_CACHE=PASS civac=2 cvau=1 foreign=2 negative=7 GNU_GAS=PASS'
else
    echo 'EFI_CACHE=PASS civac=2 cvau=1 foreign=2 negative=7 GNU_GAS=SKIP'
fi
