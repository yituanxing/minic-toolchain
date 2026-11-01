#!/usr/bin/env bash
# GNU physical SourceGraph input: no arbitrary 64 KiB line cap, and
# long comments must not corrupt subsequent branch relaxation or bytes.
set -Eeuo pipefail
: "$MINIAS"
for tool in python3 as objcopy cmp; do command -v "$tool" >/dev/null; done
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
python3 - "$work/long.s" <<'PY'
from pathlib import Path
import sys
p = Path(sys.argv[1])
p.write_text(
    '.text\n.globl long_physical_source\nlong_physical_source:\n'
    + '/*' + 'x' * 70000 + '*/\n'
    + 'je 1f\n.rept 200\n nop\n.endr\n1:\n.byte 0x42\n',
    encoding='ascii'
)
PY
as --64 -o "$work/gnu.o" "$work/long.s"
MINIAS_TRACE_RELAYOUT=1 "$MINIAS" --target=x86_64 \
    -o "$work/mini.o" "$work/long.s" 2>"$work/mini.trace"
for flavor in mini gnu; do
    objcopy -j .text -O binary "$work/$flavor.o" "$work/$flavor.bin"
done
cmp "$work/gnu.bin" "$work/mini.bin"
test "$(wc -c <"$work/mini.bin")" -eq 207
grep -Eq '^minias-source-graph:file-reads=1 .*replay-passes=[0-9]+' \
    "$work/mini.trace"
echo 'MINIAS_LONG_PHYSICAL_SOURCE=PASS long_comment=70004 GNU_text=exact branch_growth=1 reads=1'
