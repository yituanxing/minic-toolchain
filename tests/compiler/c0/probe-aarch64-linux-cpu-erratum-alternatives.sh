#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
src="$root/tests/compiler/c0/aarch64_linux_cpu_erratum_alternatives.c"

"$host" -E -P -std=gnu11 "$src" -o "$work/errata.i"
MINIC_BACKEND_CENSUS=1 "$mini" --target=aarch64-linux -S \
    "$work/errata.i" -o "$work/errata.s" \
    2>"$work/errata.log"
grep -Fq 'MINIC_BACKEND_CENSUS arch=aarch64 scanned=2 rejected=0' \
    "$work/errata.log"

# Linux's patcher consumes two 12-byte .altinstructions records, each
# containing a pair of PREL32 offsets, a CPU capability and two byte lengths.
test "$(grep -cF '.pushsection .altinstructions,"a"' "$work/errata.s")" -eq 2
test "$(grep -cF '.word 661b - .' "$work/errata.s")" -eq 2
test "$(grep -cF '.word 663f - .' "$work/errata.s")" -eq 2
test "$(grep -cF '.byte 662b - 661b' "$work/errata.s")" -eq 2
test "$(grep -cF '.byte 664f - 663f' "$work/errata.s")" -eq 2
test "$(grep -cF '.org . -' "$work/errata.s")" -eq 4
test "$(grep -cF '.hword 86' "$work/errata.s")" -eq 1
test "$(grep -cF '.hword 42' "$work/errata.s")" -eq 1
test "$(grep -c '^  nop$' "$work/errata.s")" -eq 4
test "$(grep -c '^  dsb nsh$' "$work/errata.s")" -eq 2
test "$(grep -c '^  isb$' "$work/errata.s")" -eq 2
grep -Fxq '  ic iallu' "$work/errata.s"
grep -Fxq '  .inst 0xd50330ff' "$work/errata.s"

# The target admits these two complete original GNU templates, but no
# nearby feature number, changed ISA, omitted metadata, or hidden instruction.
sed 's/"86"/"87"/' "$src" >"$work/cap86.c"
sed 's/"42"/"43"/' "$src" >"$work/cap42.c"
sed 's/nop; nop; nop/nop; nop; isb/' "$src" >"$work/old86.c"
sed 's/ic iallu; dsb nsh; isb/ic iallu; dsb ish; isb/' "$src" \
    >"$work/new86.c"
sed 's/0xd5000000/0xd5000001/' "$src" >"$work/new42.c"
sed 's/663f - ./663f - 1/g' "$src" >"$work/prel32.c"
sed 's/\.org/\.Xorg/g' "$src" >"$work/org.c"
sed 's/\.byte 664f-663f/\.byte 662b-661b/g' "$src" >"$work/length.c"
for variant in cap86 cap42 old86 new86 new42 prel32 org length; do
    cmp -s "$src" "$work/$variant.c" && {
        echo "AARCH_ERRATA=FAIL mutation_absent=$variant" >&2; exit 1;
    }
    "$host" -E -P -std=gnu11 "$work/$variant.c" \
        -o "$work/$variant.i"
    if MINIC_BACKEND_CENSUS=1 "$mini" --target=aarch64-linux -S \
        "$work/$variant.i" -o "$work/$variant.s" \
        >/dev/null 2>&1; then
        echo "AARCH_ERRATA=FAIL accepted=$variant" >&2; exit 1
    fi
    test ! -e "$work/$variant.s" || {
        echo "AARCH_ERRATA=FAIL partial_output=$variant" >&2; exit 1;
    }
done

for target in x86_64 loongarch64; do
    if "$mini" --target="$target-linux" -S "$work/errata.i" \
        -o "$work/foreign.s" >/dev/null 2>&1; then
        echo "AARCH_ERRATA=FAIL foreign=$target" >&2; exit 1
    fi
done

if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1 &&
   command -v aarch64-linux-gnu-objdump >/dev/null 2>&1; then
    aarch64-linux-gnu-gcc -c -x assembler "$work/errata.s" -o "$work/mini.o"
    aarch64-linux-gnu-gcc -c "$src" -o "$work/gnu.o"
    for artifact in mini gnu; do
        size=$(aarch64-linux-gnu-objdump -h "$work/$artifact.o" |
            awk '$2 == ".altinstructions" { print $3 }')
        test "$size" = 00000018
    done
    echo 'AARCH_ERRATA=PASS caps=86,42 records=2 length=12,8 negatives=8 foreign=2 GNU_GAS=PASS'
else
    echo 'AARCH_ERRATA=PASS caps=86,42 records=2 length=12,8 negatives=8 foreign=2 GNU_GAS=SKIP'
fi
