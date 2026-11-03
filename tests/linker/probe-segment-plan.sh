#!/usr/bin/env bash
# Host-only PHDR ownership contract across ET_EXEC and ET_DYN policies.
# Inspect ELF64 bytes directly; GNU RISC-V binutils and QEMU are not required.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
command -v python3 >/dev/null 2>&1 || {
    echo "MINILD_SEGMENTS=INCONCLUSIVE missing_tool=python3" >&2
    exit 8
}

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-segment-plan.XXXXXX")
finish() {
    if [[ ${MINILD_SEGMENT_KEEP:-0} == 1 ]]; then
        echo "MINILD_SEGMENT_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap finish EXIT

cat >"$work/input.s" <<'ASM'
.section .text.start,"ax",@progbits
.globl _start
.type _start,@function
_start:
  addi a0,zero,17
  ret
.size _start,.-_start

.section .data.word,"aw",@progbits
.balign 8
  .dword 0x1122334455667788

.section .tdata.slot,"awT",@progbits
.balign 8
.globl tls_slot
.type tls_slot,@tls_object
tls_slot:
  .dword 0x12345678
.size tls_slot,8

.section .tbss.slot,"awT",@nobits
.balign 8
  .zero 16

.section .note.minild,"a",@note
.balign 4
  .long 4,4,1
  .ascii "GNU\0"
  .long 0x11223344
ASM

cat >"$work/static.lds" <<'LDS'
OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0x20000;
  .text : { *(.text.start) }
  . = 0x30000;
  .data : AT(0x50000) { *(.data.word) }
  .tdata : { *(.tdata.slot) }
  .tbss : { *(.tbss.slot) }
  .note : { *(.note.minild) }
}
LDS

cat >"$work/vdso.lds" <<'LDS'
OUTPUT_ARCH(riscv)
SECTIONS {
  . = SIZEOF_HEADERS;
  .note : { *(.note.minild) } :text :note
  .hash : { *(.hash) } :text
  .gnu.hash : { *(.gnu.hash) } :text
  .dynsym : { *(.dynsym) } :text
  .dynstr : { *(.dynstr) } :text
  .gnu.version : { *(.gnu.version) } :text
  .gnu.version_d : { *(.gnu.version_d) } :text
  .rela.dyn : { *(.rela.dyn) } :text
  .text : { *(.text.start) } :text
  .dynamic : { *(.dynamic) } :text :dynamic
}
PHDRS {
  text PT_LOAD FLAGS(5) FILEHDR PHDRS;
  dynamic PT_DYNAMIC FLAGS(4);
  note PT_NOTE FLAGS(4);
  eh_frame_hdr PT_GNU_EH_FRAME;
}
VERSION {
  MINILD_1 {
    global: _start;
    local: *;
  };
}
LDS

"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/input.o" "$work/input.s"
"$MINILD" -melf64lriscv -e _start -o "$work/static-default.elf" "$work/input.o"
"$MINILD" -melf64lriscv -T "$work/static.lds" -o "$work/static-script.elf" "$work/input.o"
"$MINILD" -melf64lriscv -shared -o "$work/shared.so" "$work/input.o"
"$MINILD" -melf64lriscv -pie --dynamic-linker=/lib/ld-musl-riscv64.so.1 \
    -e _start -o "$work/pie.elf" "$work/input.o"

# The vDSO script deliberately excludes writable/TLS inputs. It must preserve
# the fixed four PHDRs (including the empty explicit EH-frame header).
cat >"$work/vdso-input.s" <<'ASM'
.section .text.start,"ax",@progbits
.globl _start
_start:
  addi a0,zero,17
  ret
.section .note.minild,"a",@note
.balign 4
  .long 4,4,1
  .ascii "GNU\0"
  .long 0x11223344
ASM
"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/vdso-input.o" "$work/vdso-input.s"
"$MINILD" -melf64lriscv -shared --hash-style=both -soname libsegment.so \
    -T "$work/vdso.lds" \
    -o "$work/vdso.so" "$work/vdso-input.o"

python3 - "$work" <<'PY'
import struct
import sys
from pathlib import Path

base = Path(sys.argv[1])


def inspect(filename, etype):
    raw = (base / filename).read_bytes()
    assert raw[:6] == b'\x7fELF\x02\x01', (filename, 'ELF64 little endian')
    assert struct.unpack_from('<HH', raw, 16) == (etype, 243), (filename, 'ELF type/arch')
    phoff = struct.unpack_from('<Q', raw, 32)[0]
    phentsize, phnum = struct.unpack_from('<HH', raw, 54)
    assert phentsize == 56 and phoff + phnum * 56 <= len(raw), (filename, 'PHDR bounds')
    ph = [struct.unpack_from('<IIQQQQQQ', raw, phoff + i * 56)
          for i in range(phnum)]
    shoff = struct.unpack_from('<Q', raw, 40)[0]
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', raw, 58)
    assert shentsize == 64 and shstrndx < shnum and shoff + shnum * 64 <= len(raw)
    sh = [struct.unpack_from('<IIQQQQIIQQ', raw, shoff + i * 64)
          for i in range(shnum)]
    strings = raw[sh[shstrndx][4]:sh[shstrndx][4] + sh[shstrndx][5]]
    names = {}
    for sec in sh:
        start = sec[0]
        end = strings.find(b'\0', start)
        assert end >= start, (filename, 'section name')
        names.setdefault(strings[start:end].decode(), []).append(sec)
    for segment in ph:
        typ, flags, off, vaddr, paddr, filesz, memsz, align = segment
        assert filesz <= memsz and off + filesz <= len(raw), (filename, 'segment extent', segment)
        assert align in (0, 1) or (align & (align - 1)) == 0, (filename, 'alignment')
        if align > 1:
            assert (vaddr - off) % align == 0, (filename, 'segment congruence')
    return raw, ph, names


