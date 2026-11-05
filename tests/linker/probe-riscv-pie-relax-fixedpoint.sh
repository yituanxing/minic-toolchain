#!/usr/bin/env bash
# RV64 ET_DYN must use the same CALL/ALIGN source-offset projection as static
# ET_EXEC, with all dynamic symbol and relocation reservations remaining fixed.
set -Eeuo pipefail
: "${MINIAS:?set MINIAS}"
: "${MINILD:?set MINILD}"

work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

cat >"$work/pie.s" <<'ASM'
.cfi_sections .eh_frame, .debug_frame
.option relax
.text
.globl _start
.type _start,@function
_start:
    .cfi_startproc simple
    .cfi_def_cfa 2,0
    call local_target
.globl after_local_call
after_local_call:
    .cfi_def_cfa_offset 16
    .p2align 4
.globl after_alignment
after_alignment:
    .cfi_def_cfa_offset 24
    ret
.globl local_target
local_target:
    ret
    .cfi_endproc
.size _start,.-_start
.section .note.minild,"a",@note
.balign 4
    .long 4,4,1
    .ascii "GNU\0"
    .long 0x11223344
ASM

"$MINIAS" --target=riscv -march=rv64i2p1 -mabi=lp64 \
    -o "$work/pie.o" "$work/pie.s"
readelf -Wr "$work/pie.o" | grep -q R_RISCV_RELAX
readelf -Wr "$work/pie.o" | grep -q R_RISCV_ALIGN
"$MINILD" -melf64lriscv -pie -e _start \
    -o "$work/pie.elf" "$work/pie.o"

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
  .text : { *(.text) } :text
  .eh_frame : { *(.eh_frame) } :text
  .debug_frame : { *(.debug_frame) }
  .dynamic : { *(.dynamic) } :text :dynamic
}
PHDRS {
  text PT_LOAD FLAGS(5) FILEHDR PHDRS;
  dynamic PT_DYNAMIC FLAGS(4);
  note PT_NOTE FLAGS(4);
  eh_frame_hdr PT_GNU_EH_FRAME;
}
VERSION {
  LINUX_4.15 {
    global: _start;
    local: *;
  };
}
LDS
"$MINILD" -melf64lriscv -shared --hash-style=both \
    -soname libminild-relax.so -T "$work/vdso.lds" \
    -o "$work/vdso.elf" "$work/pie.o"

# A default-visibility undefined DSO call is PLT-bound even with RELAX.
# Shrinking that pair against the unresolved symbol would break JUMP_SLOT.
cat >"$work/import.s" <<'ASM'
.option relax
.text
.globl imported_call
imported_call:
    call externally_named
    ret
ASM
"$MINIAS" --target=riscv -march=rv64i2p1 -mabi=lp64 \
    -o "$work/import.o" "$work/import.s"
readelf -Wr "$work/import.o" | grep -q R_RISCV_RELAX
"$MINILD" -melf64lriscv -shared -soname libminild-import.so \
    -o "$work/import.elf" "$work/import.o"

python3 - "$work/pie.elf" "$work/vdso.elf" "$work/import.elf" <<'PY'
import pathlib
import re
import struct
import subprocess
import sys

for stage, filename in zip(('pie','scripted_dso'), sys.argv[1:]):
    path = pathlib.Path(filename)
    ehdr = path.read_bytes()[:64]
    assert ehdr[:4] == b'\x7fELF' and ehdr[4] == 2
    assert struct.unpack_from('<H', ehdr, 16)[0] == 3, 'not ET_DYN'

    symbols = {}
    for line in subprocess.check_output(
            ['readelf', '-Ws', str(path)], text=True).splitlines():
        cols = line.split()
        if len(cols) >= 8 and cols[0].endswith(':'):
            if cols[-1] in ('_start','after_local_call','after_alignment','local_target'):
                symbols[cols[-1]] = (int(cols[1], 16), int(cols[2]))
    assert len(symbols) == 4, symbols
    start, start_size = symbols['_start']
    after = symbols['after_local_call'][0]
    aligned = symbols['after_alignment'][0]
    target = symbols['local_target'][0]
    assert after - start == 4, (stage, start, after)
    assert aligned % 16 == 0, (stage, start, aligned)
    assert target - aligned == 4 and start_size == target + 4 - start, (
        stage, start, target, start_size)

    text_vma = text_off = text_size = None
    for line in subprocess.check_output(
            ['readelf', '-SW', str(path)], text=True).splitlines():
        match = re.match(
            r'^\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+'
            r'([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)', line, re.I)
        if match and match.group(1) == '.text':
            text_vma, text_off, text_size = (
                int(v, 16) for v in match.groups()[1:])
            break
    assert text_off is not None
    text = path.read_bytes()[text_off:text_off+text_size]
    call = struct.unpack_from('<I', text, start-text_vma)[0]
    assert (call & 0x7f, (call >> 7) & 31) == (0x6f,1), (stage, hex(call))

    frame_dump = subprocess.check_output(
        ['readelf', '--debug-dump=frames', str(path)], text=True)
    frames = {}
    current = None
    for line in frame_dump.splitlines():
        frame = re.search(r'Contents of the (\.(?:eh|debug)_frame) section:', line)
        if frame:
            current = frame.group(1)
            frames[current] = []
        elif current and 'DW_CFA_advance_loc:' in line:
            pc = re.search(r'\bto\s+(?:0x)?([0-9a-fA-F]+)\b', line)
            if pc:
                frames[current].append(int(pc.group(1),16))
    for frame in ('.eh_frame','.debug_frame'):
        assert frames.get(frame, [])[:2] == [after, aligned], (
            stage, frame, frames, frame_dump)
    print(f'MINILD_RV64_RELAX_SHARED_CASE=PASS stage={stage} '
          'ET_DYN=true CALL=JAL ALIGN=16 '
          f'st_size={start_size} CFI_eh=exact CFI_debug=exact')

print('MINILD_RV64_SHARED_FIXEDPOINT=PASS modes=PIE,scripted_DSO '
      'same_machine_event_projection=true')

import_path = pathlib.Path(sys.argv[3])
symbols = subprocess.check_output(['readelf','-Ws',str(import_path)],text=True)
text_vma = text_off = text_size = None
for line in subprocess.check_output(
        ['readelf','-SW',str(import_path)],text=True).splitlines():
    match = re.match(
        r'^\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+'
        r'([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)', line, re.I)
    if match and match.group(1) == '.text':
        text_vma, text_off, text_size = (
            int(v,16) for v in match.groups()[1:])
        break
assert text_off is not None
text = import_path.read_bytes()[text_off:text_off+text_size]
auipc, jalr = struct.unpack_from('<II',text,0)
assert auipc & 0x7f == 0x17 and jalr & 0x707f == 0x67, (
    hex(auipc),hex(jalr))
relocs = subprocess.check_output(['readelf','-Wr',str(import_path)],text=True)
assert 'R_RISCV_JUMP_SLOT' in relocs and 'externally_named' in relocs, relocs
print('MINILD_RV64_SHARED_PLT=PASS external_RELAX_CALL=AUIPC_JALR '
      'JUMP_SLOT=preserved')
PY
