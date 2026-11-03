#!/usr/bin/env bash
# GNU ld.bfd is the oracle for source-qualified /DISCARD/ in ET_DYN links.
# The discarded input contributes neither bytes nor dynamic relocations,
# symbols, or dependencies, even when a same-named input stays live.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
for tool in python3 as ld sed grep; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "MINILD_SHARED_DISCARD=INCONCLUSIVE missing_tool=$tool" >&2
        exit 8
    fi
done

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-shared-discard.XXXXXX")
cleanup() {
    if [[ ${MINILD_SHARED_DISCARD_KEEP:-0} == 1 ]]; then
        echo "MINILD_SHARED_DISCARD_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap cleanup EXIT

for target in native host; do
    if [[ $target == native ]]; then
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
    $pointer alive_fn
    $pointer outside_symbol
    $pointer 0x8877665544332211
.section .note.minild,"a",@note
.balign 4
    .long 4,4,1
    .ascii "GNU\0"
    .long 0x11223344
ASM
    cat >"$work/$target-alive.s" <<'ASM'
.section .text.same,"ax",@progbits
.balign 8
.globl alive_fn
alive_fn:
    .byte 0xa1,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8
.section .data.alive,"aw",@progbits
.balign 8
.globl alive_data
alive_data:
    .quad 0x1122334455667788
.section .meta.alive,"ao",@progbits,alive_fn
    .long 0x10203040
ASM
    cat >"$work/$target-dead.s" <<'ASM'
.section .text.same,"ax",@progbits
.balign 8
.globl dead_fn
dead_fn:
    .byte 0xb1,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8
.section .data.dead,"aw",@progbits
.balign 8
.globl dead_refs
dead_refs:
    .quad only_dead_undefined
    .quad alive_fn
    .quad 0xfeedfacedeadbeef
.section .meta.dead,"ao",@progbits,dead_fn
    .long 0xc0d0e0f0
.section .text.retain,"axR",@progbits
.globl retained_dead
retained_dead:
    .byte 0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78
.section .text.comdat_dead,"axG",@progbits,comdat_mixed,comdat
.globl comdat_mixed
comdat_mixed:
    .byte 0xd1,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8
.section .rodata.comdat_live,"aG",@progbits,comdat_mixed,comdat
    .quad 0x1021324354657687
ASM
done

for item in entry dead alive; do
    "$MINIAS" -march=rv64i -mabi=lp64 \
        -o "$work/$item.o" "$work/native-$item.s"
    as -o "$work/host-$item.o" "$work/host-$item.s"
done

# A relocation emitted by a surviving input and targeting a definition in
# an explicitly discarded input is different from a relocation whose SOURCE
# is discarded. MiniLD must fail closed until that policy is fully defined.
for target in native host; do
    if [[ $target == native ]]; then pointer='.dword'; else pointer='.quad'; fi
    cat >"$work/$target-bad.s" <<ASM
.section .data.live_bad,"aw",@progbits
.balign 8
.globl bad_live_ref
bad_live_ref:
    $pointer dead_fn
ASM
done
"$MINIAS" -march=rv64i -mabi=lp64 \
    -o "$work/bad.o" "$work/native-bad.s"
as -o "$work/host-bad.o" "$work/host-bad.s"

cat >"$work/discard.lds" <<'LDS'
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
    *alive.o(.text.same)
  } :text
  .meta : { *(.meta.alive) *(.meta.dead) } :text
  .rodata : { *(.rodata.comdat_live) } :text
  .data : {
    *(.data.refs)
    *(.data.alive)
  } :text
  .dynamic : { *(.dynamic) } :text :dynamic
  /DISCARD/ : {
    *dead.o(.text.same)
    *dead.o(.data.dead)
    *(.text.retain)
    *(.text.comdat_dead)
  }
}
PHDRS {
  text PT_LOAD FLAGS(5) FILEHDR PHDRS;
  dynamic PT_DYNAMIC FLAGS(4);
  note PT_NOTE FLAGS(4);
  eh_frame_hdr PT_GNU_EH_FRAME;
}
VERSION {
  LINUX_4.15 {
    global: alive_fn; exported_refs; dead_fn; dead_refs; retained_dead;
    local: *;
  };
}
LDS
sed 's/OUTPUT_ARCH(riscv)/OUTPUT_ARCH(i386:x86-64)/' \
    "$work/discard.lds" >"$work/discard-host.lds"
sed 's/\*(\.data\.alive)/\*(.data.alive) *(.data.live_bad)/' \
    "$work/discard.lds" >"$work/bad.lds"
sed 's/OUTPUT_ARCH(riscv)/OUTPUT_ARCH(i386:x86-64)/' \
    "$work/bad.lds" >"$work/bad-host.lds"

cat >"$work/check.py" <<'PY'
import struct
import sys
from pathlib import Path