def one(names, name):
    assert len(names.get(name, [])) == 1, (name, names.get(name))
    return names[name][0]


def covers(segment, sec):
    return (segment[3] <= sec[3] and sec[3] + sec[5] <= segment[3] + segment[6])


raw, ph, names = inspect('static-default.elf', 2)
loads = [p for p in ph if p[0] == 1]
assert len(loads) == 2 and len(ph) == 2, ('ET_EXEC default PHDRs', ph)
assert loads[0][1] == 5 and loads[1][1] == 6 and all(p[7] == 4096 for p in loads)
assert loads[0][2] >= 4096 and loads[0][3] >= 0x10000
assert not (loads[0][2] <= 0 < loads[0][2] + loads[0][5]), 'static LOAD must exclude ELF header'
assert covers(loads[0], one(names, '.text.start'))
assert covers(loads[1], one(names, '.data.word'))
print('MINILD_SEGMENT_CHECK=PASS mode=static-default loads=2')

raw, ph, names = inspect('static-script.elf', 2)
loads = [p for p in ph if p[0] == 1]
assert len(loads) == 3 and all(p[7] == 1 for p in loads), ('script LOAD alignment', ph)
data = one(names, '.data')
assert any(covers(p, data) and p[4] != p[3] for p in loads), ('AT() LMA lost', ph)
assert any(covers(p, one(names, '.text')) for p in loads)
assert any(p[1] == 6 and p[6] > p[5] for p in loads), ('NOBITS LOAD tail', ph)
assert one(names, '.note')[5] == 20
print(f'MINILD_SEGMENT_CHECK=PASS mode=static-script loads={len(loads)} AT_LMA=true')

raw, ph, names = inspect('shared.so', 3)
loads = [p for p in ph if p[0] == 1]
dyn = [p for p in ph if p[0] == 2]
tls = [p for p in ph if p[0] == 7]
assert len(loads) == 2 and len(dyn) == 1 and len(tls) == 1, ('ET_DYN PHDRs', ph)
assert loads[0][2] == 0 and loads[0][3] == 0 and loads[0][5] >= 64 + len(ph) * 56
assert dyn[0][2] == one(names, '.dynamic')[4] and dyn[0][3] == one(names, '.dynamic')[3]
assert tls[0][5] > 0 and tls[0][6] > tls[0][5], ('TLS .tdata + .tbss', tls[0])
assert covers(tls[0], one(names, '.tdata.slot')) and covers(tls[0], one(names, '.tbss.slot'))
print('MINILD_SEGMENT_CHECK=PASS mode=shared PT_DYNAMIC=true PT_TLS=true')

raw, ph, names = inspect('pie.elf', 3)
loads = [p for p in ph if p[0] == 1]
phdr = [p for p in ph if p[0] == 6]
interp = [p for p in ph if p[0] == 3]
assert len(loads) == 2 and len(phdr) == 1 and len(interp) == 1
assert phdr[0][2] == 64 and phdr[0][5] == len(ph) * 56
assert phdr[0][3] == loads[0][3] + 64 and loads[0][2] == 0
assert interp[0][2] == one(names, '.interp')[4] and interp[0][3] == one(names, '.interp')[3]
assert raw[interp[0][2]:interp[0][2] + interp[0][5]] == b'/lib/ld-musl-riscv64.so.1\0'
print('MINILD_SEGMENT_CHECK=PASS mode=pie PT_PHDR=true PT_INTERP=true')

raw, ph, names = inspect('vdso.so', 3)
assert [p[0] for p in ph] == [1, 2, 4, 0x6474e550], ('vDSO PHDR order', ph)
assert ph[0][1] == 5 and ph[0][2] == ph[0][3] == 0
assert ph[0][5] >= 64 + len(ph) * 56
assert ph[1][1] == ph[2][1] == 4
assert ph[1][2] == one(names, '.dynamic')[4] and ph[1][3] == one(names, '.dynamic')[3]
assert ph[2][2] == one(names, '.note')[4] and ph[2][3] == one(names, '.note')[3]
assert ph[3][5] == ph[3][6] == 0, ('empty explicit EH header', ph[3])
print('MINILD_SEGMENT_CHECK=PASS mode=vdso PHDRS=4 empty_eh=true')
PY

echo 'MINILD_SEGMENTS=PASS modes=5'
