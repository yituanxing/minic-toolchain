#!/usr/bin/env bash
# A53-843419 target-owned post-layout, mapping, relocated-island and ET_DYN
# evidence.  Small deterministic AArch64 Linux-kernel-mode link, not vmlinux.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
for tool in python3 readelf; do command -v "$tool" >/dev/null; done

repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
source_fixture="$repo/tests/linker/fixtures/aarch64-cortex-a53-843419.S"
script_fixture="$repo/tests/linker/fixtures/aarch64-cortex-a53-843419.lds"
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-a53-843419.XXXXXX")
finish() {
    if [[ ${MINILD_AARCH64_ERRATUM_KEEP:-0} == 1 ]]; then
        echo "MINILD_AARCH64_ERRATUM_ARTIFACTS=$work"
    else
        rm -r -- "$work"
    fi
}
trap finish EXIT

"$MINIAS" --target=aarch64 -march=armv8-a -mabi=lp64 \
    -o "$work/erratum.o" "$source_fixture"
readelf -Ws "$work/erratum.o" >"$work/input.symbols"
grep -Fq '$x.' "$work/input.symbols"
grep -Fq '$d.' "$work/input.symbols"
readelf -Wr "$work/erratum.o" >"$work/input.relocs"
grep -Fq R_AARCH64_LDST64_ABS_LO12_NC "$work/input.relocs"

kernel_args=(-m aarch64linux -shared -Bsymbolic -X \
    --no-apply-dynamic-relocs --pic-veneer -z notext \
    -T "$script_fixture")
"$MINILD" "${kernel_args[@]}" -o "$work/original.elf" \
    "$work/erratum.o" >"$work/original.log" 2>&1
"$MINILD" "${kernel_args[@]}" --fix-cortex-a53-843419 \
    -o "$work/patched.elf" "$work/erratum.o" \
    >"$work/patched.log" 2>&1
"$MINILD" -m aarch64linux -static -X --fix-cortex-a53-843419 \
    -T "$script_fixture" -o "$work/patched-static.elf" \
    "$work/erratum.o" >"$work/patched-static.log" 2>&1

python3 - "$work/original.elf" "$work/patched.elf" \
    "$work/patched-static.elf" <<'PY'
from pathlib import Path
import struct
import sys


class Elf:
    def __init__(self, path, elf_type):
        self.raw = Path(path).read_bytes()
        assert self.raw[:6] == b'\x7fELF\x02\x01'
        assert struct.unpack_from('<HH', self.raw, 16) == (elf_type, 183)
        shoff = struct.unpack_from('<Q', self.raw, 40)[0]
        shentsize, shnum, shstrndx = struct.unpack_from('<HHH', self.raw, 58)
        assert shentsize == 64
        all_sections = [struct.unpack_from('<IIQQQQIIQQ', self.raw,
                        shoff + shentsize * i) for i in range(shnum)]
        shstr = all_sections[shstrndx]
        section_names = self.raw[shstr[4]:shstr[4] + shstr[5]]

        def lookup(pool, offset):
            return pool[offset:pool.index(0, offset)].decode()

        self.sections = {lookup(section_names, sh[0]): sh
                         for sh in all_sections}
        symtab = self.sections['.symtab']
        strtab = all_sections[symtab[6]]
        names = self.raw[strtab[4]:strtab[4] + strtab[5]]
        self.symbols = {}
        self.synthetic_x = []
        for off in range(symtab[4], symtab[4] + symtab[5], 24):
            name, _, _, _, value, _ = struct.unpack_from(
                '<IBBHQQ', self.raw, off)
            if name:
                symbol_name = lookup(names, name)
                self.symbols[symbol_name] = value
                if symbol_name == '$x':
                    self.synthetic_x.append(value)

    def insn(self, address):
        sh = self.sections['.text']
        assert sh[3] <= address < sh[3] + sh[5]
        return struct.unpack_from('<I', self.raw, sh[4] + address - sh[3])[0]


def branch_target(elf, address):
    insn = elf.insn(address)
    assert insn & 0xfc000000 == 0x14000000, hex(insn)
    delta = insn & 0x03ffffff
    if delta & (1 << 25):
        delta -= 1 << 26
    return address + 4 * delta


original, patched, patched_static = (
    Elf(sys.argv[1], 3), Elf(sys.argv[2], 3), Elf(sys.argv[3], 2))
