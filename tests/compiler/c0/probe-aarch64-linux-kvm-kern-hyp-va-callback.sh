#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
"${HOST_CC:-cc}" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/aarch64_linux_kvm_kern_hyp_va_callback.c" \
    -o "$work/mask.i"
"$minic" --target=aarch64-linux -S "$work/mask.i" -o "$work/mask.s"
test "$(grep -c '^  \.word kvm_update_va_mask - \.$' "$work/mask.s")" -eq 1
test "$(grep -c '^  \.hword (1 << 15) | (1)$' "$work/mask.s")" -eq 1
test "$(grep -c '^  \.word 661b - \.$' "$work/mask.s")" -eq 1
test "$(grep -c '^  \.org ' "$work/mask.s")" -eq 0
for opcode in 'and x9, x9, #1' 'ror x9, x9, #1' \
    'add x9, x9, #0' 'add x9, x9, #0, lsl #12' 'ror x9, x9, #63'; do
    grep -Fq "$opcode" "$work/mask.s"
done
# RV32/RV64 still have an existing generic GNU-ASM passthrough path;
# C-only -S on those targets does not imply acceptance by their assembler.
for target in x86_64 loongarch64; do
    if "$minic" --target="$target-linux" -S "$work/mask.i" \
        -o "$work/$target.s" >/dev/null 2>&1; then
        echo "FAIL accepted AArch KVM VA mask callback on $target" >&2
        exit 1
    fi
done
sed 's/kvm_update_va_mask/kvm_update_va_pretend/' "$work/mask.i" > "$work/wrong-callback.i"
sed 's/(1 << 15)/(1 << 14)/' "$work/mask.i" > "$work/wrong-cap.i"
sed 's/"+r"(value)/"=r"(value)/' "$work/mask.i" > "$work/wrong-constraint.i"
sed 's/ror %0, %0, #63/ror %0, %0, #62/' "$work/mask.i" > "$work/wrong-rotation.i"
sed 's/" .byte 664f-663f/" .byte 663f-663f/' "$work/mask.i" > "$work/wrong-length.i"
for category in callback cap constraint rotation length; do
    if "$minic" --target=aarch64-linux -S "$work/wrong-$category.i" \
        -o "$work/wrong-$category.s" >/dev/null 2>&1; then
        echo "FAIL accepted invalid KVM VA mask $category" >&2
        exit 1
    fi
done
printf '%s\n' 'PASS KVM __kern_hyp_va GNU ALTERNATIVE_CB callback PREL32 + live 5 instruction patch, 2 target gates +5 negative'
