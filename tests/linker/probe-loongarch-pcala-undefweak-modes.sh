#!/usr/bin/env bash
# GNU differential: ordinary static ET_EXEC vs scripted kernel static PIE.
set -Eeuo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
MINIAS=${MINIAS:-"$repo/build/release/bin/minic-as"}
MINILD=${MINILD:-"$repo/build/release/bin/minic-ld"}
GNU_LD=${GNU_LD:-loongarch64-linux-gnu-ld.bfd}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/weak.o" \
    "$repo/tests/linker/fixtures/loongarch-pcala-undefweak-modes.S"
readelf -Wr "$work/weak.o" >"$work/weak.reloc"
[[ $(grep -c R_LARCH_PCALA_HI20 "$work/weak.reloc") == 2 ]]
[[ $(grep -c R_LARCH_PCALA_LO12 "$work/weak.reloc") == 2 ]]
readelf -Ws "$work/weak.o" >"$work/weak.symbols"
grep -Eq 'NOTYPE[[:space:]]+WEAK[[:space:]]+DEFAULT[[:space:]]+UND[[:space:]]+weak_optional_data' "$work/weak.symbols"
grep -Eq 'FUNC[[:space:]]+WEAK[[:space:]]+DEFAULT[[:space:]]+UND[[:space:]]+weak_optional_function' "$work/weak.symbols"

"$MINILD" -m elf64loongarch -static -e _start \
    -o "$work/static.mini.elf" "$work/weak.o"
kernel_flags=(-m elf64loongarch -static -n -nostdlib -static -pie
              --no-dynamic-linker -z notext --build-id=sha1 -X
              --orphan-handling=warn
              "--script=$repo/tests/linker/fixtures/loongarch-pcala-undefweak-modes.lds")
"$MINILD" "${kernel_flags[@]}" -e _start \
    -o "$work/kernel-pie.mini.elf" "$work/weak.o"

python3 - "$work/static.mini.elf" "$work/kernel-pie.mini.elf" <<'PY'
from pathlib import Path
import struct
import sys

def text(path):
    data=Path(path).read_bytes()
    assert data[:6] == b'\x7fELF\x02\x01'
    shoff=struct.unpack_from('<Q',data,40)[0]
    shnum=struct.unpack_from('<H',data,60)[0]
    shstr=struct.unpack_from('<H',data,62)[0]
    sections=[struct.unpack_from('<IIQQQQIIQQ',data,shoff+i*64)
              for i in range(shnum)]
    names=sections[shstr]
    strs=data[names[4]:names[4]+names[5]]
    for s in sections:
        name=strs[s[0]:strs.index(0,s[0])].decode()
        if name == '.text':
            return s[3],data[s[4]:s[4]+s[5]]
    raise AssertionError('missing .text')

_,stat=text(sys.argv[1]);pie_addr,pie=text(sys.argv[2])
assert pie_addr == 0x9000000000220000,hex(pie_addr)
for i,rd in ((0,12),(8,13)):
    static_hi,static_lo=struct.unpack_from('<II',stat,i)
    pie_hi,pie_lo=struct.unpack_from('<II',pie,i)
    assert static_hi == 0x14000000|rd,(i,hex(static_hi))
    assert static_lo == 0x02c00000|(rd<<5)|rd,(i,hex(static_lo))
    delta = (-((pie_addr+i)&~0xfff)) & ((1<<64)-1)
    expected = 0x1a000000 | (((delta>>12)&0xfffff)<<5) | rd
    assert pie_hi == expected,(i,hex(pie_hi),hex(expected))
    assert pie_lo == 0x02c00000|(rd<<5)|rd,(i,hex(pie_lo))
print('MINILD_LOONG_WEAK_PCALA=PASS ET_EXEC_absolute_zero=true '
      'kernel_staticPIE_pcrel32=true NOTYPE_and_FUNC=true')
PY

if ! command -v "$GNU_LD" >/dev/null 2>&1; then
    if [[ ${MINILD_REQUIRE_GNU:-0} == 1 ]]; then
        echo "MINILD_LOONG_WEAK_PCALA=FAIL missing_GNU_oracle=$GNU_LD" >&2
        exit 1
    fi
    echo 'MINILD_LOONG_WEAK_PCALA=SKIP GNU_oracle_missing'
    exit 0
fi
"$GNU_LD" -m elf64loongarch -static -e _start \
    -o "$work/static.gnu.elf" "$work/weak.o"
"$GNU_LD" "${kernel_flags[@]}" -e _start \
    -o "$work/kernel-pie.gnu.elf" "$work/weak.o"
python3 - "$work/static.mini.elf" "$work/static.gnu.elf" \
          "$work/kernel-pie.mini.elf" "$work/kernel-pie.gnu.elf" <<'PY'
from pathlib import Path
import struct
import sys

def text(path):
    data=Path(path).read_bytes()
    shoff=struct.unpack_from('<Q',data,40)[0]
    shnum=struct.unpack_from('<H',data,60)[0]
    shstr=struct.unpack_from('<H',data,62)[0]
    ss=[struct.unpack_from('<IIQQQQIIQQ',data,shoff+i*64)
        for i in range(shnum)]
    s=ss[shstr];names=data[s[4]:s[4]+s[5]]
    return next(data[t[4]:t[4]+t[5]] for t in ss
                if names[t[0]:names.index(0,t[0])]==b'.text')

for label,mini,gnu in (("ET_EXEC",sys.argv[1],sys.argv[2]),
                       ("kernel_staticPIE",sys.argv[3],sys.argv[4])):
    a,b=text(mini),text(gnu)
    assert a == b,(label,len(a),len(b),a[:16].hex(),b[:16].hex())
    print(f'MINILD_LOONG_WEAK_PCALA=PASS GNU_{label}_text_byte_exact=true')
PY