label, machine_text, filename = sys.argv[1:]
machine = int(machine_text)
raw = Path(filename).read_bytes()
if raw[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<HH', raw, 16) != (3, machine):
    raise SystemExit(f'{label}: wrong ET_DYN/ELF64 machine')
shoff = struct.unpack_from('<Q', raw, 40)[0]
shentsize, shnum, shstrndx = struct.unpack_from('<HHH', raw, 58)
if shentsize != 64 or shstrndx >= shnum or shoff + 64 * shnum > len(raw):
    raise SystemExit(f'{label}: malformed section table')
sections = [struct.unpack_from('<IIQQQQIIQQ', raw, shoff + 64 * i)
            for i in range(shnum)]
strsec = sections[shstrndx]
names = raw[strsec[4]:strsec[4] + strsec[5]]

def cstring(pool, offset):
    if offset >= len(pool):
        raise SystemExit(f'{label}: out-of-bounds ELF string')
    end = pool.find(b'\0', offset)
    if end == -1:
        raise SystemExit(f'{label}: unterminated ELF string')
    return pool[offset:end].decode('ascii')

by_name = {}
for i, sh in enumerate(sections):
    by_name.setdefault(cstring(names, sh[0]), []).append((i, sh))

def get_one(name, ty=None):
    results = by_name.get(name, [])
    if len(results) != 1 or (ty is not None and results[0][1][1] != ty):
        raise SystemExit(f'{label}: expected one {name} of type {ty}')
    return results[0]

def payload(sh):
    begin, length = sh[4:6]
    if begin + length > len(raw):
        raise SystemExit(f'{label}: invalid section bounds')
    return raw[begin:begin + length]

text_i, text = get_one('.text', 1)
data_i, data = get_one('.data', 1)
_, meta = get_one('.meta', 1)
_, rodata = get_one('.rodata', 1)
dynsym_i, dynsym = get_one('.dynsym', 11)
dynstr_i, dynstr = get_one('.dynstr', 3)
_, symtab = get_one('.symtab', 2)
strtab_i, strtab = get_one('.strtab', 3)
_, rela = get_one('.rela.dyn', 4)
_, dynamic = get_one('.dynamic', 6)
if (dynsym[6] != dynstr_i or symtab[6] != strtab_i or
        rela[6] != dynsym_i or dynamic[6] != dynstr_i or
        meta[6] != text_i):
    raise SystemExit(f'{label}: stale sh_link')
if payload(meta) != struct.pack('<I', 0x10203040):
    raise SystemExit(f'{label}: discarded-target SHF_LINK_ORDER metadata retained')
if payload(rodata) != struct.pack('<Q', 0x1021324354657687):
    raise SystemExit(f'{label}: live COMDAT peer discarded with member target')

def parse_symbols(sh, string_sh):
    records = payload(sh)
    pool = payload(string_sh)
    if len(records) % 24:
        raise SystemExit(f'{label}: broken ELF symbols')
    found = {}
    ordered = []
    for position in range(0, len(records), 24):
        name, info, other, shndx, value, size = struct.unpack_from('<IBBHQQ', records, position)
        symbol = cstring(pool, name) if name else ''
        ordered.append(symbol)
        if symbol:
            found[symbol] = (shndx, value)
    return found, ordered

symbols, _ = parse_symbols(symtab, strtab)
exports, export_order = parse_symbols(dynsym, dynstr)
for symbol, owner in [('alive_fn', text_i), ('exported_refs', data_i)]:
    if symbol not in symbols or symbols[symbol][0] != owner:
        raise SystemExit(f'{label}: static symbol {symbol} lost section')
    if symbol not in exports or exports[symbol][0] != owner:
        raise SystemExit(f'{label}: dynamic export {symbol} lost section')
for discarded in ['dead_fn', 'dead_refs', 'retained_dead']:
    # GNU ld.bfd turns version-script-exported symbols whose defining section
    # was discarded into ABS zero symbols, rather than removing all dynsym
    # names.  They must never point to retained .text/.data or nonzero VMA.
    if discarded in exports and exports[discarded] not in [(0xfff1, 0), (0, 0)]:
        raise SystemExit(f'{label}: discarded export {discarded} points into live memory')
    if discarded in symbols and symbols[discarded][0] not in (0, 0xfff1):
        raise SystemExit(f'{label}: discarded symbol {discarded} remains defined')
# Similarly, GNU can retain the undefined name from a discarded source in
# dynsym, provided no dynamic relocation still references it.
if ('only_dead_undefined' in exports and
        exports['only_dead_undefined'][0] != 0):
    raise SystemExit(f'{label}: dead-only undefined name became defined')
if ('outside_symbol' not in exports or exports['outside_symbol'][0] != 0):
    raise SystemExit(f'{label}: retained undefined DSO symbol missing')

code = payload(text)
if (b'\xa1\xa2\xa3\xa4\xa5\xa6\xa7\xa8' not in code or
        b'\xb1\xb2\xb3\xb4\xb5\xb6\xb7\xb8' in code or
        b'\x71\x72\x73\x74\x75\x76\x77\x78' in code or
        b'\xd1\xd2\xd3\xd4\xd5\xd6\xd7\xd8' in code):
    raise SystemExit(f'{label}: live/dead code bytes incorrectly selected')
for fn, marker in [('_start', 0xe1), ('alive_fn', 0xa1)]:
    off = symbols[fn][1] - text[3]
    if not 0 <= off < len(code) or code[off] != marker:
        raise SystemExit(f'{label}: invalid {fn} source offset')

data_bytes = payload(data)
if (len(data_bytes) != 32 or
        data_bytes[16:24] != struct.pack('<Q', 0x8877665544332211) or
        data_bytes[24:32] != struct.pack('<Q', 0x1122334455667788)):
    raise SystemExit(f'{label}: live/dead data payload incorrect')

records = payload(rela)
if len(records) != 48:
    raise SystemExit(f'{label}: expected exactly two dynamic relocations, got {len(records)//24}')
referenced = []
for offset in range(0, len(records), 24):
    reloc_offset, info, addend = struct.unpack_from('<QQq', records, offset)
    symbol_index = info >> 32
    if not data[3] <= reloc_offset < data[3] + 16:
        raise SystemExit(f'{label}: dynamic relocation to dead/outside data')
    if symbol_index >= len(export_order):
        raise SystemExit(f'{label}: stale dynamic relocation symbol index')
    referenced.append(export_order[symbol_index])
if sorted(referenced) != ['alive_fn', 'outside_symbol']:
    raise SystemExit(f'{label}: dynamic relocation references {referenced}')
print(f'MINILD_SHARED_DISCARD_CHECK=PASS case={label} machine={machine} '
      'live=1 discard=1 dyn_relocs=2 dead_relocs=0 link_order=1 retain_overridden=1 comdat_peer=1')
PY

ld -m elf_x86_64 -shared --hash-style=both -T "$work/discard-host.lds" \
    -o "$work/host.so" "$work/host-entry.o" "$work/host-dead.o" \
    "$work/host-alive.o"
python3 "$work/check.py" host 62 "$work/host.so"

if ! "$MINILD" -melf64lriscv -shared --hash-style=both \
    -soname libminild-shared.so -T "$work/discard.lds" \
    -o "$work/native.so" "$work/entry.o" "$work/dead.o" "$work/alive.o" \
    >"$work/native.stdout" 2>"$work/native.stderr"; then
    echo 'MINILD_SHARED_DISCARD=RED mode=native' >&2
    sed -n '1,12p' "$work/native.stderr" >&2
    exit 1
fi
python3 "$work/check.py" native 243 "$work/native.so"

if ld -m elf_x86_64 -shared --hash-style=both -T "$work/bad-host.lds" \
    -o "$work/host-bad.so" "$work/host-entry.o" "$work/host-dead.o" \
    "$work/host-alive.o" "$work/host-bad.o" \
    >"$work/host-bad.stdout" 2>"$work/host-bad.stderr"; then
    echo 'MINILD_SHARED_DISCARD=FAIL gnu-linked-reloc-to-discarded-symbol' >&2
    exit 1
fi
if ! grep -q 'defined in discarded section' "$work/host-bad.stderr"; then
    echo 'MINILD_SHARED_DISCARD=FAIL missing-gnu-discarded-symbol-oracle' >&2
    sed -n '1,14p' "$work/host-bad.stderr" >&2
    exit 1
fi
if "$MINILD" -melf64lriscv -shared --hash-style=both \
    -soname libminild-shared.so -T "$work/bad.lds" \
    -o "$work/native-bad.so" "$work/entry.o" "$work/dead.o" \
    "$work/alive.o" "$work/bad.o" \
    >"$work/native-bad.stdout" 2>"$work/native-bad.stderr"; then
    echo 'MINILD_SHARED_DISCARD=FAIL live-reference-to-discarded-definition-linked' >&2
    exit 1
fi
if ! grep -q 'shared-relocation-to-discarded-symbol:dead_fn' \
    "$work/native-bad.stderr"; then
    echo 'MINILD_SHARED_DISCARD=FAIL missing-discarded-symbol-diagnostic' >&2
    sed -n '1,14p' "$work/native-bad.stderr" >&2
    exit 1
fi
echo 'MINILD_SHARED_DISCARD_CHECK=PASS case=bad-live-reference rejection=explicit gnu=rejected'
echo 'MINILD_SHARED_DISCARD=PASS cases=3 gnu_oracle=2'
