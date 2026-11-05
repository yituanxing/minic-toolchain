#!/usr/bin/env bash
# In AArch64 PIC images GNU BFD allocates global GOT slots in ELF link-hash
# bucket traversal order, not first-encounter relocation order.  Strong and
# undefined weak symbols must participate in the same ordering.
set -Eeuo pipefail

: "${MINIAS:?set MINIAS to minic-as}"
: "${MINILD:?set MINILD to minic-ld}"

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-aarch64-got-bfd-order.XXXXXX")
trap 'rm -r -- "$work"' EXIT

cat >"$work/entry.S" <<'ASM'
.section .text,"ax",@progbits
.global _start
_start:
    adrp x0,:got:got_delta
    ldr x0,[x0,:got_lo12:got_delta]
    adrp x0,:got:got_missing
    ldr x0,[x0,:got_lo12:got_missing]
    adrp x0,:got:got_beta
    ldr x0,[x0,:got_lo12:got_beta]
    adrp x0,:got:got_epsilon
    ldr x0,[x0,:got_lo12:got_epsilon]
    adrp x0,:got:got_alpha
    ldr x0,[x0,:got_lo12:got_alpha]
    adrp x0,:got:got_gamma
    ldr x0,[x0,:got_lo12:got_gamma]
    ret
.weak got_missing
.section .data,"aw",@progbits
.balign 8
.global got_alpha, got_beta, got_gamma, got_delta, got_epsilon
got_alpha:   .quad 0x1111
got_beta:    .quad 0x2222
got_gamma:   .quad 0x3333
got_delta:   .quad 0x4444
got_epsilon: .quad 0x5555
ASM

cat >"$work/link.lds" <<'LDS'
ENTRY(_start)
SECTIONS {
    .text 0x400000 : { *(.text) }
    .data 0x401000 : { *(.data) }
    .got 0x402000 : { *(.got) }
    .got.plt 0x403000 : { *(.got.plt) }
    .rela.dyn 0x404000 : { *(.rela.dyn) }
}
LDS

"$MINIAS" --target=aarch64 -march=armv8-a -mabi=lp64 \
    -o "$work/probe.o" "$work/entry.S"
"$MINILD" -m aarch64linux -shared -Bsymbolic -z notext \
    --no-apply-dynamic-relocs -T "$work/link.lds" \
    -o "$work/mini.elf" "$work/probe.o"

check_slots() {
    python3 - "$@" <<'PY'
from pathlib import Path
import struct
import sys

names=['got_delta','got_missing','got_beta',
       'got_epsilon','got_alpha','got_gamma']
def bfd_hash(name):
    value=0
    for c in name.encode():
        value=(value+c+(c<<17))&((1<<64)-1)
        value^=value>>2
    n=len(name)
    value=(value+n+(n<<17))&((1<<64)-1)
    return value^(value>>2)

def sections(path):
    data=Path(path).read_bytes()
    shoff=struct.unpack_from('<Q',data,40)[0]
    es,n,st=struct.unpack_from('<HHH',data,58)
    sh=[struct.unpack_from('<IIQQQQIIQQ',data,shoff+i*es)
        for i in range(n)]
    names_sec=sh[st]
    pool=data[names_sec[4]:names_sec[4]+names_sec[5]]
    result={}
    for s in sh:
        name=pool[s[0]:pool.index(0,s[0])].decode()
        result[name]=(s[3],data[s[4]:s[4]+s[5]])
    return result

def decode(path):
    secs=sections(path)
    assert '.text' in secs and '.got' in secs, secs.keys()
    text_addr,code=secs['.text']
    got_addr,got_data=secs['.got']
    assert len(got_data)==56,(path,len(got_data))
    assert got_data[:8]==b'\0'*8,(path,got_data[:8])
    pairs={}
    for i,name in enumerate(names):
        adrp,ldr=struct.unpack_from('<II',code,i*8)
        assert (adrp&0x9f000000)==0x90000000,(name,hex(adrp))
        assert (ldr&0xffc00000)==0xf9400000,(name,hex(ldr))
        imm=(((adrp>>5)&0x7ffff)<<2)|((adrp>>29)&3)
        if imm&(1<<20):imm-=1<<21
        page=((text_addr+i*8)&~0xfff)+(imm<<12)
        slot=page+(((ldr>>10)&0xfff)<<3)
        assert got_addr+8<=slot<got_addr+len(got_data),(name,hex(slot))
        pairs[name]=slot-got_addr
    ordered=sorted(names,key=lambda name:bfd_hash(name)%4051)
    assert [pairs[name] for name in ordered]==list(range(8,56,8)),(
        path,ordered,pairs)
    # Undefined weak has an ordinary reserved slot and a zero payload.
    assert struct.unpack_from('<Q',got_data,pairs['got_missing'])[0]==0
    return pairs,got_data,code

baseline=decode(sys.argv[1])
print('MINILD_AARCH64_KERNEL_GOT_ORDER=PASS '
      'symbols=6 global_hash_buckets=4051 includes_undefweak=true')
if len(sys.argv)>2:
    gnu=decode(sys.argv[2])
    assert baseline==gnu,('GNU GOT/code parity',baseline[:2],gnu[:2])
    print('MINILD_AARCH64_KERNEL_GOT_ORDER_GNU=PASS '
          'exact_got_bytes_and_instruction_slots')
PY
}

if [[ ${MINILD_REQUIRE_GNU:-0} == 1 ]] && \
   ! command -v aarch64-linux-gnu-ld >/dev/null 2>&1; then
    echo 'MINILD_AARCH64_KERNEL_GOT_ORDER=FAIL missing_GNU_ld' >&2
    exit 1
fi

if command -v aarch64-linux-gnu-ld >/dev/null 2>&1; then
    aarch64-linux-gnu-ld -m aarch64linux -shared -Bsymbolic -z notext \
        --no-apply-dynamic-relocs -T "$work/link.lds" \
        -o "$work/gnu.elf" "$work/probe.o"
    check_slots "$work/mini.elf" "$work/gnu.elf"
else
    check_slots "$work/mini.elf"
    echo 'MINILD_AARCH64_KERNEL_GOT_ORDER_GNU=SKIP missing_cross_binutils'
fi
