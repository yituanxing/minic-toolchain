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

# A surviving empty TLS fragment may start the TLS block before the first
# nonempty fragment.  Reserve a non-TLS prefix so the empty section starts
# before the next TLS section's stronger alignment boundary.  PT_TLS base and
# both static/dynamic STT_TLS values must use the same retained TLS origin.
cat >"$work/tls-empty-first.s" <<'ASM'
.section .text.start,"ax",@progbits
.globl _start
_start:
  ret

.section .data.prefix,"aw",@progbits
.balign 8
  .dword 0x12345678

.section .tdata.anchor,"awT",@progbits
.balign 8
.globl tls_anchor
.type tls_anchor,@tls_object
tls_anchor:
.size tls_anchor,0

.section .tdata.value,"awT",@progbits
.balign 64
.globl tls_value
.type tls_value,@tls_object
tls_value:
  .dword 0x8877665544332211
.size tls_value,8
ASM
"$MINIAS" -march=rv64i -mabi=lp64 \
    -o "$work/tls-empty-first.o" "$work/tls-empty-first.s"
"$MINILD" -melf64lriscv -e _start \
    -o "$work/static-tls-empty-first.elf" "$work/tls-empty-first.o"
"$MINILD" -melf64lriscv -shared \
    -o "$work/shared-tls-empty-first.so" "$work/tls-empty-first.o"

# Zero-length surviving TLS still defines the origin for STT_TLS, but by
# itself must not produce a PT_TLS program header.
cat >"$work/tls-all-empty.s" <<'ASM'
.section .text.start,"ax",@progbits
.globl _start
_start:
  ret
.section .data.prefix,"aw",@progbits
  .dword 0x12345678
.section .tdata.empty,"awT",@progbits
.balign 64
.globl tls_empty
.type tls_empty,@tls_object
tls_empty:
.size tls_empty,0
ASM
"$MINIAS" -march=rv64i -mabi=lp64 \
    -o "$work/tls-all-empty.o" "$work/tls-all-empty.s"
"$MINILD" -melf64lriscv -shared \
    -o "$work/shared-tls-all-empty.so" "$work/tls-all-empty.o"

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

# Scripted ET_DYN prunes real input sections before dynamic metadata sizing.
# Allocated TLS sections removed by /DISCARD/ must also be invisible to the
# later PT_TLS segment planner, including SHT_NOBITS inputs whose size was
# reset to zero by the pre-prune pass.
sed '/^  .dynamic :/a\  /DISCARD/ : { *(.data.word) *(.tdata.slot) *(.tbss.slot) }' \
    "$work/vdso.lds" >"$work/vdso-tls-discard.lds"
if ! "$MINILD" -melf64lriscv -shared --hash-style=both \
    -soname libsegment-tls-discard.so -T "$work/vdso-tls-discard.lds" \
    -o "$work/vdso-tls-discard.so" "$work/input.o" \
    >"$work/vdso-tls-discard.stdout" \
    2>"$work/vdso-tls-discard.stderr"; then
    echo 'MINILD_SEGMENT_CHECK=FAIL mode=vdso-tls-discard unexpected-rejection' >&2
    sed -n '1,12p' "$work/vdso-tls-discard.stderr" >&2
    exit 1
fi

# A surviving TLS input cannot be represented by this constrained Linux
# vDSO script: its PHDRS require a single read/execute PT_LOAD and have no
# PT_TLS entry or writable PT_LOAD.  Do not accept live TLS accidentally while
# fixing the discarded-only case.  Exercise both initialized and NOBITS TLS.
sed '/^  .dynamic :/a\  .tdata : { *(.tdata.slot) } :text\
  /DISCARD/ : { *(.data.word) *(.tbss.slot) }' \
    "$work/vdso.lds" >"$work/vdso-tls-live-data.lds"
sed '/^  .dynamic :/a\  .tbss : { *(.tbss.slot) } :text\
  /DISCARD/ : { *(.data.word) *(.tdata.slot) }' \
    "$work/vdso.lds" >"$work/vdso-tls-live-bss.lds"
for kind in data bss; do
    if "$MINILD" -melf64lriscv -shared --hash-style=both \
        -soname libsegment-tls-live.so -T "$work/vdso-tls-live-$kind.lds" \
        -o "$work/vdso-tls-live-$kind.so" "$work/input.o" \
        >"$work/vdso-tls-live-$kind.stdout" \
        2>"$work/vdso-tls-live-$kind.stderr"; then
        echo "MINILD_SEGMENT_CHECK=FAIL mode=vdso-tls-live-$kind unexpectedly-linked" >&2
        exit 1
    fi
    if ! grep -q 'invalid-shared-tls-segment-layout' \
        "$work/vdso-tls-live-$kind.stderr"; then
        echo "MINILD_SEGMENT_CHECK=FAIL mode=vdso-tls-live-$kind missing-diagnostic" >&2
        sed -n '1,12p' "$work/vdso-tls-live-$kind.stderr" >&2
        exit 1
    fi
