#!/usr/bin/env bash
# Symbol lifecycle contract across input globals, archive extraction, weak vs
# strong winners, COMMON allocation, script PROVIDE and final ELF symbols.
# Host-only structural test: MiniAS/MiniLD, ar and Python are sufficient.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
for tool in ar python3; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "MINILD_SYMBOL_OWNER=INCONCLUSIVE missing_tool=$tool" >&2
        exit 8
    }
done

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-symbol-owner.XXXXXX")
finish() {
    if [[ ${MINILD_SYMBOL_OWNER_KEEP:-0} == 1 ]]; then
        echo "MINILD_SYMBOL_OWNER_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap finish EXIT

cat > "$work/entry.s" <<'ASM'
.section .text.entry,"ax",@progbits
.balign 4
.globl _start
.type _start,@function
_start:
    call chosen
    call archive_fn
    lui t0,%hi(owner_refs)
    addi t0,t0,%lo(owner_refs)
    addi a0,zero,0
    addi a7,zero,93
    ecall
.size _start,.-_start

.section .data.refs,"aw",@progbits
.balign 8
.globl owner_refs
.type owner_refs,@object
owner_refs:
    .dword chosen
    .dword archive_fn
    .dword absent_weak
    .dword provided_tag
    .dword object_tag
    .dword shared_common
.size owner_refs,48

.weak absent_weak
.globl object_tag
.set object_tag,0x22334455
ASM

cat > "$work/weak.s" <<'ASM'
.section .text.weak,"ax",@progbits
.weak chosen
.type chosen,@function
chosen:
    addi a0,zero,9
    ret
.size chosen,.-chosen
ASM

cat > "$work/strong.s" <<'ASM'
.section .text.strong,"ax",@progbits
.globl chosen
.type chosen,@function
chosen:
    addi a0,zero,42
    ret
.size chosen,.-chosen
.comm shared_common,16,8
ASM

cat > "$work/archive.s" <<'ASM'
.section .text.archive,"ax",@progbits
.globl archive_fn
.type archive_fn,@function
archive_fn:
    addi a0,zero,13
    ret
.size archive_fn,.-archive_fn
ASM

cat > "$work/unused.s" <<'ASM'
.section .text.unused,"ax",@progbits
.globl never_extracted
.type never_extracted,@function
never_extracted:
    ret
.size never_extracted,.-never_extracted
ASM

cat > "$work/link.lds" <<'LDS'
OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text.entry) *(.text.weak) *(.text.strong) *(.text.archive) }
  .data : { *(.data.refs) }
  .bss : { *(.bss) *(COMMON) }
  PROVIDE(provided_tag = 0x44556677);
  PROVIDE(object_tag = 0xdeadbeef);
}
LDS

for input in entry weak strong archive unused; do
    "$MINIAS" -march=rv64i -mabi=lp64 -o "$work/$input.o" "$work/$input.s"
done
ar qc "$work/libchoice.a" "$work/archive.o" "$work/unused.o"

cat > "$work/check.py" <<'PY'
import struct
import sys
from pathlib import Path