assert patched.sections['.text'][5] == patched_static.sections['.text'][5]
text = patched.sections['.text']
static_text = patched_static.sections['.text']
assert (patched.raw[text[4]:text[4] + text[5]] ==
        patched_static.raw[static_text[4]:static_text[4] + static_text[5]])
for label in ('_start', 'cascade_sequence', 'optional_sequence',
              'page_last_sequence',
              'write_destination_sequence', 'unpatched_sequence',
              'embedded_data_sequence', 'fixture_value'):
    assert label in original.symbols and label in patched.symbols

site = patched.symbols['_start']
cascade = patched.symbols['cascade_sequence']
optional = patched.symbols['optional_sequence']
page_last = patched.symbols['page_last_sequence']
write_destination = patched.symbols['write_destination_sequence']
negative = patched.symbols['unpatched_sequence']
data = patched.symbols['embedded_data_sequence']
value = patched.symbols['fixture_value']
assert site & 0xfff == 0xff8 and optional & 0xfff == 0xff8
assert original.symbols['cascade_sequence'] & 0xfff == 0xff0
# BFD aligns the synthetic stub section to 8B. The first input .text ends
# at +0x100c, so its 4B alignment prefix moves the following would-be
# cascade to +0xff4. The test must retain the non-cascade assertion below.
assert cascade & 0xfff == 0xff4
assert page_last & 0xfff == 0xffc and write_destination & 0xfff == 0xff8
assert negative & 0xfff == 0xff4 and data & 0xfff == 0xff8
assert value & 0xfff == 24

# The positive three-instruction form needs one patch.  The relocated
# R_AARCH64_LDST64_ABS_LO12_NC must now apply to the *moved* load in the
# synthetic branch island, not to the branch overwriting its old address.
original_load = original.insn(original.symbols['_start'] + 8)
assert original_load == 0xf9400c03, hex(original_load)
stub = branch_target(patched, site + 8)
assert stub != site + 8 and stub != site + 12
assert patched.insn(stub) == original_load
assert branch_target(patched, stub + 4) == site + 12
assert stub in patched.synthetic_x, 'missing island $x mapping after $d'
assert stub % 8 == 0
assert patched.insn(stub - 4) == 0xd503201f
assert branch_target(patched, stub - 8) == stub - 8 + 4096

# GNU reserves a full 4 KiB erratum tail, so the next source section's
# ADRP stays at safe +0xff0; an 8B-only tail would spuriously patch it.
assert patched.insn(cascade + 8) == 0xf9400003

# The optional-middle-instruction form and last-word-in-page +0xffc form both
# need patches.  GNU also matches a second-instruction load that overwrites
# ADRP's register: pattern matching must follow the encoded instruction
# family, not presume a dataflow constraint.  +0xff4 and $d remain safe.
assert original.insn(original.symbols['optional_sequence'] + 12) == 0xf9400003
optional_stub = branch_target(patched, optional + 12)
assert patched.insn(optional_stub) == 0xf9400003
assert branch_target(patched, optional_stub + 4) == optional + 16
last_stub = branch_target(patched, page_last + 8)
assert patched.insn(last_stub) == 0xf9400003
assert branch_target(patched, last_stub + 4) == page_last + 12
overwrite_stub = branch_target(patched, write_destination + 8)
assert patched.insn(overwrite_stub) == 0xf9400003
assert branch_target(patched, overwrite_stub + 4) == write_destination + 12
assert patched.insn(negative + 8) == 0xf9400003
assert patched.insn(data + 8) == 0xf9400003
assert len({stub, overwrite_stub, optional_stub, last_stub}) == 4
assert original.insn(original.symbols['_start'] + 8) & 0xfc000000 != 0x14000000
print('MINILD_AARCH64_A53_843419=PASS kernel_et_dyn=1 static_exec=1 '
      'matched=4 page_stable_without_false_cascade=1 unmapped_data_preserved=1 '
      'writes_adrp_included=1 reloc_migrated=1 branch_roundtrip=4')
PY

# An allocated absolute pointer requires a runtime relocation under KASLR;
# --no-apply-dynamic-relocs reserves R_AARCH64_RELATIVE without altering its
# original input slot.  The erratum patch itself remains independent.
"$MINIAS" --target=aarch64 -march=armv8-a -mabi=lp64 \
    -o "$work/forbidden.o" \
    "$repo/tests/linker/fixtures/aarch64-cortex-a53-843419-abs64.S"
