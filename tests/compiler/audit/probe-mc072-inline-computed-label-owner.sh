#!/bin/sh
# Intentionally negative *audit* probe; not a check-fast acceptance test.
# When MC-072 is fixed, update this script to expect resolved label owners.
set -eu
: "${MINIC_CC:?set MINIC_CC to the compiler under test}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
source="$root/tests/compiler/audit/mc072_inline_computed_label_owner.c"

cc -std=gnu11 -O0 -c -x c "$source" -o "$work/gnu-O0.o"
cc -std=gnu11 -O2 -c -x c "$source" -o "$work/gnu-O2.o"
cc -std=gnu11 -E -P -x c "$source" -o "$work/input.i"

for target in riscv32 riscv64 aarch64 loongarch64; do
    "$MINIC_CC" --target="$target-linux" -S "$work/input.i" \
        -o "$work/$target.s"
    # The reproducible linker failure has two relocation *references* but no
    # corresponding label definitions. Merely counting emitted clone symbols
    # would incorrectly declare this C corpus accepted.
    refs=$(grep -Ec '^[[:space:]]*\.(8byte|dword|4byte)[[:space:]]+\.Luser_[0-9]+' \
        "$work/$target.s")
    if [ "$refs" -ne 2 ] || grep -Eq '^\.Luser_[0-9]+:' "$work/$target.s" ||
       ! grep -Fq '__minic_inline_spec_' "$work/$target.s"; then
        echo "MC072_AUDIT=FAIL unexpected target=$target refs=$refs" >&2
        exit 1
    fi
done
echo 'MC072_PREEXISTING_NEGATIVE=CONFIRMED GCC_O0_O2_PASS MiniC_undefined_source_labels=4_targets inline_clones=2 target_GNU_AS_QEMU=NOT_RUN'
