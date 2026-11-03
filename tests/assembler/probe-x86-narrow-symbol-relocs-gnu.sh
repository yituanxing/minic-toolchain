#!/usr/bin/env bash
# GNU ELF32 REL and ELF64 RELA oracle for legitimate narrow symbol fixups.
# x86 has no ADD/SUB relocation pairs: PC8/PC16 is one S+A-P relocation.
set -Eeuo pipefail
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
: "${MINIAS:=$repo/build/release/bin/minic-as}"
for program in as ld readelf objcopy cmp python3; do
    command -v "$program" >/dev/null || {
        echo "MINIAS_X86_NARROW_RELOC=SKIP missing=$program" >&2
        exit 0
    }
done
test -x "$MINIAS"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
input="$repo/tests/assembler/fixtures/x86-narrow-symbol-relocs.S"
definitions="$repo/tests/assembler/fixtures/x86-narrow-symbol-defs.S"
as --64 -o "$work/amd64.defs.o" "$definitions"

# The same legal AMD64 relocation family must also be addressable by the
# explicit GNU `.reloc` names. Never accept AMD64 spellings in ELF32 i386.
explicit="$repo/tests/assembler/fixtures/x86-narrow-explicit-relocs.S"
as --64 -o "$work/explicit.gnu.o" "$explicit"
"$MINIAS" --target=x86_64 -march=x86-64 -mabi=sysv \
    -o "$work/explicit.mini.o" "$explicit"
for producer in gnu mini; do
    readelf -Wr "$work/explicit.$producer.o" >"$work/explicit.$producer.reloc"
    objcopy -j .text -O binary "$work/explicit.$producer.o" \
        "$work/explicit.$producer.text"
    ld -m elf_x86_64 -n --section-start=.text=0x1000 \
        --section-start=.data=0x1020 -e explicit_start \
        -o "$work/explicit.$producer.elf" \
        "$work/explicit.$producer.o" "$work/amd64.defs.o" \
        >"$work/explicit.$producer.link.log" 2>&1
    objcopy -j .text -O binary "$work/explicit.$producer.elf" \
        "$work/explicit.$producer.linked.text"
done
cmp "$work/explicit.gnu.text" "$work/explicit.mini.text"
cmp "$work/explicit.gnu.linked.text" "$work/explicit.mini.linked.text"
python3 - "$work/explicit.gnu.reloc" "$work/explicit.mini.reloc" <<'PY'
from pathlib import Path
import re
import sys

expected = [('0', 'R_X86_64_8', 'abs_byte', '+ 1'),
            ('1', 'R_X86_64_16', 'abs_half', '+ 2'),
            ('3', 'R_X86_64_PC8', 'cross_data', '+ 0'),
            ('4', 'R_X86_64_PC16', 'cross_data', '+ 0')]
def rows(path):
    result=[]
    for line in Path(path).read_text().splitlines():
        match=re.match(r'^\s*([0-9a-f]+)\s+[0-9a-f]+\s+'
                       r'(R_X86_64_[A-Z0-9_]+)\s+[0-9a-f]+\s+'
                       r'(\S+)\s+([+-])\s+(\S+)', line)
        if match:
            offset, kind, symbol, sign, addend = match.groups()
            result.append((str(int(offset,16)),kind,symbol,sign+' '+addend))
    return result
assert rows(sys.argv[1]) == expected, rows(sys.argv[1])
assert rows(sys.argv[2]) == expected, rows(sys.argv[2])
PY
if as --32 -o "$work/explicit.gnu-invalid.o" "$explicit" \
        >"$work/explicit.gnu-invalid.log" 2>&1; then
    echo 'MINIAS_X86_NARROW_RELOC=FAIL GNU_accepted_AMD64_reloc_in_i386' >&2
    exit 1
fi
if "$MINIAS" --target=x86_64 -march=i386 -mabi=i386 \
        -o "$work/explicit.mini-invalid.o" "$explicit" \
        >"$work/explicit.mini-invalid.log" 2>&1; then
    echo 'MINIAS_X86_NARROW_RELOC=FAIL MiniAS_accepted_AMD64_reloc_in_i386' >&2
    exit 1
fi
printf 'MINIAS_X86_NARROW_RELOC=PASS AMD64_explicit_reloc_names=4 GNU_ld_bytes=exact i386_mismatch=reject\n'

