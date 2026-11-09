#!/bin/sh
# Dynamic guarded-page native oracle plus all five target -S paths.
set -eu
: "${MINIC:?Set MINIC to absolute minic-cc path}"
: "${CC:=cc}"

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$root/build/packed-bitfield-guard"}
mkdir -p "$work"
source_file="$root/tests/compiler/c0/packed_bitfield_guard_page.c"

"$CC" -std=gnu11 -O0 "$source_file" -o "$work/gnu"
"$CC" -std=gnu11 -E -P "$source_file" -o "$work/input.i"
for target in x86_64-linux riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
    "$MINIC" --target="$target" -S "$work/input.i" -o "$work/$target.s"
    test -s "$work/$target.s"
done
"$CC" -no-pie "$work/x86_64-linux.s" -o "$work/minic"
"$work/gnu"
"$work/minic"

# The Loong output must use byte accesses for a packed field at record offset
# 0 when its record has alignment 1 (even if the whole object fits 4B).
sed -n '/^read_base:/,/^\.size read_base/p' "$work/loongarch64-linux.s" |
    grep -F '  ld.bu ' >/dev/null
sed -n '/^write_base:/,/^\.size write_base/p' "$work/loongarch64-linux.s" |
    grep -F '  st.b ' >/dev/null
printf '%s\n' 'PACKED_BITFIELD_GUARD=PASS GNU_native=1 MiniC_native=1 five_ISA_S=5 Loong_byte_access=1'
