#!/usr/bin/env bash
# RISC-V SET_ULEB128/SUB_ULEB128 must operate as one fixed-width difference.
# Inspect actual ET_EXEC output, compare with GNU BFD where available, and
# keep malformed/unpaired/overflow cases fail-closed in both RV32 and RV64.
set -Eeuo pipefail
repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
minias=${MINIAS:-"$repo/build/release/bin/minic-as"}
minild=${MINILD:-"$repo/build/release/bin/minic-ld"}
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-riscv-uleb.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT
for tool in "$minias" "$minild" python3 readelf; do
    command -v "$tool" >/dev/null
done

for width in 32 64; do
    if [[ $width == 32 ]]; then
        isa=rv32i abi=ilp32 emulation=elf32lriscv
    else
        isa=rv64i abi=lp64 emulation=elf64lriscv
    fi
    dir="$work/$width"
    mkdir -p "$dir"
    "$minias" --target=riscv "-march=$isa" "-mabi=$abi" \
        -o "$dir/original.o" "$repo/tests/linker/fixtures/riscv-uleb-static.S"
    # MiniAS emits real local-symbol R_RISCV_32 placeholders; change only
    # ELF r_offset/r_type to the GNU SET_ULEB128/SUB_ULEB128 pair.  The
    # original placeholder bytes remain to prove the reserved span is fixed.
    python3 - "$dir/original.o" "$dir/pair.o" <<'PY'
from pathlib import Path
import struct,sys
src=Path(sys.argv[1]); dst=Path(sys.argv[2]); b=bytearray(src.read_bytes())
assert b[:4]==b'\x7fELF' and b[5]==1
is64=b[4]==2
assert b[4] in (1,2)
if is64:
    shoff=struct.unpack_from('<Q',b,40)[0]
    shentsize,shnum,shstrndx=struct.unpack_from('<HHH',b,58)
    fmt='<IIQQQQIIQQ'; rela_fmt='<QQq'; rela_width=24
else:
    shoff=struct.unpack_from('<I',b,32)[0]
    shentsize,shnum,shstrndx=struct.unpack_from('<HHH',b,46)
    fmt='<IIIIIIIIII'; rela_fmt='<IIi'; rela_width=12
sections=[struct.unpack_from(fmt,b,shoff+i*shentsize) for i in range(shnum)]
names=sections[shstrndx]; strings=b[names[4]:names[4]+names[5]]
def name(s):
    start=s[0]; return strings[start:strings.index(0,start)].decode()
reloc=next(s for s in sections if name(s)=='.rela.debug_info')
assert reloc[5]==2*rela_width,(reloc[5],rela_width)
types=[]
for i,new_type in enumerate((60,61)):
    at=reloc[4]+i*rela_width
    old_off,old_info,addend=struct.unpack_from(rela_fmt,b,at)
    assert old_off==(2+4*i) and addend==0,(old_off,addend)
    assert (old_info & (0xffffffff if is64 else 0xff))==1
    sym=old_info>>(32 if is64 else 8)
    info=(sym<<32|new_type) if is64 else (sym<<8|new_type)
    struct.pack_into(rela_fmt,b,at,0,info,0)
    types.append(new_type)
assert types==[60,61]
dst.write_bytes(b)
PY
    readelf -Wr "$dir/pair.o" >"$dir/relocs.txt"
    grep -Fq 'R_RISCV_SET_ULEB128' "$dir/relocs.txt"
    grep -Fq 'R_RISCV_SUB_ULEB128' "$dir/relocs.txt"
    "$minild" -m "$emulation" -static -e _start \
        -o "$dir/mini.elf" "$dir/pair.o"
    if [[ $width == 64 ]]; then
        "$minild" -m "$emulation" -shared \
            -o "$dir/shared.so" "$dir/pair.o"
    fi
    outputs=("$dir/mini.elf")
    if [[ $width == 64 ]]; then
        outputs+=("$dir/shared.so")
    fi
    for output in "${outputs[@]}"; do
    python3 - "$output" <<'PY'
from pathlib import Path
import struct,sys
b=Path(sys.argv[1]).read_bytes(); is64=b[4]==2
if is64:
    off=struct.unpack_from('<Q',b,40)[0]
    es,n,ni=struct.unpack_from('<HHH',b,58)
    fmt='<IIQQQQIIQQ'
else:
    off=struct.unpack_from('<I',b,32)[0]
    es,n,ni=struct.unpack_from('<HHH',b,46)
    fmt='<IIIIIIIIII'
s=[struct.unpack_from(fmt,b,off+i*es) for i in range(n)]
names=s[ni]; st=b[names[4]:names[4]+names[5]]
name=lambda x: st[x[0]:st.index(0,x[0])]
debug=next(x for x in s if name(x)==b'.debug_info')
pair=b[debug[4]:debug[4]+2]
assert pair==b'\xf8\x00',pair
print('MINILD_RISCV_ULEB=PASS file='+Path(sys.argv[1]).name+
      ' reserved_width=2 result=120 bytes=f800')
PY
    done
    # All malformed mutations are on identical real MiniAS objects.
    for variant in unpaired reversed overflow unterminated negative; do
        python3 - "$dir/pair.o" "$dir/$variant.o" "$variant" <<'PY'
