#!/usr/bin/env bash
# Exact GNU byte parity for 512 IRP values with nested REPT expansion frames.
set -Eeuo pipefail
: "$MINIAS"
for cmd in python3 as objcopy cmp; do command -v "$cmd" >/dev/null; done
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
python3 - "$work/source.s" <<'PY'
from pathlib import Path
import sys
seq=','.join('0x%02x' % (i%256) for i in range(512))
src=(
    '.text\n'
    '.byte 0x65,0x66\n'
    '.irp item,'+seq+'\n'
    ' .byte \\item\n'
    ' .rept 1\n'
    '  .byte 0xa5\n'
    ' .endr\n'
    '.endr\n'
    '.byte 0x67,0x68\n'
)
Path(sys.argv[1]).write_text(src,encoding='ascii')
PY
as --64 -o "$work/gnu.o" "$work/source.s"
"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/source.s"
for kind in mini gnu; do
    objcopy -O binary -j .text "$work/$kind.o" "$work/$kind.bin"
done
cmp "$work/gnu.bin" "$work/mini.bin"
python3 - "$work/mini.bin" <<'PY'
from pathlib import Path
import sys
content=Path(sys.argv[1]).read_bytes()
expected=b'\x65\x66'+b''.join(bytes((i%256,0xa5)) for i in range(512))+b'\x67\x68'
assert content==expected, (len(content),len(expected))
PY
echo 'MINIAS_IRP_REUSED_FRAME=PASS GNU_text=exact values=512 nested_frames=512'