for abi in amd64 i386; do
    if [[ $abi == amd64 ]]; then
        gas=(as --64)
        mini=(--target=x86_64 -march=x86-64 -mabi=sysv)
        link_mode=elf_x86_64
        type_prefix=R_X86_64_
        reloc_section=.rela.text
    else
        gas=(as --32)
        mini=(--target=x86_64 -march=i386 -mabi=i386)
        link_mode=elf_i386
        type_prefix=R_386_
        reloc_section=.rel.text
    fi
    "${gas[@]}" -o "$work/$abi.gnu.o" "$input"
    "${gas[@]}" -o "$work/$abi.defs.o" "$definitions"
    "$MINIAS" "${mini[@]}" -o "$work/$abi.mini.o" "$input"
    for producer in gnu mini; do
        readelf -Wr "$work/$abi.$producer.o" >"$work/$abi.$producer.reloc"
        objcopy -j .text -O binary "$work/$abi.$producer.o" \
            "$work/$abi.$producer.text"
    done
    cmp "$work/$abi.gnu.text" "$work/$abi.mini.text"

    python3 - "$work/$abi.gnu.reloc" "$work/$abi.mini.reloc" \
        "$type_prefix" "$reloc_section" <<'PY'
from pathlib import Path
import re
import sys

gnu_path, mini_path, prefix, section = sys.argv[1:]
expected = [
    ('0', prefix + '8', 'abs_byte'),
    ('1', prefix + '16', 'abs_half'),
    ('3', prefix + 'PC8', 'cross_data'),
    ('4', prefix + 'PC16', 'cross_data'),
    ('6', prefix + '32', 'abs_long'),
]
def relocations(path):
    content = Path(path).read_text()
    assert "Relocation section '" + section + "'" in content, path
    result = []
    for line in content.splitlines():
        found = re.match(r'^\s*([0-9a-f]+)\s+[0-9a-f]+\s+'
                         r'(R_(?:X86_64|386)_[A-Z0-9_]+)\s+'
                         r'[0-9a-f]+\s+(\S+)(.*)$', line)
        if found:
            offset, kind, symbol, tail = found.groups()
            result.append((str(int(offset, 16)), kind, symbol,
                           ' '.join(tail.split())))
    return result

gnu, mini = relocations(gnu_path), relocations(mini_path)
assert [(off, kind, symbol) for off, kind, symbol, _ in gnu] == expected, gnu
assert mini == gnu, (gnu, mini)
if prefix == 'R_X86_64_':
    assert [addend for _, _, _, addend in gnu] == [
        '+ 1', '+ 2', '+ 0', '+ 0', '+ 3'], gnu
else:
    assert all(not addend for _, _, _, addend in gnu), gnu
PY

    for producer in gnu mini; do
        # Adjacent .text/.data makes PC8 linkable. The two producers must
        # resolve exactly the same narrow writes, including ELF32 REL's
        # implicit addends and ELF64 RELA's explicit addends.
        ld -m "$link_mode" -n --section-start=.text=0x1000 \
            --section-start=.data=0x1020 -e narrow_start \
            -o "$work/$abi.$producer.elf" \
            "$work/$abi.$producer.o" "$work/$abi.defs.o" \
            >"$work/$abi.$producer.link.log" 2>&1
        objcopy -j .text -O binary "$work/$abi.$producer.elf" \
            "$work/$abi.$producer.linked.text"
        # A genuine PC8 relocation must overflow rather than silently
        # widening to PC32 when the cross-section target moves away.
        if ld -m "$link_mode" -n --section-start=.text=0x1000 \
              --section-start=.data=0x2000 -e narrow_start \
              -o "$work/$abi.$producer.overflow.elf" \
              "$work/$abi.$producer.o" "$work/$abi.defs.o" \
              >"$work/$abi.$producer.overflow.log" 2>&1; then
            echo "MINIAS_X86_NARROW_RELOC=FAIL missing_PC8_overflow:$abi:$producer" >&2
            exit 1
        fi
        if ! grep -q 'relocation truncated to fit:' \
              "$work/$abi.$producer.overflow.log"; then
            echo "MINIAS_X86_NARROW_RELOC=FAIL unexpected_overflow:$abi:$producer" >&2
            exit 1
        fi
    done
    cmp "$work/$abi.gnu.linked.text" "$work/$abi.mini.linked.text"
    printf 'MINIAS_X86_NARROW_RELOC=PASS abi=%s GNU_object_bytes=exact relocations=5 GNU_ld_bytes=exact PC8_overflow=reject\n' "$abi"
done
