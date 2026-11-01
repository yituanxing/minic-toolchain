#!/usr/bin/env bash
# GNU exact bytes, long macro header and long outer/inline IRP argument lists.
set -Eeuo pipefail
: "$MINIAS"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
python3 - "$work/large.s" <<'PY'
from pathlib import Path
import sys
def seq(n, value):
    return ",".join([str(value)] * n)
s=(".text\n"
   ".macro unused p="+"9"*1300+"\n"
   " .byte 0x7a\n.endm\n"
   ".irp v,"+seq(130,1)+"\n"
   " .byte \\v\n.endr\n"
   ".irp v,"+seq(120,2)+"; .byte \\v; .endr\n")
Path(sys.argv[1]).write_text(s,encoding="ascii")
PY
as --64 -o "$work/gnu.o" "$work/large.s"
"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/large.s"
for kind in gnu mini; do
    objcopy -j .text -O binary "$work/$kind.o" "$work/$kind.bin"
done
cmp "$work/gnu.bin" "$work/mini.bin"
test "$(wc -c <"$work/mini.bin")" -eq 250
python3 - "$work/mini.bin" <<'PY'
from pathlib import Path
import sys
assert Path(sys.argv[1]).read_bytes()==bytes([1]*130+[2]*120)
PY
echo 'MINIAS_LARGE_COLLECTOR=PASS GNU_bytes=exact macro_header=1300 irp=130 inline_irp=120'
