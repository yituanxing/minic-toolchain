#!/usr/bin/env bash
# GNU/BFD RISC-V final-link relaxation parity: GP/x0 data references and
# CALL reach under an explicitly aligned output section.  Both RV32 and RV64
# use the same MiniLD machine-level fixed-point implementation.
set -Eeuo pipefail
: "${MINIAS:?set MINIAS to minic-as}"
: "${MINILD:?set MINILD to minic-ld}"

work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

cat > "$work/reference.s" <<'ASM'
.option norvc
.option relax
.section .text.gp,"ax",@progbits
.globl _start
_start:
.globl gp_lla
gp_lla:
    lla t0, near_gp
.globl gp_store
gp_store:
    sw a0,near_gp,t1
.globl weak_lla
weak_lla:
    lla t2, missing_weak
.globl gp_boundary
gp_boundary:
    lla t4,edge_gp
.globl far_lla
far_lla:
    lla t3, far_data
.globl ordinary_call
ordinary_call:
    call ordinary_target
.globl ordinary_target
ordinary_target:
    ret
.option norelax
.globl unrelaxed_gp
unrelaxed_gp:
    lla t4, near_gp
    ret
.weak missing_weak

.option relax
.section .init.text,"ax",@progbits
.globl aligned_call
aligned_call:
    call aligned_target
.globl aligned_target
aligned_target:
    ret

.section .sdata,"aw",@progbits
.balign 8
.globl edge_gp
edge_gp:
    .word 0xdeadbeef
    .space 1272
.globl near_gp
near_gp:
    .word 0x11223344
.section .data.far,"aw",@progbits
.globl far_data
far_data:
    .word 0x55667788
ASM

cat > "$work/layout.ld" <<'LDS'
OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text.gp) }
  . = 0x20000;
  .init.text : ALIGN(0x400000) { *(.init.text) }
  . = 0x900000;
  .sdata : { *(.sdata) __global_pointer$ = . + 0x300; }
  . = 0xb00000;
  .data : { *(.data.far) }
}
LDS

for bits in 32 64; do
    if [[ $bits == 32 ]]; then abi=ilp32; else abi=lp64; fi
    "$MINIAS" --target=riscv "-march=rv${bits}i2p1" "-mabi=$abi" \
        -o "$work/$bits.o" "$work/reference.s"
    "$MINILD" "-melf${bits}lriscv" -static -e _start \
        -T "$work/layout.ld" -o "$work/$bits.elf" "$work/$bits.o"
done

python3 - "$work" <<'PY'
import pathlib, struct, sys

root = pathlib.Path(sys.argv[1])


def inspect(path):
    data = path.read_bytes()
    assert data[:4] == b'\x7fELF'
    bits = 32 if data[4] == 1 else 64
    if bits == 32:
        shoff = struct.unpack_from('<I', data, 32)[0]
        shentsize, shnum, shstrndx = struct.unpack_from('<HHH', data, 46)
        shfmt = '<IIIIIIIIII'
        symfmt = '<IIIBBH'
    else:
        shoff = struct.unpack_from('<Q', data, 40)[0]
        shentsize, shnum, shstrndx = struct.unpack_from('<HHH', data, 58)
        shfmt = '<IIQQQQIIQQ'
        symfmt = '<IBBHQQ'
    shdr = [struct.unpack_from(shfmt, data, shoff+i*shentsize)
            for i in range(shnum)]
    if bits == 32:
        getsec = lambda h: (h[0], h[3], h[4], h[5], h[8], h[6], h[9])
    else:
        getsec = lambda h: (h[0], h[3], h[4], h[5], h[8], h[6], h[9])
    nameoff, _, str_off, str_len, _, _, _ = getsec(shdr[shstrndx])
    names = data[str_off:str_off+str_len]
    def getname(table, index):
        return table[index:table.index(b'\0', index)].decode()
    sections = {getname(names, getsec(h)[0]): (i, getsec(h))
                for i,h in enumerate(shdr)}
    sym_sec = next(h for h in shdr if h[1] == 2)
    if bits == 32:
        sym_off,sym_size,sym_str,sym_entry = sym_sec[4],sym_sec[5],sym_sec[6],sym_sec[9]
    else:
        sym_off,sym_size,sym_str,sym_entry = sym_sec[4],sym_sec[5],sym_sec[6],sym_sec[9]
    string_hdr = shdr[sym_str]
    string_data = data[string_hdr[4]:string_hdr[4]+string_hdr[5]]
    symbols = {}
    for i in range(sym_size//sym_entry):
        sym = struct.unpack_from(symfmt, data, sym_off+i*sym_entry)
        if bits == 32:
            nm,value,_,_,_,_ = sym
        else:
            nm,_,_,_,value,_ = sym
        if nm: symbols[getname(string_data, nm)] = value
    def insn(symbol, section):
        _, (_,addr,off,size,_,_,_) = sections[section]
        loc = symbols[symbol]-addr
        assert 0 <= loc <= size-4, (bits, symbol, hex(loc))
        return struct.unpack_from('<I', data, off+loc)[0]
    def assert_base(symbol, base, expected_opcode):
        ins = insn(symbol,'.text')
        assert ins&127 == expected_opcode, (bits,symbol,hex(ins))
        assert (ins>>15)&31 == base, (bits,symbol,hex(ins))
    assert_base('gp_lla',3,0x13)
    assert_base('gp_store',3,0x23)
    assert_base('weak_lla',0,0x13)
    # Signed-12 itself fits here, but GNU's same-output alignment and
    # object-size reservation make this an unsafe GP shrink candidate.
    assert insn('gp_boundary','.text')&127 == 0x17
    assert insn('far_lla','.text')&127 == 0x17
    assert insn('unrelaxed_gp','.text')&127 == 0x17
    assert insn('ordinary_call','.text')&127 == 0x6f
    assert insn('aligned_call','.init.text')&127 == 0x17
    _, (_,_,_,_,align,_,_) = sections['.init.text']
    assert align == 0x400000, (bits,hex(align))
    print(f'MINILD_RISCV_GP_ALIGN_RELAX=PASS rv={bits} '
          'GP_I=gp GP_S=gp BOUNDARY=AUIPC '
          'WEAK_I=x0 FAR=AUIPC '
          'NO_RELAX=AUIPC CALL=JAL INIT_CALL=AUIPC_JALR '
          'init_sh_addralign=0x400000')


for bits in (32,64):
    inspect(root/f'{bits}.elf')
PY