"$MINILD" "${kernel_args[@]}" --fix-cortex-a53-843419 \
    -o "$work/relative.elf" "$work/erratum.o" "$work/forbidden.o" \
    >"$work/relative.log" 2>&1
readelf -Wr "$work/relative.elf" >"$work/relative.relocs"
grep -Fq 'R_AARCH64_RELATIVE' "$work/relative.relocs"
python3 - "$work/relative.elf" <<'PY_REBASE'
import struct,sys
b=open(sys.argv[1],'rb').read()
off=struct.unpack_from('<Q',b,40)[0]
num,shstrings=struct.unpack_from('<HH',b,60)
sections=[struct.unpack_from('<IIQQQQIIQQ',b,off+i*64) for i in range(num)]
pool=b[sections[shstrings][4]:sections[shstrings][4]+sections[shstrings][5]]
name=lambda sec: pool[sec[0]:pool.index(0,sec[0])].decode()
s={name(sec):sec for sec in sections}
rela=s['.rela.dyn']
data=s['.data']
assert rela[5]==24
ro,info,addend=struct.unpack_from('<QQq',b,rela[4])
assert info&0xffffffff==1027 and info>>32==0
assert data[3]<=ro<=(data[3]+data[5]-8)
ptr=data[4]+ro-data[3]
assert b[ptr:ptr+8]==b'\0'*8
assert addend>=s['.text'][3]
print('MINILD_AARCH64_A53_843419=PASS relative_pointer=1 deferred_slot=1')
PY_REBASE

# The script-generated .pad is absent on trial #1 and appears only AFTER the
# target grows .text by a full 4 KiB page-stable erratum veneer. The output
# script scales the size difference to 8B to exercise new synthetic NOBITS
# section materialization without invalidating Cortex-A53 page phase.
"$MINIAS" --target=aarch64 -march=armv8-a -mabi=lp64 \
    -o "$work/pad.o" \
    "$repo/tests/linker/fixtures/aarch64-cortex-a53-843419-pad.S"
pad_script="$repo/tests/linker/fixtures/aarch64-cortex-a53-843419-pad.lds"
"$MINILD" -m aarch64linux -static -X -T "$pad_script" \
    -o "$work/pad-before.elf" "$work/pad.o" \
    >"$work/pad-before.log" 2>&1
"$MINILD" -m aarch64linux -static -X --fix-cortex-a53-843419 \
    -T "$pad_script" -o "$work/pad-after.elf" "$work/pad.o" \
    >"$work/pad-after.log" 2>&1
python3 - "$work/pad-before.elf" "$work/pad-after.elf" <<'PY'
from pathlib import Path
import struct
import sys


def sections(path):
    raw = Path(path).read_bytes()
    assert raw[:6] == b'\x7fELF\x02\x01'
    shoff = struct.unpack_from('<Q', raw, 40)[0]
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', raw, 58)
    headers = [struct.unpack_from('<IIQQQQIIQQ', raw, shoff + i * shentsize)
               for i in range(shnum)]
    strings = headers[shstrndx]
    names = raw[strings[4]:strings[4] + strings[5]]
    result = {}
    for hdr in headers:
        off = hdr[0]
        result[names[off:names.find(b'\0', off)].decode()] = hdr
    return raw, result


raw_before, before = sections(sys.argv[1])
raw_after, after = sections(sys.argv[2])
assert before['.text'][5] == 0x1010, before['.text']
assert '.pad' not in before or before['.pad'][5] == 0, before.get('.pad')
text = after['.text']
pad = after['.pad']
assert text[5] == 0x2010, text
assert pad[1] == 8 and pad[3] == text[3] + text[5], pad
assert pad[5] == 8 and pad[3] % 16 == 0, pad
assert pad[4] % 4096 == pad[3] % 4096, pad
site_offset = text[4] + 0x1000
branch = struct.unpack_from('<I', raw_after, site_offset)[0]
assert branch & 0xfc000000 == 0x14000000, hex(branch)
print('MINILD_AARCH64_A53_843419=PASS script_synthetic_nobits_cascade=1 '
      'new_section_after_first_round=1 page_stable_fixed_point=1')
PY
