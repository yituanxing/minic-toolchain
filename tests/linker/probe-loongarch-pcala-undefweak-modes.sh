#!/usr/bin/env bash
# GNU differential: ordinary static ET_EXEC vs scripted kernel static PIE.
set -Eeuo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
MINIAS=${MINIAS:-"$repo/build/release/bin/minic-as"}
MINILD=${MINILD:-"$repo/build/release/bin/minic-ld"}
GNU_LD=${GNU_LD:-loongarch64-linux-gnu-ld.bfd}
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-loong-undefweak.XXXXXXXX")
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
for label,code in (('ET_EXEC',stat),('kernel_staticPIE',pie)):
    for i,rd in ((0,12),(8,13)):
        hi,lo=struct.unpack_from('<II',code,i)
        assert hi == (0x14000000|rd),(label,i,hex(hi))
        assert lo == (0x02c00000|(rd<<5)|rd),(label,i,hex(lo))
        signed20=((hi>>5)&0xfffff)
        if signed20 & (1<<19): signed20 -= 1<<20
        absolute_addr=(signed20<<12)|((lo>>10)&0xfff)
        assert absolute_addr == 0,(label,i,hex(absolute_addr))
print('MINILD_LOONG_WEAK_PCALA=PASS ET_EXEC_absolute_zero=true '
      'kernel_staticPIE_absolute_zero=true NOTYPE_and_FUNC=true')
PY

# PCREL20_S2 is created inside ld by PCALA shortening, not yet directly
# expressible by MiniAS `.reloc`.  Transplant an ordinary MiniAS RELA entry
# onto the pcaddi site to exercise the static fixup's undefined-weak arm,
# including its required signed-12-bit fail-closed boundary.
"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/pcaddi-raw.o" \
    "$repo/tests/linker/fixtures/loongarch-pcrel20-undefweak.S"
python3 - "$work/pcaddi-raw.o" "$work" <<'PY'
from pathlib import Path
import struct
import sys

data=Path(sys.argv[1]).read_bytes()
assert data[:6] == b'\x7fELF\x02\x01'
shoff=struct.unpack_from('<Q',data,40)[0]
shnum=struct.unpack_from('<H',data,60)[0]
shstrndx=struct.unpack_from('<H',data,62)[0]
sections=[struct.unpack_from('<IIQQQQIIQQ',data,shoff+i*64)
          for i in range(shnum)]
shstr=sections[shstrndx]
names=data[shstr[4]:shstr[4]+shstr[5]]
rela=next(section for section in sections
          if names[section[0]:names.index(0,section[0])]==b'.rela.text')
assert rela[1]==4 and rela[5]==24 and rela[9]==24,rela
off=rela[4]
original_offset,original_info,original_addend=struct.unpack_from('<QQq',data,off)
assert original_offset==4 and (original_info&0xffffffff)==1
assert original_addend==0
for case,addend in (('zero',0),('plus1',1),('minus1',-1),
                    ('max12',2047),('min12',-2048),
                    ('overflow',2048),('underflow',-2049)):
    result=bytearray(data)
    struct.pack_into('<QQq',result,off,0,
                     (original_info&0xffffffff00000000)|103,addend)
    (Path(sys.argv[2])/f'pcaddi-{case}.o').write_bytes(result)
PY
readelf -Wr "$work/pcaddi-zero.o" | grep -q 'R_LARCH_PCREL20_S2'
for mode in static kernel-pie; do
    if [[ $mode == static ]]; then
        flags=(-m elf64loongarch -static -e _start)
    else
        flags=("${kernel_flags[@]}" -e _start)
    fi
    for entry in 'zero:0' 'plus1:1' 'minus1:-1' 'max12:2047' 'min12:-2048'; do
        case=${entry%%:*}
        addend=${entry#*:}
        "$MINILD" "${flags[@]}" -o "$work/pcaddi-$mode-$case.elf" \
            "$work/pcaddi-$case.o"
        python3 - "$work/pcaddi-$mode-$case.elf" "$addend" <<'PY'
from pathlib import Path
import struct
import sys
data=Path(sys.argv[1]).read_bytes()
shoff=struct.unpack_from('<Q',data,40)[0]
shnum=struct.unpack_from('<H',data,60)[0]
shstrndx=struct.unpack_from('<H',data,62)[0]
sections=[struct.unpack_from('<IIQQQQIIQQ',data,shoff+i*64)
          for i in range(shnum)]
names_sec=sections[shstrndx]
names=data[names_sec[4]:names_sec[4]+names_sec[5]]
code=next(data[sec[4]:sec[4]+sec[5]] for sec in sections
          if names[sec[0]:names.index(0,sec[0])]==b'.text')
word=struct.unpack_from('<I',code)[0]
expected=0x0280000e|((int(sys.argv[2])&0xfff)<<10)
assert word==expected,(hex(word),hex(expected))  # addi.w $r14,$r0,S+A
PY
    done
    for case in overflow underflow; do
        if "$MINILD" "${flags[@]}" -o "$work/pcaddi-$mode-$case.elf" \
            "$work/pcaddi-$case.o" >"$work/pcaddi-$mode-$case.log" 2>&1; then
            echo "MINILD_LOONG_WEAK_PCREL20=FAIL out_of_range_accepted mode=$mode case=$case" >&2
            exit 1
        fi
    done
done
echo 'MINILD_LOONG_WEAK_PCREL20=PASS modes=ET_EXEC,staticPIE signed12=zero,+1,-1,+2047,-2048 overflow=+2048,-2049_rejected'

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
"$GNU_LD" --version | head -n 1
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
    t=next(t for t in ss
           if names[t[0]:names.index(0,t[0])]==b'.text')
    return t[3],data[t[4]:t[4]+t[5]]

def old_bfd_pcala_to_zero(code,base):
    for i,rd in ((0,12),(8,13)):
        hi,lo=struct.unpack_from('<II',code,i)
        page=(base+i)&~0xfff
        old_pc_relative=0x1a000000|((((-page)>>12)&0xfffff)<<5)|rd
        if hi != old_pc_relative: return False
        if lo != (0x02c00000|(rd<<5)|rd): return False
    return True

for label,mini,gnu in (("ET_EXEC",sys.argv[1],sys.argv[2]),
                       ("kernel_staticPIE",sys.argv[3],sys.argv[4])):
    mini_vma,a=text(mini)
    gnu_vma,b=text(gnu)
    assert mini_vma==gnu_vma,(label,'section_vma',hex(mini_vma),hex(gnu_vma))
    if a == b:
        print(f'MINILD_LOONG_WEAK_PCALA=PASS GNU_{label}_text_byte_exact=true patched_BFD=true')
        continue
    # Before GNU BFD's 2024-12 undefined-weak fix, pcalau12i was left
    # PC-relative for symbols requiring absolute zero.  Accept *only* this
    # documented opcode difference, with identical remaining machine code.
    expected=bytearray(b)
    for i,_ in ((0,12),(8,13)):
        expected[i:i+4]=a[i:i+4]
    assert bytes(expected)==a and old_bfd_pcala_to_zero(b,gnu_vma),(
        label,len(a),len(b),a[:16].hex(),b[:16].hex())
    print(f'MINILD_LOONG_WEAK_PCALA=PASS GNU_{label}_LEGACY_pre_2024_12_pcala_bug=true '
          'other_bytes_exact=true')
PY
