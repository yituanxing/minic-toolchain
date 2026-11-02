#!/usr/bin/env bash
# Common Section records store semantic kind/flags; ELF numerics are projected
# exactly once at the terminal writer. Check the real writer independently of
# parser state, then compare GNU ELF section semantics and emitted bytes.
set -Eeuo pipefail
repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
minias=${MINIAS:-"$repo/build/release/bin/minic-as"}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

"${CC:-cc}" -std=c11 -Wall -Wextra -Wconversion -Wshadow -Werror \
    -I"$repo/assembler/src" -I"$repo/elf/include" \
    "$repo/tests/assembler/object-section-format-projection-probe.c" \
    "$repo/assembler/src/elf_writer.c" \
    "$repo/assembler/src/object_model.c" \
    "$repo/assembler/src/object_output.c" \
    "$repo/elf/src/reader.c" \
    "$repo/elf/src/relocatable_writer.c" \
    -o "$work/section-projection-probe"
"$work/section-projection-probe" "$work/section-kind"

python3 - "$repo" <<'PY'
from pathlib import Path
import re
import sys

root = Path(sys.argv[1]) / 'assembler/src'
api = (root / 'minias_target_api.h').read_text()
internal = (root / 'minias_internal.h').read_text()
writer = (root / 'elf_writer.c').read_text()
source = '\n'.join(p.read_text() for p in root.glob('*.[ch]'))

if re.search(r'\bMINIAS_(?:SHF|SHT|GRP|STB|STT|STV)_', source):
    raise SystemExit('MiniAS still exposes ELF numeric Section/Symbol metadata')
for name in ('PROGBITS', 'NOBITS', 'NOTE', 'INIT_ARRAY',
             'FINI_ARRAY', 'PREINIT_ARRAY'):
    token = 'MINIAS_SECTION_KIND_' + name
    if token not in api or token not in writer:
        raise SystemExit('Section kind not mapped in ELF writer: ' + name)
for name in ('ALLOC', 'WRITE', 'EXECINSTR', 'MERGE', 'STRINGS',
             'TLS', 'GROUP', 'LINK_ORDER', 'GNU_RETAIN'):
    token = 'MINIAS_SECTION_FLAG_' + name
    if token not in api or token not in writer:
        raise SystemExit('Section flag not mapped in ELF writer: ' + name)
for token in ('MINIAS_SYMBOL_BINDING_GNU_UNIQUE',
              'MINIAS_SYMBOL_KIND_GNU_IFUNC',
              'MINIAS_SYMBOL_KIND_TLS',
              'MINIAS_SYMBOL_VISIBILITY_HIDDEN'):
    if token not in api or token not in writer:
        raise SystemExit('Symbol semantics lack explicit ELF projection: ' + token)
if 'MiniAsSectionKind type;' not in internal or \
   'MiniAsSectionFlags flags;' not in internal or \
   'bool group_comdat;' not in internal or 'group_flags;' in internal:
    raise SystemExit('Context retains ELF wire metadata or duplicate group flags')
for token in ('MiniAsSymbolBinding bind;', 'MiniAsSymbolKind type;',
              'MiniAsSymbolVisibility visibility;'):
    if token not in internal:
        raise SystemExit('Context Symbol record retains ELF wire value: ' + token)
print('MINIAS_SECTION_SEMANTIC_OWNER=PASS kinds=6 flags=9 group=bool '\
      'ELF_numeric=writer_only')
print('MINIAS_SYMBOL_SEMANTIC_OWNER=PASS binding_kind_visibility=typed '\
      'ELF_numeric=writer_only')
PY

