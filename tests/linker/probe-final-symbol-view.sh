#!/usr/bin/env bash
# Final-symbol ownership contract for both ET_EXEC and ET_DYN.  This is a
# structural ELF64 test: it needs MiniAS/MiniLD and host Python, not a RISC-V
# GNU toolchain or emulator.  It must outlive symbol-writer refactoring.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
if ! command -v python3 >/dev/null 2>&1; then
    echo 'MINILD_FINAL_SYMBOL=INCONCLUSIVE missing_tool=python3' >&2
    exit 8
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-final-symbol.XXXXXX")
finish() {
    if [[ ${MINILD_FINAL_SYMBOL_KEEP:-0} == 1 ]]; then
        echo "MINILD_FINAL_SYMBOL_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap finish EXIT

cat >"$work/one.s" <<'ASM'
.section .text.core,"ax",@progbits
.balign 8
.globl canonical_fn
.type canonical_fn,@function
canonical_fn:
    addi a0,zero,19
    ret
.size canonical_fn,.-canonical_fn
.local local_fn
.type local_fn,@function
local_fn:
    ret
.size local_fn,.-local_fn

.section .data.core,"aw",@progbits
.balign 8
.local local_data
.type local_data,@object
local_data:
    .dword 0x1122334455667788
.size local_data,8
.globl exported_data
.type exported_data,@object
exported_data:
    .dword canonical_fn
    .dword weak_missing
.size exported_data,16

.section .tdata.slot,"awT",@progbits
.balign 8
.globl tls_slot
.type tls_slot,@tls_object
tls_slot:
    .dword 0x7867564534231201
.size tls_slot,8

.section .meta.info,"",@progbits
.local local_nonalloc
.type local_nonalloc,@object
local_nonalloc:
    .long 0x1234abcd
.size local_nonalloc,4

.globl absolute_tag
.set absolute_tag,0x12345678
.weak weak_missing
ASM

cat >"$work/static.lds" <<'LDS'
OUTPUT_ARCH(riscv)
ENTRY(canonical_fn)
SECTIONS {
  . = 0x10000;
  .text : { *(.text.core) }
  . = ALIGN(0x1000);
  .data : { *(.data.core) }
  .tdata : { *(.tdata.slot) }
  .meta : { *(.meta.info) }
}
LDS

"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/one.o" "$work/one.s"
if ! "$MINILD" -melf64lriscv -T "$work/static.lds" \
    -o "$work/static.elf" "$work/one.o" \
    >"$work/static.stdout" 2>"$work/static.stderr"; then
    echo 'MINILD_FINAL_SYMBOL=BLOCKED mode=static-link' >&2
    sed -n '1,16p' "$work/static.stderr" >&2
    exit 1
fi
if ! "$MINILD" -melf64lriscv -shared --hash-style=both \
    -soname libfinal-symbol-view.so -o "$work/shared.so" "$work/one.o" \
    >"$work/shared.stdout" 2>"$work/shared.stderr"; then
    echo 'MINILD_FINAL_SYMBOL=BLOCKED mode=shared-link' >&2
    sed -n '1,16p' "$work/shared.stderr" >&2
    exit 1
fi

cat >"$work/check.py" <<'PY'
import struct
import sys
from pathlib import Path

mode, source = sys.argv[1:]
blob = Path(source).read_bytes()
etype = 2 if mode == 'static' else 3
if (blob[:6] != b'\x7fELF\x02\x01' or
        struct.unpack_from('<HH', blob, 16) != (etype, 243)):
    raise SystemExit(f'{mode}: wrong ET_EXEC/ET_DYN machine')
shoff = struct.unpack_from('<Q', blob, 40)[0]
entsize, count, shstr_index = struct.unpack_from('<HHH', blob, 58)
if (entsize != 64 or shstr_index >= count or
        shoff + count * entsize > len(blob)):
    raise SystemExit(f'{mode}: malformed ELF section table')
headers = [struct.unpack_from('<IIQQQQIIQQ', blob, shoff + 64 * i)
           for i in range(count)]

def contents(section):
    if section[4] + section[5] > len(blob):
        raise SystemExit(f'{mode}: invalid section payload')
    return blob[section[4]:section[4] + section[5]]

def cstr(pool, index):
    if index >= len(pool):
        raise SystemExit(f'{mode}: invalid string index')
    end = pool.find(b'\0', index)
    if end == -1:
        raise SystemExit(f'{mode}: unterminated string')
    return pool[index:end].decode('ascii')

section_names = contents(headers[shstr_index])
names = {}
for index, section in enumerate(headers):
    names.setdefault(cstr(section_names, section[0]), []).append((index, section))

def get(name, kind):
    sections = names.get(name, [])
    if len(sections) != 1 or sections[0][1][1] != kind:
        raise SystemExit(f'{mode}: expected single {name} of kind {kind}')
    return sections[0]

symtab_id, symtab = get('.symtab', 2)
strtab_id, strtab = get('.strtab', 3)
if symtab[6] != strtab_id or symtab[9] != 24 or symtab[7] == 0:
    raise SystemExit(f'{mode}: symtab link/entsize/sh_info invalid')

def parse_symbols(table, strings):
    data = contents(table)
    pool = contents(strings)
    if len(data) % 24:
        raise SystemExit(f'{mode}: incomplete symbol entries')
    result = {}
    ordered = []
    for pos in range(0, len(data), 24):
        name, info, other, shndx, value, size = struct.unpack_from(
            '<IBBHQQ', data, pos)
        key = cstr(pool, name) if name else ''
        ordered.append((key, info >> 4, info & 15, other, shndx, value, size))
        if key:
            if key in result:
                raise SystemExit(f'{mode}: duplicate symbol {key}')
            result[key] = ordered[-1]
    return result, ordered

symbols, order = parse_symbols(symtab, strtab)
if not order or order[0] != ('', 0, 0, 0, 0, 0, 0):
    raise SystemExit(f'{mode}: missing initial STN_UNDEF')
first_global = next((i for i, sym in enumerate(order) if sym[1] != 0), len(order))
if first_global != symtab[7] or any(sym[1] == 0 for sym in order[first_global:]):
    raise SystemExit(f'{mode}: sh_info/local-before-global mismatch')

text_name, data_name, tls_name, meta_name = (
    ('.text', '.data', '.tdata', '.meta') if mode == 'static' else
    ('.text.core', '.data.core', '.tdata.slot', '.meta.info'))
text_id, text = get(text_name, 1)
data_id, data = get(data_name, 1)
tls_id, tls = get(tls_name, 1)
meta_id, meta = get(meta_name, 1)
if meta[2] & 2 or not (tls[2] & 0x400 and tls[2] & 2):
    raise SystemExit(f'{mode}: alloc/nonalloc/TLS attributes changed')

expected = [('canonical_fn', text_id, 1, 2),
            ('local_fn', text_id, 0, 2),
            ('local_data', data_id, 0, 1),
            ('exported_data', data_id, 1, 1),
            ('local_nonalloc', meta_id, 0, 1),
            ('tls_slot', tls_id, 1, 6)]
for name, index, binding, kind in expected:
    sym = symbols.get(name)
    if sym is None or (sym[1], sym[2], sym[4]) != (binding, kind, index):
        raise SystemExit(f'{mode}: {name} binding/type/shndx incorrect: {sym}')

for name, section in [('canonical_fn', text), ('local_fn', text),
                      ('local_data', data), ('exported_data', data)]:
    value = symbols[name][5]
    if not section[3] <= value < section[3] + section[5]:
        raise SystemExit(f'{mode}: {name} final address is outside its section')
if symbols['local_nonalloc'][5] != 0:
    raise SystemExit(f'{mode}: nonalloc symbol has absolute address')
if symbols['absolute_tag'][4:6] != (0xfff1, 0x12345678):
    raise SystemExit(f'{mode}: ABS symbol encoding changed')
if symbols['weak_missing'][1] != 2 or symbols['weak_missing'][4:6] != (0, 0):
    raise SystemExit(f'{mode}: unresolved weak symbol must remain SHN_UNDEF')

data_base = symbols['exported_data'][5] - data[3]
if data_base < 0 or data_base + 16 > data[5]:
    raise SystemExit(f'{mode}: exported_data out of live data range')
if mode == 'static':
    pointer, weak_pointer = struct.unpack_from('<QQ', contents(data), data_base)
    if pointer != symbols['canonical_fn'][5] or weak_pointer != 0:
        raise SystemExit('static: final pointer fixup disagrees with symbol value')

# An STT_TLS st_value is an offset in the TLS block, not an executable VMA.
# Test both modes so their static symbol tables share the same contract.
if symbols['tls_slot'][5] != 0:
    raise SystemExit(f'{mode}: TLS st_value must be TLS-block-relative')

if mode == 'shared':
    dynsym_id, dynsym = get('.dynsym', 11)
    dynstr_id, dynstr = get('.dynstr', 3)
    if dynsym[6] != dynstr_id:
        raise SystemExit('shared: dynamic string owner mismatch')
    exports, export_order = parse_symbols(dynsym, dynstr)
    for name in ('canonical_fn', 'exported_data', 'tls_slot', 'absolute_tag'):
        if name not in exports or exports[name][4:6] != symbols[name][4:6]:
            raise SystemExit(f'shared: dynsym/symtab disagree on {name}')
    if 'local_fn' in exports or 'local_data' in exports or 'local_nonalloc' in exports:
        raise SystemExit('shared: local symbol exported in dynsym')
    if exports['weak_missing'][4:6] != (0, 0):
        raise SystemExit('shared: weak undefined symbol became defined')

    rela_id, rela = get('.rela.dyn', 4)
    if rela[6] != dynsym_id or rela[9] != 24:
        raise SystemExit('shared: relocation symbol owner mismatch')
    relocations = contents(rela)
    if len(relocations) % 24:
        raise SystemExit('shared: corrupted dynamic relocation entries')
    if len(relocations) != 48:
        raise SystemExit('shared: expected exactly two input pointer relocations')
    references = []
    for offset in range(0, len(relocations), 24):
        addr, info, addend = struct.unpack_from('<QQq', relocations, offset)
        if addr not in (symbols['exported_data'][5],
                        symbols['exported_data'][5] + 8):
            raise SystemExit('shared: relocation points outside expected pointer slots')
        if info >> 32 >= len(export_order):
            raise SystemExit('shared: relocation references stale symbol ID')
        references.append(export_order[info >> 32][0])
    if sorted(references) != ['canonical_fn', 'weak_missing']:
        raise SystemExit(f'shared: unexpected relocation owners: {references}')

print(f'MINILD_FINAL_SYMBOL_CHECK=PASS mode={mode} '
      f'locals={symtab[7]} symbols={len(order)} tls_offset=0 '
      f'nonalloc={meta_id} relocs_checked={mode == "shared"}')
PY

python3 "$work/check.py" shared "$work/shared.so"
python3 "$work/check.py" static "$work/static.elf"
echo 'MINILD_FINAL_SYMBOL=PASS modes=2'
