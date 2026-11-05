#!/usr/bin/env bash
# GNU BFD reserves AArch64 global/weak ABS64 dynamic relocs before a script
# discards their allocating input section. Empty reservation slots must be
# preserved as R_NONE; local-symbol references do not reserve such slots.
set -Eeuo pipefail

: "${MINIAS:?set MINIAS to minic-as}"
: "${MINILD:?set MINILD to minic-ld}"
repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
fixtures="$repo/tests/linker/fixtures"
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-aarch-discard-dynrel.XXXXXX")
trap 'rm -r -- "$work"' EXIT

"$MINIAS" --target=aarch64 -march=armv8-a -mabi=lp64 \
    -o "$work/probe.o" "$fixtures/aarch64-discarded-dynrel-symbols.S"
readelf -Wr "$work/probe.o" >"$work/input.rela"
test "$(grep -c R_AARCH64_ABS64 "$work/input.rela")" -eq 4
"$MINILD" -m aarch64linux -shared -Bsymbolic \
    --no-apply-dynamic-relocs -z notext \
    -T "$fixtures/aarch64-discarded-dynrel.lds" \
    -o "$work/mini.elf" "$work/probe.o"

python3 - "$work/mini.elf" <<'PY'
from pathlib import Path
import struct
import sys

b=Path(sys.argv[1]).read_bytes()
assert b[:6]==b'\x7fELF\x02\x01'
shoff=struct.unpack_from('<Q',b,40)[0]
ent,n,shstr=struct.unpack_from('<HHH',b,58)
sections=[struct.unpack_from('<IIQQQQIIQQ',b,shoff+i*ent)
          for i in range(n)]
string_table=sections[shstr]
names=b[string_table[4]:string_table[4]+string_table[5]]
def name(section):
    return names[section[0]:names.index(0,section[0])].decode()
sh={name(s):s for s in sections}
assert '.rela.dyn' in sh and '.data' in sh, sh.keys()
assert '.data.discard' not in sh
rela=sh['.rela.dyn']
assert rela[1]==4 and rela[5]==3*24,(rela[1],rela[5])
records=[struct.unpack_from('<QQq',b,rela[4]+i*24) for i in range(3)]
assert [v[1]&0xffffffff for v in records]==[1027,0,0],records
assert all(rec==(0,0,0) for rec in records[1:]),records
assert records[0][0]==sh['.data'][3],records
print('MINILD_AARCH64_DISCARDED_DYNREL=PASS global_discard=NONE '
      'weak_discard=NONE local_discard=none live_global=RELATIVE')
PY

if command -v aarch64-linux-gnu-as >/dev/null 2>&1 &&
   command -v aarch64-linux-gnu-ld >/dev/null 2>&1; then
    aarch64-linux-gnu-as -o "$work/gnu.o" \
        "$fixtures/aarch64-discarded-dynrel-symbols.S"
    aarch64-linux-gnu-ld -m aarch64linux -shared -Bsymbolic \
        --no-apply-dynamic-relocs -z notext \
        -T "$fixtures/aarch64-discarded-dynrel.lds" \
        -o "$work/gnu.elf" "$work/gnu.o"
    python3 - "$work/gnu.elf" "$work/mini.elf" <<'PY_GNU'
from pathlib import Path
import struct
import sys
def relocs(path):
    b=Path(path).read_bytes()
    off=struct.unpack_from('<Q',b,40)[0]
    ent,n,shstr=struct.unpack_from('<HHH',b,58)
    sh=[struct.unpack_from('<IIQQQQIIQQ',b,off+i*ent) for i in range(n)]
    st=sh[shstr]
    names=b[st[4]:st[4]+st[5]]
    for s in sh:
        name=names[s[0]:names.index(0,s[0])].decode()
        if name=='.rela.dyn':
            return [(struct.unpack_from('<QQq',b,s[4]+i*24))
                    for i in range(s[5]//24)]
    return []
gnu=relocs(sys.argv[1])
mini=relocs(sys.argv[2])
if gnu != mini:
    def pretty(records):
        return [(hex(place), hex(info >> 32), info & 0xffffffff, addend)
                for place, info, addend in records]
    print('MINILD_AARCH64_DISCARDED_DYNREL_GNU=DIFF '
          f'gnu_count={len(gnu)} mini_count={len(mini)}', flush=True)
    print(f'GNU_Rela={pretty(gnu)}', flush=True)
    print(f'Mini_Rela={pretty(mini)}', flush=True)
assert gnu == mini, 'GNU dynrel parity'
print('MINILD_AARCH64_DISCARDED_DYNREL_GNU=PASS exact_relro_records')
PY_GNU
else
    echo 'MINILD_AARCH64_DISCARDED_DYNREL_GNU=SKIP missing_cross_binutils'
fi
