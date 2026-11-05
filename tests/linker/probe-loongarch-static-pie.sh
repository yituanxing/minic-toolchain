#!/usr/bin/env bash
# GNU LoongArch -static -pie -n remains ET_EXEC, but its absolute data
# references need image-base relocations for kernel KASLR at boot.
set -Eeuo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
MINIAS=${MINIAS:-"$repo/build/release/bin/minic-as"}
MINILD=${MINILD:-"$repo/build/release/bin/minic-ld"}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

cat >"$work/source.s" <<'ASM'
.text
.globl _start
_start:
    jirl $r0,$r1,0
.data
.globl alpha
alpha:
    .quad _start
    .quad alpha+8
    .quad absolute_value
ASM
cat >"$work/absolute.s" <<'ASM'
.globl absolute_value
.set absolute_value, 0x12345678
ASM
cat >"$work/image.lds" <<'LDS'
OUTPUT_ARCH(loongarch)
ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(7); }
SECTIONS {
  . = 0x9000000000100000;
  .text : { *(.text) } :text
  .iplt : { *(.iplt) }
  .got : ALIGN(16) { *(.got) }
  .igot.plt : { *(.igot.plt) }
  .data : { *(.data) }
  .rela.dyn : ALIGN(8) {
    __rela_dyn_begin = .;
    *(.rela.dyn)
    __rela_dyn_end = .;
  }
}
LDS
"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/source.o" "$work/source.s"
"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/absolute.o" "$work/absolute.s"
"$MINILD" -m elf64loongarch -static -pie -n --no-dynamic-linker \
    --script="$work/image.lds" -o "$work/pie.elf" \
    "$work/source.o" "$work/absolute.o"
"$MINILD" -m elf64loongarch -static -n \
    --script="$work/image.lds" -o "$work/ordinary.elf" \
    "$work/source.o" "$work/absolute.o"

python3 - "$work/pie.elf" "$work/ordinary.elf" <<'PY'
from pathlib import Path
import struct
import sys

def extract(path):
    raw = Path(path).read_bytes()
    assert raw[:6] == b'\x7fELF\x02\x01'
    assert struct.unpack_from('<HH', raw, 16) == (2, 258)
    shoff = struct.unpack_from('<Q', raw, 40)[0]
    shnum, shstridx = struct.unpack_from('<HH', raw, 60)
    sections = [struct.unpack_from('<IIQQQQIIQQ', raw, shoff + n * 64)
                for n in range(shnum)]
    shstr = sections[shstridx]
    names = raw[shstr[4]:shstr[4] + shstr[5]]
    byname = {}
    for sec in sections:
        name = names[sec[0]:names.find(b'\0', sec[0])].decode()
        byname[name] = sec
    def secdata(name):
        sec = byname[name]
        return raw[sec[4]:sec[4] + sec[5]]
    return byname, secdata

pie, content = extract(sys.argv[1])
ordinary, old_content = extract(sys.argv[2])
assert pie['.got'][5] == 8 and pie['.got'][3] % 16 == 0
assert content('.got') == b'\0' * 8
assert pie['.rela.dyn'][1] == 4 and pie['.rela.dyn'][9] == 24
relocations = [struct.unpack_from('<QQQ', content('.rela.dyn'), i)
               for i in range(0, pie['.rela.dyn'][5], 24)]
assert len(relocations) == 2, relocations
data_va = pie['.data'][3]
text_va = pie['.text'][3]
assert relocations[:2] == [
    (data_va, 3, text_va),
    (data_va + 8, 3, data_va + 8),
], relocations
# GNU BFD excludes SHN_ABS constants from PIE dynamic relocation capacity;
# the absolute pointer is linked directly without a synthetic R_NONE slot.
assert struct.unpack_from('<3Q', content('.data')) == (
    text_va, data_va + 8, 0x12345678)
assert '.rela.dyn' not in ordinary or ordinary['.rela.dyn'][5] == 0
assert '.got' not in ordinary or ordinary['.got'][5] == 0
assert struct.unpack_from('<3Q', old_content('.data'))[2] == 0x12345678
print('MINILD_LOONGARCH_STATIC_PIE=PASS ET_EXEC=1 GOT=8 '
      'RELATIVE=2 NONE=0 absolute_unchanged=1 ordinary_nmagic_unaffected=1')
PY

cat >"$work/ifunc.s" <<'ASM'
.text
.globl test_resolver
.type test_resolver,@gnu_indirect_function
test_resolver:
    jirl $r0,$r1,0
.data
.globl resolver_pointer
resolver_pointer:
    .quad test_resolver
ASM
"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/ifunc.o" "$work/ifunc.s"
"$MINILD" -m elf64loongarch -static -pie -n --no-dynamic-linker \
    --script="$work/image.lds" -o "$work/mixed.elf" \
    "$work/source.o" "$work/absolute.o" "$work/ifunc.o"
python3 - "$work/mixed.elf" <<'PY'
from pathlib import Path
import struct
import sys

b = Path(sys.argv[1]).read_bytes()
shoff = struct.unpack_from('<Q', b, 40)[0]
shnum, shstridx = struct.unpack_from('<HH', b, 60)
sections = [struct.unpack_from('<IIQQQQIIQQ', b, shoff + i * 64)
            for i in range(shnum)]
pool = b[sections[shstridx][4]:sections[shstridx][4]+sections[shstridx][5]]
def name(off): return pool[off:pool.index(0, off)].decode()
byname = {name(section[0]): section for section in sections}
rela, got, stub = (byname[x] for x in ('.rela.dyn', '.igot.plt', '.iplt'))
assert got[5] == 8 and stub[5] == 16
relocs = [struct.unpack_from('<QQQ', b, rela[4] + i)
          for i in range(0, rela[5], 24)]
assert len(relocs) == 4, relocs
assert [entry[1] for entry in relocs] == [3, 3, 3, 12], relocs
assert relocs[2][2] == stub[3], ('IFUNC pointer addend is not IPLT', relocs)
assert relocs[3][:2] == (got[3], 12)
sym = byname['.symtab']
strtab = sections[sym[6]]
strings = b[strtab[4]:strtab[4]+strtab[5]]
values = {}
for i in range(sym[4], sym[4]+sym[5], 24):
    idx, _, _, _, addr, _ = struct.unpack_from('<IBBHQQ', b, i)
    if idx:
        symbol = strings[idx:strings.index(0, idx)].decode()
        values[symbol] = addr
assert values['__rela_iplt_start'] == rela[3] + 3 * 24
assert values['__rela_iplt_end'] == rela[3] + 4 * 24
print('MINILD_LOONGARCH_STATIC_PIE=PASS mixed_relative_ifunc=3+1 '
      'iplt_bounds_exclude_relative=1 ifunc_addend_uses_stub=1')
PY
