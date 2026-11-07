#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
"${HOST_CC:-cc}" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/aarch64_linux_pstate_pan_tco_alternatives.c" \
    -o "$work/pstate.i"
"$minic" --target=aarch64-linux -S "$work/pstate.i" -o "$work/pstate.s"
test "$(grep -c '^  \.hword 38$' "$work/pstate.s")" -eq 2
test "$(grep -c '^  \.hword 53$' "$work/pstate.s")" -eq 3
test "$(grep -c '^  \.org \.' "$work/pstate.s")" -eq 10
test "$(grep -c '^  \.if 0 == 1$' "$work/pstate.s")" -eq 1
for opcode in 0xd500409f 0xd500419f 0xd503409f 0xd503419f; do
    grep -Fq ".inst $opcode" "$work/pstate.s"
done
# RV32/RV64 currently retain their pre-existing generic zero-operand GNU
# inline-ASM passthrough path; their -S is not a machine-ISA validation.
for target in x86_64 loongarch64; do
    if "$minic" --target="$target-linux" -S "$work/pstate.i" \
        -o "$work/$target.s" >/dev/null 2>&1; then
        echo "FAIL accepted AArch PAN/TCO on $target" >&2
        exit 1
    fi
done
sed 's/"38"/"39"/' "$work/pstate.i" > "$work/wrong-cap.i"
sed 's/"53"/"52"/' "$work/pstate.i" > "$work/wrong-mte-cap.i"
sed 's/".if " "0"/".if " "2"/' "$work/pstate.i" > "$work/wrong-config.i"
sed 's/".org . - (664b-663b) + (662b-661b)/".org . - (664b-663b) + (661b-661b)/' \
    "$work/pstate.i" > "$work/wrong-length.i"
sed 's/"(0xd500401f/"(0xd500401e/' "$work/pstate.i" > "$work/wrong-opcode.i"
for category in cap mte-cap config length opcode; do
    if "$minic" --target=aarch64-linux -S "$work/wrong-$category.i" \
        -o "$work/wrong-$category.s" >/dev/null 2>&1; then
        echo "FAIL accepted modified PSTATE alternative $category" >&2
        exit 1
    fi
done
printf '%s\n' 'PASS Linux PAN cap38 / MTE TCO cap53 ALT GNU original .inst+PREL32 cfg enabled/disabled, 2 target gates +5 negative'
