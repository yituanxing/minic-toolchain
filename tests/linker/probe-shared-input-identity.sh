#!/usr/bin/env bash
# A shared/vDSO linker script must select original input sections by object
# filename, not a prematurely merged same-named work section.  x86-64 GNU ld
# supplies an independent layout oracle; MiniLD emits its RV64 ELF variant.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
for tool in python3 readelf as ld sed; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "MINILD_SHARED_INPUT=INCONCLUSIVE missing_tool=$tool" >&2
        exit 8
    fi
done

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-shared-input.XXXXXX")
cleanup() {
    if [[ ${MINILD_SHARED_INPUT_KEEP:-0} == 1 ]]; then
        echo "MINILD_SHARED_INPUT_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap cleanup EXIT

for target in rv host; do
    if [[ $target == rv ]]; then
        pointer='.dword'
    else
        pointer='.quad'
    fi
    cat >"$work/$target-entry.s" <<ASM
.section .text.entry,"ax",@progbits
.balign 8
.globl _start
_start:
    .byte 0xe1,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8
.section .data.refs,"aw",@progbits
.balign 8
.globl exported_refs
exported_refs:
    $pointer alpha_fn
    $pointer zeta_fn
    $pointer outside_symbol
.section .note.minild,"a",@note
.balign 4
    .long 4,4,1
    .ascii "GNU\0"
    .long 0x11223344
ASM
    cat >"$work/$target-alpha.s" <<'ASM'
.section .text.same,"ax",@progbits
.balign 8
.globl alpha_fn
alpha_fn:
    .byte 0xa1,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8
.section .text.rank.a,"ax",@progbits
.balign 8
.globl rank_a
rank_a:
    .byte 0xc1,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8
.section .meta.alpha,"ao",@progbits,alpha_fn
    .long 0x11223344
ASM
    cat >"$work/$target-zeta.s" <<'ASM'
.section .text.same,"ax",@progbits
.balign 8
.globl zeta_fn
zeta_fn:
    .byte 0xb1,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8
.section .text.rank.z,"ax",@progbits
.balign 8
.globl rank_z
rank_z:
    .byte 0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8
ASM
done

for object in entry alpha zeta; do
    "$MINIAS" -march=rv64i -mabi=lp64 \
        -o "$work/$object.o" "$work/rv-$object.s"
    as -o "$work/host-$object.o" "$work/host-$object.s"
done

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
  .text : {
    *(.text.entry)
    *alpha.o(.text.same)
    *zeta.o(.text.same)
    *(SORT_BY_NAME(.text.rank.*))
  } :text
  .meta : { *(.meta.alpha) } :text
  .data : { *(.data.refs) } :text
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
    global: alpha_fn; zeta_fn; exported_refs;
    local: *;
  };
}
LDS
sed 's/OUTPUT_ARCH(riscv)/OUTPUT_ARCH(i386:x86-64)/' \
    "$work/vdso.lds" >"$work/vdso-host.lds"

cat >"$work/check.py" <<'PY'
import struct
import sys
from pathlib import Path