case, path = sys.argv[1:]
blob = Path(path).read_bytes()
if blob[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<HH', blob, 16) != (2, 243):
    raise SystemExit(f'{case}: expected RV64 ET_EXEC')
shoff = struct.unpack_from('<Q', blob, 40)[0]
entsize, shnum, shstrndx = struct.unpack_from('<HHH', blob, 58)
if entsize != 64 or shnum <= 4 or shstrndx >= shnum or shoff + shnum * 64 > len(blob):
    raise SystemExit(f'{case}: malformed section table')
sections = [struct.unpack_from('<IIQQQQIIQQ', blob, shoff + 64 * i)
            for i in range(shnum)]

def data(sh):
    if sh[4] + sh[5] > len(blob):
        raise SystemExit(f'{case}: section payload outside image')
    return blob[sh[4]:sh[4] + sh[5]]

def cstr(pool, offset):
    if offset >= len(pool):
        raise SystemExit(f'{case}: string index outside pool')
    end = pool.find(b'\0', offset)
    if end < 0:
        raise SystemExit(f'{case}: unterminated string')
    return pool[offset:end].decode()

shstr = data(sections[shstrndx])
names = {cstr(shstr, sh[0]): (i, sh) for i, sh in enumerate(sections)}
for required in ('.text', '.data', '.bss', '.symtab', '.strtab'):
    if required not in names:
        raise SystemExit(f'{case}: missing {required}')
symtab = names['.symtab'][1]
strings = data(sections[symtab[6]])
symbytes = data(symtab)
if symtab[9] != 24 or len(symbytes) % 24:
    raise SystemExit(f'{case}: invalid symbol table')
symbols = {}
all_entries = []
for offset in range(0, len(symbytes), 24):
    nameoff, info, other, shndx, value, size = struct.unpack_from(
        '<IBBHQQ', symbytes, offset)
    name = cstr(strings, nameoff)
    all_entries.append((name, info, shndx, value, size))
    # RISC-V mapping symbols are local per input section and may share a
    # spelling; only user-facing global winner names must be unique.
    if name and not name.startswith('$'):
        if name in symbols:
            raise SystemExit(f'{case}: duplicated final symbol {name}')
        symbols[name] = (info, shndx, value, size)
first_global = next((i for i, (_, info, *_rest) in enumerate(all_entries)
                     if info >> 4 != 0), len(all_entries))
if first_global != symtab[7] or any(
        info >> 4 == 0 for _, info, *_rest in all_entries[first_global:]):
    raise SystemExit(f'{case}: local/global symbol partition changed')

def symbol(name):
    if name not in symbols:
        raise SystemExit(f'{case}: missing resolved symbol {name}')
    return symbols[name]

textidx, text = names['.text']
dataidx, dsec = names['.data']
bssidx, bss = names['.bss']
_ = dataidx
chosen = symbol('chosen')
archived = symbol('archive_fn')
common = symbol('shared_common')
if chosen[0] >> 4 != 1 or chosen[1] != textidx:
    raise SystemExit(f'{case}: strong symbol did not win')
if archived[1] != textidx or archived[0] >> 4 != 1:
    raise SystemExit(f'{case}: archive symbol unresolved')
for symbol_name, entry, expected_bytes in (
        ('chosen', chosen, bytes.fromhex('1305a002')),
        ('archive_fn', archived, bytes.fromhex('1305d000'))):
    byteoff = entry[2] - text[3]
    if byteoff < 0 or data(text)[byteoff:byteoff + 4] != expected_bytes:
        raise SystemExit(f'{case}: {symbol_name} winner points to wrong code')
if common[1] != bssidx or common[3] != 16 or common[2] % 8:
    raise SystemExit(f'{case}: COMMON winner size or alignment changed')
if symbol('provided_tag')[1:3] != (0xfff1, 0x44556677):
    raise SystemExit(f'{case}: script PROVIDE failed to create referenced symbol')
if symbol('object_tag')[1:3] != (0xfff1, 0x22334455):
    raise SystemExit(f'{case}: PROVIDE incorrectly overrode input ABS definition')
weak = symbol('absent_weak')
if weak[0] >> 4 != 2 or weak[1:3] != (0, 0):
    raise SystemExit(f'{case}: unresolved weak became defined')
if 'never_extracted' in symbols:
    raise SystemExit(f'{case}: unreferenced archive member was extracted')

refs = symbol('owner_refs')
if refs[1] != names['.data'][0]:
    raise SystemExit(f'{case}: refs section differs from output .data')
off = refs[2] - dsec[3]
if off < 0 or off + 48 > dsec[5]:
    raise SystemExit(f'{case}: refs symbol outside final .data')
pointers = struct.unpack_from('<6Q', data(dsec), off)
expected = (chosen[2], archived[2], 0, 0x44556677, 0x22334455, common[2])
if pointers != expected:
    raise SystemExit(f'{case}: relocations use stale winner/source symbol ids: '
                     f'{pointers} != {expected}')
print(f'MINILD_SYMBOL_OWNER_CHECK=PASS case={case} '
      'strong_over_weak=1 archive=1 common=16 provide=1 weak_undef=1')
PY

for case in normal gc; do
    flags=()
    if [[ $case == gc ]]; then
        flags+=(--gc-sections)
    fi
    "$MINILD" -melf64lriscv -static -T "$work/link.lds" "${flags[@]}" \
        -o "$work/$case.elf" "$work/entry.o" "$work/weak.o" \
        "$work/strong.o" "$work/libchoice.a"
    python3 "$work/check.py" "$case" "$work/$case.elf"
done

# Shared output must export the same winning global, not the earlier weak
# input, and preserve the resolver's symbol IDs in .dynsym independently of
# the full non-ALLOC .symtab view.
"$MINILD" -melf64lriscv -shared --hash-style=both \
    -soname libsymbol-owner.so -o "$work/shared.so" \
    "$work/entry.o" "$work/weak.o" "$work/strong.o" "$work/libchoice.a"
cat > "$work/check-shared.py" <<'PY'
import struct
import sys
from pathlib import Path

blob = Path(sys.argv[1]).read_bytes()
if blob[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<HH', blob, 16) != (3, 243):
    raise SystemExit('shared: expected RV64 ET_DYN')
shoff = struct.unpack_from('<Q', blob, 40)[0]
stride, count, shstr_index = struct.unpack_from('<HHH', blob, 58)
if stride != 64 or shstr_index >= count or shoff + count * 64 > len(blob):
    raise SystemExit('shared: invalid shdr table')
headers = [struct.unpack_from('<IIQQQQIIQQ', blob, shoff + 64 * i)
           for i in range(count)]

def contents(sh):
    if sh[4] + sh[5] > len(blob):
        raise SystemExit('shared: section outside file')
    return blob[sh[4]:sh[4] + sh[5]]

def name(pool, offset):
    end = pool.find(b'\0', offset)
    if offset >= len(pool) or end < 0:
        raise SystemExit('shared: invalid string')
    return pool[offset:end].decode()

shstr = contents(headers[shstr_index])
sections = {name(shstr, sh[0]): sh for sh in headers}

def symbols(table_name):
    sh = sections[table_name]
    strings = contents(headers[sh[6]])
    entries = contents(sh)
    if len(entries) % 24:
        raise SystemExit('shared: invalid symbol entry size')
    result = {}
    for offset in range(0, len(entries), 24):
        nameoff, info, other, index, value, size = struct.unpack_from(
            '<IBBHQQ', entries, offset)
        key = name(strings, nameoff)
        if key and not key.startswith('$') and info >> 4:
            if key in result:
                raise SystemExit(f'shared: duplicate global {key}')
            result[key] = (info, index, value, size)
    return result

symtab = symbols('.symtab')
dynsym = symbols('.dynsym')
for key in ('chosen', 'archive_fn', 'owner_refs', 'shared_common', 'object_tag'):
    if key not in symtab or key not in dynsym or symtab[key] != dynsym[key]:
        raise SystemExit(f'shared: inconsistent final/export symbol {key}')
if dynsym['chosen'][0] >> 4 != 1 or dynsym['chosen'][1] == 0:
    raise SystemExit('shared: weak candidate won over strong global')
if dynsym['shared_common'][3] != 16 or dynsym['shared_common'][1] == 0:
    raise SystemExit('shared: COMMON allocation lost')
if dynsym['object_tag'][1:3] != (0xfff1, 0x22334455):
    raise SystemExit('shared: input ABS changed')
for name_key in ('absent_weak', 'provided_tag'):
    if name_key not in dynsym or dynsym[name_key][1:3] != (0, 0):
        raise SystemExit(f'shared: undefined symbol {name_key} became defined')
if 'never_extracted' in symtab or 'never_extracted' in dynsym:
    raise SystemExit('shared: unreferenced archive member extracted')
print('MINILD_SYMBOL_OWNER_CHECK=PASS case=shared dynsym_winner=1 '
      'archive=1 common=16 weak_undef=1')
PY
python3 "$work/check-shared.py" "$work/shared.so"
echo 'MINILD_SYMBOL_OWNER=PASS modes=3'
