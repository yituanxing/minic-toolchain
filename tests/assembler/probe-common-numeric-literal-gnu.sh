#!/usr/bin/env bash
# Strict numeric directive fields share GNU's binary/C-suffix literal lexer.
set -Eeuo pipefail
: "${MINIAS:?MINIAS binary required}"
for cmd in python3 as readelf objcopy cmp; do command -v "$cmd" >/dev/null; done
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

python3 - "$work" <<'PY'
from pathlib import Path
import sys
root=Path(sys.argv[1])
(root/'positive.s').write_text('''\
.text
.globl numtest
numtest:
  .byte 0x41
  .size numtest,0b10
.subsection 0b10
  .byte 0x5a
.subsection 0
.section .minias_num_merge,"aM",@progbits,0b10
  .short 0x4142
.section .minias_num_unique,"a",@progbits,unique,0b11
  .byte 0x43
''',encoding='ascii')
invalid={
    'subsection':'.text\n.subsection 0b2\n.byte 0x41\n',
    'size':'.text\nnumtest:\n.byte 0x41\n.size numtest,0b2\n',
    'section':'.section .foo,"aM",@progbits,0b2\n.byte 0x41\n',
}
for name,content in invalid.items():
    (root/('bad_'+name+'.s')).write_text(content,encoding='ascii')
PY

as --64 -o "$work/gnu.o" "$work/positive.s"
"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/positive.s"
for kind in gnu mini; do
    readelf -W -S "$work/$kind.o" > "$work/$kind.sections"
    readelf -W -s "$work/$kind.o" > "$work/$kind.symbols"
    for section in .text .minias_num_merge; do
        objcopy -O binary -j "$section" "$work/$kind.o" \
            "$work/$kind${section}.bin"
    done
done
cmp "$work/gnu.sections" "$work/mini.sections"
cmp "$work/gnu.symbols" "$work/mini.symbols"
cmp "$work/gnu.text.bin" "$work/mini.text.bin"
cmp "$work/gnu.minias_num_merge.bin" "$work/mini.minias_num_merge.bin"

for name in subsection size section; do
    if as --64 -o "$work/bad_gnu.o" "$work/bad_$name.s" \
             >"$work/gnu.$name.log" 2>&1; then
        echo "GNU unexpectedly accepted invalid $name numeric literal" >&2
        exit 1
    fi
    if "$MINIAS" --target=x86_64 -o "$work/bad_mini.o" "$work/bad_$name.s" \
             >"$work/mini.$name.log" 2>&1; then
        echo "MiniAS unexpectedly accepted invalid $name numeric literal" >&2
        exit 1
    fi
done
echo 'MINIAS_COMMON_NUMERIC_LITERAL=PASS GNU_sections_symbols=exact binary=3 invalid=3'
