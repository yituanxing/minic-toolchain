#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
"${HOST_CC:-cc}" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/aarch64_linux_kvm_tlbi_range.c" \
    -o "$work/tlbi.i"
"$minic" --target=aarch64-linux -S "$work/tlbi.i" -o "$work/tlbi.s"
for mnemonic in ripas2e1is ripas2e1 vmalls12e1; do
    grep -Fq "tlbi $mnemonic" "$work/tlbi.s"
done
printf '%s\n' \
    'void unknown(unsigned long v) { asm volatile(".arch armv8.5-a\n\ttlbi ripas2e2, %x0" :: "rZ"(v)); }' \
    > "$work/unknown.i"
if "$minic" --target=aarch64-linux -S "$work/unknown.i" -o "$work/unknown.s" >/dev/null 2>&1; then
    echo 'FAIL accepted unrecognized TLBI opcode' >&2
    exit 1
fi
printf '%s\n' \
    'void bad(unsigned long v) { asm volatile(".arch armv8.5-a\n\ttlbi ripas2e1is, %x0\n\tnop" :: "rZ"(v)); }' \
    > "$work/bad.i"
if "$minic" --target=aarch64-linux -S "$work/bad.i" -o "$work/bad.s" >/dev/null 2>&1; then
    echo 'FAIL accepted appended unqualified hardware opcode' >&2
    exit 1
fi
printf '%s\n' 'PASS AArch64 KVM range TLBI hardware=3 forbidden=2'