label, expected_machine, path = sys.argv[1:]
raw = Path(path).read_bytes()
machine = int(expected_machine)
if raw[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<HH", raw, 16) != (3, machine):
    raise SystemExit(f"{label}: invalid ET_DYN machine={machine}")
shoff = struct.unpack_from("<Q", raw, 40)[0]
shentsize, shnum, shstrndx = struct.unpack_from("<HHH", raw, 58)
if shentsize != 64 or shstrndx >= shnum or shoff + shnum * 64 > len(raw):
    raise SystemExit(f"{label}: invalid shdr table")
sh = [struct.unpack_from("<IIQQQQIIQQ", raw, shoff + i * 64)
      for i in range(shnum)]
names = raw[sh[shstrndx][4]:sh[shstrndx][4] + sh[shstrndx][5]]

def name_at(strings, offset):
    end = strings.find(b"\0", offset)
    if offset >= len(strings) or end == -1:
        raise SystemExit(f"{label}: invalid name offset")
    return strings[offset:end].decode("ascii")

by_name = {}
for index, section in enumerate(sh):
    by_name.setdefault(name_at(names, section[0]), []).append((index, section))

def unique(name, kind=None):
    matches = by_name.get(name, [])
    if len(matches) != 1 or (kind is not None and matches[0][1][1] != kind):
        raise SystemExit(f"{label}: expected unique {name}: {matches}")
    return matches[0]

def content(section):
    begin, size = section[4:6]
    if begin + size > len(raw):
        raise SystemExit(f"{label}: invalid section bounds")
    return raw[begin:begin + size]

text_i, text_sec = unique(".text", 1)
dynsym_i, dynsym_sec = unique(".dynsym", 11)
dynstr_i, dynstr_sec = unique(".dynstr", 3)
symtab_i, symtab_sec = unique(".symtab", 2)
_, strtab_sec = unique(".strtab", 3)
_, meta_sec = unique(".meta", 1)
data_i, data_sec = unique(".data", 1)
_, rela_sec = unique(".rela.dyn", 4)
_, dynamic_sec = unique(".dynamic", 6)
if (dynsym_sec[6] != dynstr_i or symtab_sec[6] != unique(".strtab")[0] or
        meta_sec[6] != text_i or rela_sec[6] != dynsym_i or
        dynamic_sec[6] != dynstr_i):
    raise SystemExit(f"{label}: ELF sh_link index stale")
if content(meta_sec) != b"\x44\x33\x22\x11":
    raise SystemExit(f"{label}: SHF_LINK_ORDER payload corrupt")

def symbols_in(symbol_section, string_section):
    items = {}
    symbols = content(symbol_section)
    strings = content(string_section)
    if len(symbols) % 24:
        raise SystemExit(f"{label}: truncated symbols")
    for off in range(0, len(symbols), 24):
        name, info, other, shndx, value, size = struct.unpack_from("<IBBHQQ", symbols, off)
        if name:
            items[name_at(strings, name)] = (shndx, value)
    return items

symbols = symbols_in(symtab_sec, strtab_sec)
dynamic_symbols = symbols_in(dynsym_sec, dynstr_sec)
text_bytes = content(text_sec)
sequence = ["_start", "alpha_fn", "zeta_fn", "rank_a", "rank_z"]
offsets = []
for name in sequence:
    if name not in symbols or symbols[name][0] != text_i:
        raise SystemExit(f"{label}: bad static symbol {name}")
    offset = symbols[name][1] - text_sec[3]
    if offset < 0 or offset + 8 > len(text_bytes):
        raise SystemExit(f"{label}: out of bounds {name}")
    offsets.append(offset)
if offsets != sorted(offsets):
    raise SystemExit(f"{label}: source-file/SORT order broken {list(zip(sequence, offsets))}")
for name, marker, offset in zip(sequence, (0xe1, 0xa1, 0xb1, 0xc1, 0xf1), offsets):
    if text_bytes[offset:offset + 8] != bytes(range(marker, marker + 8)):
        raise SystemExit(f"{label}: corrupted {name} source bytes")
for name, target in (("alpha_fn", text_i), ("zeta_fn", text_i),
                     ("exported_refs", data_i)):
    if name not in dynamic_symbols or dynamic_symbols[name][0] != target:
        raise SystemExit(f"{label}: bad dynsym symbol {name}")
if "outside_symbol" not in dynamic_symbols or dynamic_symbols["outside_symbol"][0] != 0:
    raise SystemExit(f"{label}: missing undefined DSO symbol")
if rela_sec[5] < 24 or rela_sec[5] % 24:
    raise SystemExit(f"{label}: missing shared input relocations")
phoff = struct.unpack_from("<Q", raw, 32)[0]
phentsize, phcount = struct.unpack_from("<HH", raw, 54)
if phentsize != 56 or phcount != 4:
    raise SystemExit(f"{label}: bad vDSO PHDR structure")
ph = [struct.unpack_from("<IIQQQQQQ", raw, phoff + i * 56)
      for i in range(phcount)]
if [p[0] for p in ph] != [1, 2, 4, 0x6474e550]:
    raise SystemExit(f"{label}: PHDR types mismatch")
print(f"MINILD_SHARED_INPUT_CHECK=PASS case={label} machine={machine}"
      " source_order=alpha/zeta reloc=1 link_order=1")
PY

ld -m elf_x86_64 -shared -T "$work/vdso-host.lds" \
    -o "$work/host.so" \
    "$work/host-entry.o" "$work/host-zeta.o" "$work/host-alpha.o"
python3 "$work/check.py" host 62 "$work/host.so"

if ! "$MINILD" -melf64lriscv -shared --hash-style=both \
    -soname libminild-shared.so -T "$work/vdso.lds" \
    -o "$work/native.so" "$work/entry.o" "$work/zeta.o" "$work/alpha.o" \
    >"$work/native.stdout" 2>"$work/native.stderr"; then
    echo "MINILD_SHARED_INPUT=BLOCKED mode=vdso" >&2
    sed -n '1,16p' "$work/native.stderr" >&2
    exit 1
fi
python3 "$work/check.py" native 243 "$work/native.so"

# Shared script selection now happens before dynamic symbol/relocation
# reservation. A discarded allocated input must contribute no output bytes,
# definition or dynamic relocation even when it references an undefined DSO
# symbol which otherwise would have required one.
cat >"$work/discard.s" <<'ASM'
.section .text.drop,"ax",@progbits
.globl discarded_fn
discarded_fn:
    .byte 0x91,0x92,0x93,0x94
.section .data.dead,"aw",@progbits
.balign 8
.globl discarded_refs
discarded_refs:
    .dword dead_only_external
    .dword 0xfeedfacedeadbeef
ASM
"$MINIAS" -march=rv64i -mabi=lp64 \
    -o "$work/discard.o" "$work/discard.s"
sed '/^  .dynamic :/a\  /DISCARD/ : { *discard.o(.text.drop) *discard.o(.data.dead) }' \
    "$work/vdso.lds" >"$work/vdso-discard.lds"
if ! "$MINILD" -melf64lriscv -shared --hash-style=both \
    -soname libminild-discard.so -T "$work/vdso-discard.lds" \
    -o "$work/discard.so" "$work/entry.o" "$work/zeta.o" \
    "$work/alpha.o" "$work/discard.o" \
    >"$work/discard.stdout" 2>"$work/discard.stderr"; then
    echo "MINILD_SHARED_INPUT=FAIL case=discard reason=unexpected-link-failure" >&2
    sed -n '1,12p' "$work/discard.stderr" >&2
    exit 1
fi
python3 "$work/check.py" discard 243 "$work/discard.so"
python3 - "$work/native.so" "$work/discard.so" <<'PY'
import struct
import sys
from pathlib import Path

def inspect(path):
    blob = Path(path).read_bytes()
    shoff = struct.unpack_from('<Q', blob, 40)[0]
    shnum, shstrndx = struct.unpack_from('<HH', blob, 60)
    sections = [struct.unpack_from('<IIQQQQIIQQ', blob, shoff + i * 64)
                for i in range(shnum)]
    shstr = sections[shstrndx]
    names = blob[shstr[4]:shstr[4] + shstr[5]]
    def name_at(pool, offset):
        end = pool.find(b'\0', offset)
        if offset >= len(pool) or end < 0:
            raise SystemExit('discard: invalid string table')
        return pool[offset:end].decode('ascii')
    named = {name_at(names, sec[0]): sec for sec in sections}
    def content(name):
        sec = named[name]
        return blob[sec[4]:sec[4] + sec[5]]
    def symbol_table(section_name):
        table = named[section_name]
        strings = blob[sections[table[6]][4]:
                       sections[table[6]][4] + sections[table[6]][5]]
        symbols = {}
        ordered = []
        for pos in range(table[4], table[4] + table[5], 24):
            name, info, other, shndx, value, size = struct.unpack_from(
                '<IBBHQQ', blob, pos)
            symbol_name = name_at(strings, name) if name else ''
            ordered.append(symbol_name)
            if symbol_name:
                symbols[symbol_name] = (shndx, value)
        return symbols, ordered
    return named, content, symbol_table('.symtab'), symbol_table('.dynsym')

base, base_content, _, _ = inspect(sys.argv[1])
discard, discarded_content, (symbols, _), (dynamic_symbols, dynsym_order) = inspect(sys.argv[2])
for name in ('.text.drop', '.data.dead'):
    if name in discard:
        raise SystemExit(f'discard: dead output section {name} leaked')
if b'\x91\x92\x93\x94' in discarded_content('.text'):
    raise SystemExit('discard: dead code bytes leaked')
if struct.pack('<Q', 0xfeedfacedeadbeef) in discarded_content('.data'):
    raise SystemExit('discard: dead data bytes leaked')
for name in ('discarded_fn', 'discarded_refs'):
    if name in symbols and symbols[name][0] not in (0, 0xfff1):
        raise SystemExit(f'discard: definition {name} still assigned to output')
    if name in dynamic_symbols:
        raise SystemExit(f'discard: dead export {name} leaked to .dynsym')
# GNU ld.bfd may retain an otherwise unused undefined name in .dynsym even
# after its sole input relocation is discarded. It must remain undefined and
# never have a dynamic relocation in the final image.
if ('dead_only_external' in dynamic_symbols and
        dynamic_symbols['dead_only_external'][0] != 0):
    raise SystemExit('discard: dead-only external became a definition')
if len(discarded_content('.rela.dyn')) != len(base_content('.rela.dyn')):
    raise SystemExit('discard: dead-only dynamic relocation leaked')
data = discard['.data']
referenced = []
for pos in range(0, len(discarded_content('.rela.dyn')), 24):
    offset, info, addend = struct.unpack_from('<QQq',
                                              discarded_content('.rela.dyn'), pos)
    if not data[3] <= offset < data[3] + data[5]:
        raise SystemExit('discard: dynamic relocation points outside live data')
    symbol_index = info >> 32
    if symbol_index >= len(dynsym_order):
        raise SystemExit('discard: dynamic relocation has invalid symbol index')
    referenced.append(dynsym_order[symbol_index])
if sorted(referenced) != ['alpha_fn', 'outside_symbol', 'zeta_fn']:
    raise SystemExit(f'discard: live relocations changed: {referenced}')
print('MINILD_SHARED_INPUT_CHECK=PASS case=discard bytes=absent'
      ' live_defs=absent dead_relocs=0')
PY

# .eh_frame is deliberately retained as a single work section in shared
# scripts, because the current eh_frame_hdr builder has one CFI input owner.
# Two source objects must therefore still contribute two FDE records to one
# non-empty .eh_frame_hdr when --eh-frame-hdr is requested.
for target in rv host; do
    if [[ $target == rv ]]; then
        cfa=2
    else
        cfa=7
    fi
    for who in a b; do
        cat >"$work/$target-eh-$who.s" <<ASM
.section .text.eh.$who,"ax",@progbits
.globl eh_$who
eh_$who:
.cfi_startproc
.cfi_def_cfa $cfa,16
    .byte 0x13,0x00,0x00,0x00
.cfi_endproc
ASM
        if [[ $target == rv ]]; then
            "$MINIAS" -march=rv64i -mabi=lp64 \
                -o "$work/eh-$who.o" "$work/$target-eh-$who.s"
        else
            as -o "$work/host-eh-$who.o" "$work/$target-eh-$who.s"
        fi
    done
done
# Deliberately reverse the object command order and the .eh_frame source
# order relative to .text.eh.  The binary-search table must be sorted by
# relocated PC, not by input-object or FDE storage order.
sed '/^  .meta :/i\  .text.eh : { *eh-a.o(.text.eh.a) *eh-b.o(.text.eh.b) } :text\n  .eh_frame : { *eh-b.o(.eh_frame) *eh-a.o(.eh_frame) } :text\n  .eh_frame_hdr : { *(.eh_frame_hdr) } :text :eh_frame_hdr' \
    "$work/vdso.lds" >"$work/vdso-eh.lds"
sed 's/OUTPUT_ARCH(riscv)/OUTPUT_ARCH(i386:x86-64)/' \
    "$work/vdso-eh.lds" >"$work/vdso-eh-host.lds"

ld -m elf_x86_64 -shared --eh-frame-hdr -T "$work/vdso-eh-host.lds" \
    -o "$work/eh-host.so" \
    "$work/host-entry.o" "$work/host-zeta.o" "$work/host-alpha.o" \
    "$work/host-eh-b.o" "$work/host-eh-a.o"
if ! "$MINILD" -melf64lriscv -shared --hash-style=both --eh-frame-hdr \
    -soname libminild-eh.so -T "$work/vdso-eh.lds" \
    -o "$work/eh-native.so" "$work/entry.o" "$work/zeta.o" \
    "$work/alpha.o" "$work/eh-b.o" "$work/eh-a.o" \
    >"$work/eh-native.stdout" 2>"$work/eh-native.stderr"; then
    echo "MINILD_SHARED_INPUT=BLOCKED case=eh-frame-hdr" >&2
    sed -n '1,16p' "$work/eh-native.stderr" >&2
    exit 1
fi
python3 - "$work/eh-host.so" "$work/eh-native.so" <<'PY'
import struct
import sys
from pathlib import Path

for case, path in zip(('host', 'native'), sys.argv[1:]):
    data = Path(path).read_bytes()
    shoff = struct.unpack_from('<Q', data, 40)[0]
    shnum, shstrndx = struct.unpack_from('<HH', data, 60)
    sh = [struct.unpack_from('<IIQQQQIIQQ', data, shoff + 64 * i)
          for i in range(shnum)]
    strs = data[sh[shstrndx][4]:sh[shstrndx][4] + sh[shstrndx][5]]
    def name_of(section):
        begin = section[0]
        end = strs.find(b'\0', begin)
        return strs[begin:end].decode('ascii')
    headers = [sec for sec in sh if name_of(sec) == '.eh_frame_hdr']
    frames = [sec for sec in sh if name_of(sec) == '.eh_frame']
    if len(headers) != 1 or len(frames) != 1:
        raise SystemExit(f'{case}: missing or duplicate EH sections')
    hdr = headers[0]
    frame = frames[0]
    payload = data[hdr[4]:hdr[4] + hdr[5]]
    frame_bytes = data[frame[4]:frame[4] + frame[5]]
    if len(payload) != 28 or payload[:4] != b'\x01\x1b\x03\x3b':
        raise SystemExit(f'{case}: EH header missing two FDE records')
    count = struct.unpack_from('<I', payload, 8)[0]
    if count != 2:
        raise SystemExit(f'{case}: EH FDE count {count}, expected 2')
    frame_pointer = hdr[3] + 4 + struct.unpack_from('<i', payload, 4)[0]
    if frame_pointer != frame[3]:
        raise SystemExit(f'{case}: EH header points to wrong frame section')

    symtabs = [sec for sec in sh if name_of(sec) == '.symtab']
    if len(symtabs) != 1:
        raise SystemExit(f'{case}: missing EH symbol table')
    symtab = symtabs[0]
    if symtab[6] >= len(sh) or symtab[5] % 24:
        raise SystemExit(f'{case}: invalid EH symbol table')
    symstr = sh[symtab[6]]
    symnames = data[symstr[4]:symstr[4] + symstr[5]]
    symbols = {}
    for pos in range(symtab[4], symtab[4] + symtab[5], 24):
        name, info, other, shndx, value, size = struct.unpack_from(
            '<IBBHQQ', data, pos)
        if name >= len(symnames):
            raise SystemExit(f'{case}: invalid symbol string offset')
        end = symnames.find(b'\0', name)
        if end < 0:
            raise SystemExit(f'{case}: unterminated symbol name')
        symbols[symnames[name:end].decode('ascii')] = (shndx, value)
    expected_pcs = [symbols[name][1] for name in ('eh_a', 'eh_b')]
    if expected_pcs != sorted(expected_pcs) or len(set(expected_pcs)) != 2:
        raise SystemExit(f'{case}: EH code inputs are not in scripted order')

    entries = []
    for i in range(count):
        pc_delta, fde_delta = struct.unpack_from('<ii', payload, 12 + i * 8)
        pc = hdr[3] + pc_delta
        fde = hdr[3] + fde_delta
        fde_offset = fde - frame[3]
        if fde_offset < 0 or fde_offset + 12 > len(frame_bytes):
            raise SystemExit(f'{case}: EH table points outside .eh_frame')
        record_size, cie_delta = struct.unpack_from('<II', frame_bytes,
                                                     fde_offset)
        if (record_size in (0, 0xffffffff) or record_size < 12 or
                fde_offset + 4 + record_size > len(frame_bytes)):
            raise SystemExit(f'{case}: EH table points at invalid FDE record')
        cie_offset = fde_offset + 4 - cie_delta
        if (cie_delta == 0 or cie_offset < 0 or
                cie_offset + 8 > len(frame_bytes) or
                struct.unpack_from('<I', frame_bytes, cie_offset + 4)[0] != 0):
            raise SystemExit(f'{case}: FDE lacks its own valid CIE')
        encoded_pc = (frame[3] + fde_offset + 8 +
                      struct.unpack_from('<i', frame_bytes, fde_offset + 8)[0])
        if pc != encoded_pc:
            raise SystemExit(f'{case}: EH FDE address disagrees with search PC')
        entries.append((pc, fde))
    if [pc for pc, fde in entries] != expected_pcs or len({fde for pc, fde in entries}) != 2:
        raise SystemExit(f'{case}: EH search table order/function mapping wrong')
    if entries[0][1] <= entries[1][1]:
        raise SystemExit(f'{case}: EH FDE storage did not follow reverse script order')
    print(f'MINILD_SHARED_INPUT_CHECK=PASS case={case}-eh fde={count}'
          ' table_pc=2 table_fde=2 cie=2 source_order=reverse')
PY
echo "MINILD_SHARED_INPUT=PASS cases=5 gnu_oracle=2"
