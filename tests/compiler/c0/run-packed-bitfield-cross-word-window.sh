#!/bin/sh
# Packed 32-bit field at bit offset 1 needs five bytes/64-bit Core transport.
set -eu
: "${MINIC:?Set MINIC to the absolute path of minic-cc}"
: "${CC:=cc}"

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$root/build/packed-bitfield-cross-word"}
mkdir -p "$work"
source_file="$root/tests/compiler/c0/packed_bitfield_cross_word_window.c"

"$CC" -std=gnu11 -O0 "$source_file" -o "$work/gnu"
"$CC" -std=gnu11 -E -P "$source_file" -o "$work/input.i"
for target in x86_64-linux riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
    "$MINIC" --target="$target" -S "$work/input.i" -o "$work/$target.s"
    test -s "$work/$target.s"
done
"$CC" -no-pie "$work/x86_64-linux.s" -o "$work/minic"
"$work/gnu"
"$work/minic"

# Existing 64-bit Core bit containers cannot transport a 65-bit span. Until
# the high/low split model lands, GCC accepts the type but MiniC must reject
# the function, never silently truncate or emit a nine-byte wide access.
limit="$root/tests/compiler/c0/packed_bitfield_cross_word_64_limit.c"
"$CC" -std=gnu11 -fsyntax-only "$limit"
"$CC" -std=gnu11 -E -P "$limit" -o "$work/limit.i"
if "$MINIC" --target=x86_64-linux -S "$work/limit.i" -o "$work/limit.s" \
    >"$work/limit.stdout" 2>"$work/limit.stderr"; then
    printf '%s\n' 'unsupported 65-bit packed span was silently admitted' >&2
    exit 1
fi
grep -F "Core IR does not yet support function 'read_nine'" "$work/limit.stderr" >/dev/null
printf '%s\n' 'PACKED_CROSS_WORD=PASS GNU_native=1 MiniC_native=1 five_ISA_S=5 65_bit_limit=fail_closed'