# Validate the numerically different Common enum/bitset at compile-time.
printf '%s\n' '
#include "minias_target_api.h"
_Static_assert(MINIAS_SECTION_KIND_PROGBITS == 0, "semantic kind starts at zero");
_Static_assert(MINIAS_SECTION_KIND_NOBITS != 8, "NOBITS is not SHT_NOBITS");
_Static_assert(MINIAS_SECTION_KIND_NOTE != 7, "NOTE is not SHT_NOTE");
_Static_assert(MINIAS_SECTION_FLAG_ALLOC != 2, "ALLOC is not SHF_ALLOC");
_Static_assert(MINIAS_SECTION_FLAG_WRITE != 1, "WRITE is not SHF_WRITE");
_Static_assert(MINIAS_SECTION_FLAG_GROUP != 512, "GROUP is not SHF_GROUP");
_Static_assert(MINIAS_SECTION_FLAG_GNU_RETAIN != (1ULL << 21), "RETAIN not ELF bit");
_Static_assert(MINIAS_SYMBOL_BINDING_GNU_UNIQUE != 10, "GNU_UNIQUE not STB_GNU_UNIQUE");
_Static_assert(MINIAS_SYMBOL_KIND_GNU_IFUNC != 10, "GNU_IFUNC not STT_GNU_IFUNC");
_Static_assert(MINIAS_SYMBOL_KIND_TLS != 6, "TLS not STT_TLS");
' | "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
    -I"$repo/assembler/src" -x c -fsyntax-only -

python3 - "$work/section-kind" <<'PY'
from pathlib import Path
import struct
import sys

prefix = sys.argv[1]
expected = ((10, 1, 0, 3), (1, 10, 0, 3),
            (1, 6, 0, 0), (1, 1, 2, 0))
for index, (binding, kind, visibility, osabi) in enumerate(expected):
    data = Path(f'{prefix}-symbol-{index}.o').read_bytes()
    assert data[:6] == b'\x7fELF\x02\x01'
    if data[7] != osabi:
        raise SystemExit(f'Symbol OSABI mismatch case={index} expected={osabi} actual={data[7]}')
    offset = struct.unpack_from('<Q', data, 40)[0]
    stride, count = struct.unpack_from('<HH', data, 58)
    headers = [struct.unpack_from('<IIQQQQIIQQ', data, offset + i * stride)
               for i in range(count)]
    symtabs = [entry for entry in headers if entry[1] == 2]
    if len(symtabs) != 1:
        raise SystemExit(f'Symbol table missing case={index}')
    symtab = symtabs[0]
    strings = headers[symtab[6]]
    strtab = data[strings[4]: strings[4]+strings[5]]
    selected = []
    for pos in range(symtab[4], symtab[4] + symtab[5], 24):
        name, info, other, shndx, value, size = struct.unpack_from('<IBBHQQ', data, pos)
        if strtab[name: strtab.find(b'\0', name)] == b'semantic_symbol':
            selected.append((info >> 4, info & 15, other & 3))
    if selected != [(binding, kind, visibility)]:
        raise SystemExit(f'ELF symbol projection mismatch case={index} selected={selected}')
print('MINIAS_SYMBOL_GNU_PROJECTION=PASS UNIQUE=10 IFUNC=10 TLS=6 HIDDEN=2 '
      'ELF_OSABI=checked invalid_enums=3')
PY

if ! command -v as >/dev/null || ! command -v readelf >/dev/null; then
    echo 'MINIAS_SECTION_GNU_PROJECTION=ERROR missing_host_gnu_tools' >&2
    exit 127
fi

cat >"$work/all-sections.s" <<'SRC'
.section .note.semantic,"",@note
    .long 0x01020304
.section .init_array.semantic,"aw",@init_array
    .quad 0x1122334455667788
.section .fini_array.semantic,"aw",@fini_array
    .quad 0x8877665544332211
.section .preinit_array.semantic,"aw",@preinit_array
    .quad 0x0102030405060708
.section .tbss.semantic,"awT",@nobits
    .zero 13
.section .rodata.merge,"aMS",@progbits,1
    .asciz "semantic-section"
.section .data.retain,"awR",@progbits
    .long 0x12345678
.section .text.owned,"ax",@progbits
.globl linked_target
linked_target:
    nop