from pathlib import Path
import struct,sys
src,dst,mode=sys.argv[1:]; b=bytearray(Path(src).read_bytes())
is64=b[4]==2
if is64:
    shoff=struct.unpack_from('<Q',b,40)[0]; es,n,ni=struct.unpack_from('<HHH',b,58)
    fmt='<IIQQQQIIQQ'; rf='<QQq'; rw=24
else:
    shoff=struct.unpack_from('<I',b,32)[0]; es,n,ni=struct.unpack_from('<HHH',b,46)
    fmt='<IIIIIIIIII'; rf='<IIi'; rw=12
s=[struct.unpack_from(fmt,b,shoff+i*es) for i in range(n)]
st=s[ni]; names=b[st[4]:st[4]+st[5]]
name=lambda x: names[x[0]:names.index(0,x[0])]
rel=next(x for x in s if name(x)==b'.rela.debug_info')
debug=next(x for x in s if name(x)==b'.debug_info')
first,second=rel[4],rel[4]+rw
o1,i1,a1=struct.unpack_from(rf,b,first)
o2,i2,a2=struct.unpack_from(rf,b,second)
mask=0xffffffff if is64 else 0xff
if mode=='unpaired':
    struct.pack_into(rf,b,second,o2,i2&~mask,a2)
elif mode=='reversed':
    struct.pack_into(rf,b,first,o1,(i1&~mask)|61,a1)
    struct.pack_into(rf,b,second,o2,(i2&~mask)|60,a2)
elif mode=='overflow':
    b[debug[4]]=0
    struct.pack_into(rf,b,first,o1,i1,8)  # final 128, cannot fit 1 byte
elif mode=='unterminated':
    b[debug[4]:debug[4]+10]=b'\x80'*10
elif mode=='negative':
    struct.pack_into(rf,b,first,o1,((i2&~mask)|60),a1)
    struct.pack_into(rf,b,second,o2,((i1&~mask)|61),a2)
else: raise AssertionError(mode)
Path(dst).write_bytes(b)
PY
        if "$minild" -m "$emulation" -static -e _start \
           -o "$dir/$variant.elf" "$dir/$variant.o" \
           >"$dir/$variant.log" 2>&1; then
            echo "MINILD_RISCV_ULEB=FAIL variant=$variant admitted" >&2
            exit 1
        fi
        grep -Fq 'riscv-uleb-' "$dir/$variant.log"
        echo "MINILD_RISCV_ULEB=PASS variant=$variant fail_closed=1"
    done

    gnu_ld=${GNU_LD:-riscv64-linux-gnu-ld.bfd}
    qemu=${QEMU_USER:-"qemu-riscv$width"}
    if [[ ${MINILD_REQUIRE_GNU:-0} == 1 ]] && \
       ! command -v "$gnu_ld" >/dev/null 2>&1; then
        echo "MINILD_RISCV_ULEB=FAIL missing_required_gnu_ld=$gnu_ld" >&2
        exit 1
    fi
    if [[ ${MINILD_REQUIRE_QEMU:-0} == 1 ]] && \
       ! command -v "$qemu" >/dev/null 2>&1; then
        echo "MINILD_RISCV_ULEB=FAIL missing_required_qemu=$qemu" >&2
        exit 1
    fi
    if command -v "$gnu_ld" >/dev/null 2>&1; then
        "$gnu_ld" -m "$emulation" -static -e _start \
            -o "$dir/gnu.elf" "$dir/pair.o"
        python3 - "$dir/mini.elf" "$dir/gnu.elf" <<'PY'
from pathlib import Path
import struct,sys
def get_pair(filename):
    b=Path(filename).read_bytes(); is64=b[4]==2
    if is64:
        off=struct.unpack_from('<Q',b,40)[0]; es,n,ni=struct.unpack_from('<HHH',b,58)
        fmt='<IIQQQQIIQQ'
    else:
        off=struct.unpack_from('<I',b,32)[0]; es,n,ni=struct.unpack_from('<HHH',b,46)
        fmt='<IIIIIIIIII'
    s=[struct.unpack_from(fmt,b,off+i*es) for i in range(n)]
    st=s[ni]; names=b[st[4]:st[4]+st[5]]
    name=lambda x: names[x[0]:names.index(0,x[0])]
    d=next(x for x in s if name(x)==b'.debug_info')
    return b[d[4]:d[4]+2]
one,two=map(get_pair,sys.argv[1:]); assert one==two,(one,two)
print('MINILD_RISCV_ULEB=PASS gnu_bfd_debug_bytes=equal')
PY
        if command -v "$qemu" >/dev/null 2>&1; then
            timeout 15s "$qemu" "$dir/mini.elf"
            timeout 15s "$qemu" "$dir/gnu.elf"
            echo "MINILD_RISCV_ULEB=PASS arch=rv$width gnu_bfd=1 qemu_runtime=2"
        else
            echo "MINILD_RISCV_ULEB=SKIP arch=rv$width no_qemu"
        fi
    else
        echo "MINILD_RISCV_ULEB=SKIP arch=rv$width no_gnu_cross_ld"
    fi
done