done

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

raw, ph, names = inspect('shared-tls-empty-first.so', 3)
tls = [p for p in ph if p[0] == 7]
assert len(tls) == 1, ('empty-first TLS segment count', ph)
anchor = one(names, '.tdata.anchor')
value = one(names, '.tdata.value')
assert anchor[5] == 0 and value[5] == 8, ('TLS fixture section sizes', anchor, value)
assert anchor[3] < value[3], ('empty TLS must precede aligned nonempty TLS', anchor, value)
assert tls[0][3] == anchor[3], ('PT_TLS must begin at retained empty TLS section', tls[0], anchor)
rw_loads = [p for p in ph if p[0] == 1 and p[1] == 6]
assert len(rw_loads) == 1, ('TLS writable LOAD missing', ph)
# A zero-length section may have sh_offset=0 in the section table, even
# though its virtual address is inside the mapped TLS template.
assert tls[0][2] == rw_loads[0][2] + anchor[3] - rw_loads[0][3], (
    'PT_TLS file base must be derived from writable LOAD', tls[0], rw_loads[0])
assert tls[0][5] == tls[0][6] == value[3] + value[5] - anchor[3], (
    'PT_TLS span must include alignment gap', tls[0], anchor, value)


def tls_symbols(section_name, str_name):
    table = one(names, section_name)
    strings = one(names, str_name)
    names_blob = raw[strings[4]:strings[4] + strings[5]]
    records = {}
    for offset in range(table[4], table[4] + table[5], 24):
        name, info, _, shndx, address, size = struct.unpack_from('<IBBHQQ', raw, offset)
        if not name:
            continue
        end = names_blob.find(b'\0', name)
        assert end > name, ('TLS symbol name bounds', section_name, name)
        records[names_blob[name:end].decode()] = (info, shndx, address, size)
    return records


expected_tls_offsets = {'tls_anchor': 0, 'tls_value': value[3] - anchor[3]}
for table_name, str_name in (('.symtab', '.strtab'), ('.dynsym', '.dynstr')):
    records = tls_symbols(table_name, str_name)
    for name, expected in expected_tls_offsets.items():
        assert name in records, ('TLS symbol missing', table_name, name)
        info, shndx, offset, size = records[name]
        assert info & 0xf == 6 and shndx != 0, ('not a defined STT_TLS', table_name, name)
        assert offset == expected, ('TLS relative value mismatch', table_name, name,
                                     offset, expected)
print('MINILD_SEGMENT_CHECK=PASS mode=shared-tls-empty-first PT_TLS=retained-zero-origin'
      ' STT_TLS=static+dynamic')

raw, ph, names = inspect('static-tls-empty-first.elf', 2)
anchor = one(names, '.tdata.anchor')
value = one(names, '.tdata.value')
assert anchor[3] < value[3], ('static empty-first TLS section VMA order', anchor, value)
for name, expected in {'tls_anchor': 0, 'tls_value': value[3] - anchor[3]}.items():
    info, shndx, offset, size = tls_symbols('.symtab', '.strtab')[name]
    assert info & 0xf == 6 and shndx != 0 and offset == expected, (
        'ET_EXEC TLS origin mismatch', name, offset, expected)
print('MINILD_SEGMENT_CHECK=PASS mode=static-tls-empty-first STT_TLS=retained-zero-origin')

raw, ph, names = inspect('shared-tls-all-empty.so', 3)
assert not any(p[0] == 7 for p in ph), ('empty-only TLS incorrectly produced PT_TLS', ph)
assert one(names, '.tdata.empty')[5] == 0, 'empty TLS section not retained'
for table_name, strings_name in (('.symtab', '.strtab'), ('.dynsym', '.dynstr')):
    info, shndx, offset, size = tls_symbols(table_name, strings_name)['tls_empty']
    assert info & 0xf == 6 and shndx != 0 and offset == size == 0, (
        'empty-only TLS symbol mismatch', table_name, offset, size)
print('MINILD_SEGMENT_CHECK=PASS mode=shared-tls-all-empty PT_TLS=absent STT_TLS=defined')

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

raw, ph, names = inspect('vdso-tls-discard.so', 3)
assert [p[0] for p in ph] == [1, 2, 4, 0x6474e550], (
    'discarded TLS must not synthesize PT_TLS', ph)
assert not any(p[0] == 7 for p in ph), ('unexpected discarded PT_TLS', ph)
for discarded in ('.tdata.slot', '.tbss.slot', '.data.word', '.tdata', '.tbss'):
    assert discarded not in names, ('discarded TLS/data leaked', discarded)
assert len([p for p in ph if p[0] == 1]) == 1
print('MINILD_SEGMENT_CHECK=PASS mode=vdso-tls-discard PT_TLS=absent')
print('MINILD_SEGMENT_CHECK=PASS mode=vdso-tls-live-mixed fail_closed=2')
PY

echo 'MINILD_SEGMENTS=PASS modes=11'