.section .meta.linked,"ao",@progbits,linked_target
    .long 0x43211234
.section .text.shared,"axG",@progbits,semantic_signature,comdat
.globl semantic_signature
semantic_signature:
    nop
.section .rodata.shared,"aG",@progbits,semantic_signature,comdat
    .long semantic_signature
SRC

"$minias" --target=x86_64 -march=x86-64 -mabi=sysv \
    -o "$work/mini.o" "$work/all-sections.s"
as --64 -o "$work/gnu.o" "$work/all-sections.s"
readelf --section-groups "$work/mini.o" >"$work/mini.groups"
readelf --section-groups "$work/gnu.o" >"$work/gnu.groups"

python3 - "$work/gnu.o" "$work/mini.o" "$work/gnu.groups" "$work/mini.groups" <<'PY'
from pathlib import Path
import re
import struct
import sys

names = (
    '.note.semantic', '.init_array.semantic', '.fini_array.semantic',
    '.preinit_array.semantic', '.tbss.semantic', '.rodata.merge',
    '.data.retain', '.text.owned', '.meta.linked',
    '.text.shared', '.rodata.shared',
)

def sections(path):
    content = Path(path).read_bytes()
    assert content[:6] == b'\x7fELF\x02\x01', path
    offset = struct.unpack_from('<Q', content, 40)[0]
    stride, count, strings_index = struct.unpack_from('<HHH', content, 58)
    assert stride == 64, path
    entries = [struct.unpack_from('<IIQQQQIIQQ', content, offset + i * stride)
               for i in range(count)]
    strings_header = entries[strings_index]
    strings = content[strings_header[4]: strings_header[4]+strings_header[5]]
    result = {}
    for index, entry in enumerate(entries):
        name_offset, typ, flags, addr, data_offset, size, link, info, align, entsize = entry
        end = strings.find(b'\0', name_offset)
        name = strings[name_offset:end].decode()
        result.setdefault(name, []).append(dict(
            index=index, type=typ, flags=flags, align=align,
            entsize=entsize, size=size, link=link,
            data=None if typ == 8 else content[data_offset:data_offset+size]))
    by_index = {item['index']: name for name, values in result.items()
                for item in values}
    return result, by_index

reference, ref_index = sections(sys.argv[1])
candidate, cand_index = sections(sys.argv[2])
for name in names:
    gnu = reference.get(name, [])
    mini = candidate.get(name, [])
    if len(gnu) != 1 or len(mini) != 1:
        raise SystemExit(f'Section missing or duplicate: {name}: GNU={len(gnu)} MiniAS={len(mini)}')
    g, m = gnu[0], mini[0]
    for field in ('type', 'flags', 'align', 'entsize', 'size', 'data'):
        if g[field] != m[field]:
            raise SystemExit(f'Section GNU mismatch: {name} {field}: GNU={g[field]!r} MiniAS={m[field]!r}')
    if name == '.meta.linked':
        if ref_index[g['link']] != cand_index[m['link']] or \
           cand_index[m['link']] != '.text.owned':
            raise SystemExit('SHF_LINK_ORDER target section mismatch')

groups = []
for path in sys.argv[3:]:
    source = Path(path).read_text()
    if not re.search(r'COMDAT group section.*\[semantic_signature\]', source):
        raise SystemExit(f'missing COMDAT signature in {path}')
    members = set(re.findall(r'\[\s*\d+\]\s+(\.\S+)', source))
    groups.append(members & {'.text.shared', '.rodata.shared', '.rela.rodata.shared'})
if groups != [{'.text.shared', '.rodata.shared', '.rela.rodata.shared'}] * 2:
    raise SystemExit(f'COMDAT member mismatch: {groups}')
print('MINIAS_SECTION_GNU_PROJECTION=PASS named_sections=11 kind_flags_entsize_size_exact=1 '
      'payload_byte_exact=1 link_order=1 comdat_group=3')
PY
