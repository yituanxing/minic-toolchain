#!/usr/bin/env bash
# Verify a bounded LoongArch transport loop without a cross assembler.
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { echo "LOONGARCH64_RECORD_COPY_SIZE=FAIL $*" >&2; exit 1; }

test -x "$minic" || fail 'MiniC compiler missing'
cc -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/loongarch64_record_copy_size.c" \
    -o "$work/input.i"
"$minic" --target=loongarch64-linux -S "$work/input.i" -o "$work/output.s" \
    || fail 'MiniC LoongArch compilation'

# A 32-byte copy formerly expanded to 128 scalar instructions at each site.
# The counter and local numeric backward branch preserve byte-read/write order.
test "$(grep -Fc '  li.d $t3, 32' "$work/output.s")" -ge 2 \
    || fail 'record copy/load counters'
grep -Fq '  addi.d $t3, $t3, -1' "$work/output.s" \
    || fail 'record copy decrement'
grep -Fq '  bnez $t3, 1b' "$work/output.s" \
    || fail 'record copy loop branch'
test "$(wc -l < "$work/output.s")" -lt 400 \
    || fail 'unexpected large source assembly'
echo 'LOONGARCH64_RECORD_COPY_SIZE=PASS record_bytes=32 bounded_byte_loop=1'
