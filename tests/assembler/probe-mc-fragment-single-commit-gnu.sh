#!/usr/bin/env bash
# Every source MCFragment (instruction, alignment, ordinary DATA, .fill,
# .space, .org, NOBITS) must use the same staged-byte/fixup commit.
# GNU exact payload/relocation checks expose double append, stale cursor,
# discarded data and wrong .-anchored MCExpr during migration.
set -Eeuo pipefail
: "${MINIAS:?point MINIAS at the exact-head binary}"
for tool in as readelf objcopy cmp python3; do command -v "$tool" >/dev/null; done
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/valid.s" <<'SRC'
.text
source_entry:
  nop
  .p2align 4
  nop
.data
.byte 0x91, 0x92
# A single GCC/Linux-style statement owns several ordered DATA writes;
# old one-pattern-per-fragment staging failed after the first .long.
.long external_data + 7, 0x55667788
.long 0x11223344, 0x99aabbcc
.fill 20,4,0x11223344
.space 8192,0x5a
.p2align 5
local_data:
.quad local_data - .
.long external_code - . + 12
.org 8512, 0xa5
.section .bss,"aw",@nobits
.space 333, 0x7f
.p2align 6
SRC

"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/valid.s"
as --64 -o "$work/gnu.o" "$work/valid.s"
for section in .text .data; do
    objcopy -j "$section" -O binary "$work/mini.o" "$work/mini-$section.bin"
    objcopy -j "$section" -O binary "$work/gnu.o" "$work/gnu-$section.bin"
    cmp "$work/mini-$section.bin" "$work/gnu-$section.bin"
done

python3 - "$work/mini.o" "$work/gnu.o" <<'PY'
import re, struct, subprocess, sys
def section_metadata(path):
    data=open(path,'rb').read()
    assert data[:6]==b'\x7fELF\x02\x01'
    hdr=struct.unpack_from('<16sHHIQQQIHHHHHH',data)
    off,step,num,strings=hdr[6],hdr[11],hdr[12],hdr[13]
    secs=[struct.unpack_from('<IIQQQQIIQQ',data,off+i*step)
          for i in range(num)]
    names=data[secs[strings][4]:secs[strings][4]+secs[strings][5]]
    found={}
    for entry in secs:
        index=entry[0]
        end=names.find(b'\0',index)
        name=names[index:end].decode()
        if name in ('.text','.data','.bss'):
            found[name]=(entry[1],entry[2],entry[5],entry[8],entry[9])
    return found

def relocs(path):
    result=[]
    for row in subprocess.check_output(
        ['readelf','-rW',path],text=True).splitlines():
        m=re.match(r'^\s*([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+'
                   r'(R_X86_64_\w+)\s+[0-9a-fA-F]+\s+'
                   r'(\S+)\s+([+-])\s+(\S+)',row)
        if m:
            result.append((int(m[1],16),m[2],m[3],m[4],m[5]))
    return result

mini,gnu=sys.argv[1:]
m,g=section_metadata(mini),section_metadata(gnu)
if m!=g:
    raise SystemExit(f'section sizes/types/flags/align/entsize mismatch: {m} != {g}')
r,s=relocs(mini),relocs(gnu)
if r!=s or len(r)!=2:
    raise SystemExit(f'fragment relocation order/metadata mismatch: {r} != {s}')
PY

# A NOBITS literal with nonzero bytes must fail and must never emit an ELF.
cat >"$work/invalid.s" <<'SRC'
.bss
.long 1
SRC
if "$MINIAS" --target=x86_64 -o "$work/invalid.o" "$work/invalid.s" >"$work/error" 2>&1; then
    echo "MiniAS emitted nonzero NOBITS data" >&2
    exit 1
fi
test ! -e "$work/invalid.o"
grep -q 'nobits-nonzero-data' "$work/error"

# Structural guard: reject a regression to the old two-path source emission.
! grep -q 'capture_instruction_bytes' assembler/src/{assembler.c,minias_internal.h,mc_encode_result.c}
grep -q 'minias_stage_fragment_bytes' assembler/src/assembler.c
grep -q 'minias_stage_fragment_pattern' assembler/src/assembler.c
grep -q 'minias_commit_encode_result(as,stmt)' assembler/src/assembler.c

echo 'MINIAS_FRAGMENT_SINGLE_COMMIT=PASS GNU_payload=exact GNU_reloc=exact NOBITS=exact source_bridge=removed'
