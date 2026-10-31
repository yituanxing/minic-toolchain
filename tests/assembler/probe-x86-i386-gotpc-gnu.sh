#!/usr/bin/env bash
# GNU i386 GAS treats _GLOBAL_OFFSET_TABLE_ as GOTPC in all machine fields,
# not just the GCC-conventional `addl $_GLOBAL_OFFSET_TABLE_,%ebx` idiom.
set -Eeuo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
minias=${MINIAS:-"$root/build/release/bin/minic-as"}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

cat >"$work/gotpc.s" <<'ASM'
.text
.code32
.globl gotpc_fields
gotpc_fields:
    movl $_GLOBAL_OFFSET_TABLE_, %eax
    addl $_GLOBAL_OFFSET_TABLE_, %eax
    subl $_GLOBAL_OFFSET_TABLE_, %eax
    orl $_GLOBAL_OFFSET_TABLE_, %eax
    leal _GLOBAL_OFFSET_TABLE_(%ebx), %ecx
    ret
.section .note.GNU-stack,"",@progbits
ASM
as --32 -o "$work/gnu.o" "$work/gotpc.s"
"$minias" --target=x86_64 -march=i386 -mabi=i386 \
  -o "$work/mini.o" "$work/gotpc.s"
objcopy -O binary --only-section=.text "$work/gnu.o" "$work/gnu.text"
objcopy -O binary --only-section=.text "$work/mini.o" "$work/mini.text"
cmp "$work/gnu.text" "$work/mini.text"
readelf -rW "$work/gnu.o" >"$work/gnu.rel"
readelf -rW "$work/mini.o" >"$work/mini.rel"
python3 - "$work/gnu.rel" "$work/mini.rel" <<'PY'
from pathlib import Path
import sys
def gotpc(path):
    rows=[]
    for line in Path(path).read_text().splitlines():
        fields=line.split()
        if len(fields)>=5 and fields[2].startswith('R_'):
            rows.append((int(fields[0],16),fields[2],fields[-1]))
    return rows
gnu,mini=map(gotpc,sys.argv[1:])
assert gnu==mini, (gnu,mini)
assert len(gnu)==5 and all(x[1]=='R_386_GOTPC' for x in gnu), gnu
print('MINIAS_X86_I386_GOTPC_RELOCS=PASS fields=5 exact=GNU')
PY
ld -m elf_i386 -shared -o "$work/mini.so" "$work/mini.o"
readelf -h "$work/mini.so" | grep -Eq 'Type:.*DYN'
echo 'MINIAS_X86_I386_GOTPC=PASS GNU_bytes=exact GNU_relocations=exact GNU_shared_link=PASS'
