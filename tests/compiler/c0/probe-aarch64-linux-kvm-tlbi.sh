#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
"${HOST_CC:-cc}" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/aarch64_linux_kvm_tlbi.c" -o "$work/input.i"
"$minic" --target=aarch64-linux -S "$work/input.i" -o "$work/tlbi.s"
test -s "$work/tlbi.s"
for op in vale1is vale2is aside1is rvale1is vae1is rvae1is \
    vaale1is vaae1is ipas2e1is ipas2e1 vae2is vmalle1 \
    vmalle1is alle1is vmalls12e1is; do
    grep -Fq "  tlbi $op" "$work/tlbi.s"
done
for target in x86_64 riscv32 riscv64 loongarch64; do
    if "$minic" --target="$target-linux" -S "$work/input.i" \
        -o "$work/$target.s" >/dev/null 2>&1; then
        printf 'FAIL ARM64 KVM TLBI accepted by %s\n' "$target" >&2
        exit 1
    fi
done
printf '%s\n' 'PASS compiler/c0/aarch64_linux_kvm_tlbi hardware_ops=15 target-gate=4'
