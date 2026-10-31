#!/usr/bin/env bash
# GCC-generated `.loc ... view .LVU*` labels are absolute DWARF view ordinals,
# not source locations or relocatable section symbols. Check the final values
# against GNU GAS in both ELF classes, including forced view resets.
set -Eeuo pipefail
: "${MINIAS:?Set MINIAS to the exact-head assembler binary}"
for tool in gcc as objcopy cmp python3; do command -v "$tool" >/dev/null; done
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

cat >"$work/views.s" <<'ASM'
.file 1 "views.c"
.text
.globl entry
.type entry,@function
entry:
.loc 1 1 0 view .LVU0
.loc 1 2 0 view .LVU1
  nop
.loc 1 3 0 view .LVU2
.loc 1 4 0 view .LVU3
  nop
.loc 1 5 0 view -0
.loc 1 6 0 view .LVU4
  ret
.size entry,.-entry
.section .debug_loc,"",@progbits
  .long .LVU0,.LVU1,.LVU2,.LVU3,.LVU4
  .uleb128 .LVU3-.LVU2
  .sleb128 .LVU4-.LVU2
ASM

for width in 32 64; do
    if [[ $width == 32 ]]; then
        target=(-march=i386 -mabi=i386)
    else
        target=()
    fi
    as --"$width" -o "$work/gnu.o" "$work/views.s"
    "$MINIAS" --target=x86_64 "${target[@]}" \
        -o "$work/mini.o" "$work/views.s"
    objcopy --dump-section .debug_loc="$work/gnu.bin" "$work/gnu.o"
    objcopy --dump-section .debug_loc="$work/mini.bin" "$work/mini.o"
    cmp "$work/gnu.bin" "$work/mini.bin"
    python3 - "$work/mini.bin" "$width" <<'PY'
import pathlib
import struct
import sys

data = pathlib.Path(sys.argv[1]).read_bytes()
assert len(data) == 22, len(data)
view_numbers = struct.unpack('<5I', data[:20])
assert view_numbers == (0, 1, 0, 1, 1), view_numbers
assert data[20:] == b'\x01\x01', data[20:]
print(f'MINIAS_DWARF_NAMED_VIEWS=PASS ELF{sys.argv[2]} views={view_numbers} GNU_bytes=exact LEB=exact')
PY
done

# A real optimized GCC translation unit is the stronger oracle: its DWARF5
# loclists contain dozens of symbolic .LVU* differences that can cross
# changes in instruction lengths. Compare the complete binary section, not
# just whether MiniAS happened to accept every directive.
real_c=tests/external/linux/runtime_initramfs/probe.c
gcc -O2 -g -gdwarf-5 -gvariable-location-views -S \
    -o "$work/real-gcc.s" "$real_c"
if ! grep -Eq '\.loc .*view \.LVU[0-9]+' "$work/real-gcc.s"; then
    echo 'MINIAS_DWARF_REAL_GCC=FAIL missing named GCC location views' >&2
    exit 1
fi
as -o "$work/real-gnu.o" "$work/real-gcc.s"
"$MINIAS" --target=x86_64 -o "$work/real-mini.o" "$work/real-gcc.s"
objcopy --dump-section .debug_loclists="$work/real-gnu.bin" "$work/real-gnu.o"
objcopy --dump-section .debug_loclists="$work/real-mini.bin" "$work/real-mini.o"
cmp "$work/real-gnu.bin" "$work/real-mini.bin"
echo 'MINIAS_DWARF_REAL_GCC=PASS optimized_O2=1 named_views=1 GNU_debug_loclists=exact'
