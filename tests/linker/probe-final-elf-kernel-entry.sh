#!/usr/bin/env bash
# Real Linux first-link modes: high-half kernel LMA-entry ET_EXEC and ET_DYN.
# Both GNU reference and Mini candidate must resolve the same executable
# instruction location; merely accepting any entry inside PT_LOAD is unsafe.
set -Eeuo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
audit="$repo/tools/ci/minild-final-elf-audit.py"
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-elf-kernel-entry.XXXXXX")
trap 'rm -rf -- "$work"' EXIT

cat >"$work/entry.s" <<'ASM'
.section .text,"ax",@progbits
.globl _start
_start:
    nop
    nop
    nop
    ret
ASM
cat >"$work/high-half.lds" <<'LDS'
OUTPUT_ARCH(i386:x86-64)
ENTRY(physical_entry)
PHDRS { text PT_LOAD FLAGS(5); }
SECTIONS {
  . = 0xffffffff81000000;
  .text : AT(0x1000000) { *(.text) } :text
  physical_entry = ABSOLUTE(_start - 0xffffffff80000000);
}
LDS
as -o "$work/entry.o" "$work/entry.s"
ld -m elf_x86_64 -static -T "$work/high-half.lds" \
    -o "$work/gnu-high-half.elf" "$work/entry.o"
ld -m elf_x86_64 -shared -e _start \
    -o "$work/gnu-et-dyn.elf" "$work/entry.o"
cp "$work/gnu-high-half.elf" "$work/mini-high-half.elf"
cp "$work/gnu-et-dyn.elf" "$work/mini-et-dyn.elf"

python3 - "$work" <<'PY'
from pathlib import Path
import struct
import sys

work = Path(sys.argv[1])
original = (work / 'mini-high-half.elf').read_bytes()
assert struct.unpack_from('<Q', original, 24)[0] == 0x1000000
phoff = struct.unpack_from('<Q', original, 32)[0]
assert struct.unpack_from('<I', original, phoff)[0] == 1
assert struct.unpack_from('<Q', original, phoff + 16)[0] == 0xffffffff81000000
assert struct.unpack_from('<Q', original, phoff + 24)[0] == 0x1000000

def mutant(name, entry=None, physical=None):
    image = bytearray(original)
    if entry is not None:
        struct.pack_into('<Q', image, 24, entry)
    if physical is not None:
        struct.pack_into('<Q', image, phoff + 24, physical)
    (work / name).write_bytes(image)

# No executable virtual or physical mapping can cover this entry.
mutant('entry-outside.elf', entry=0x2000000)
# Entry unchanged, but the candidate loader maps text elsewhere physically.
mutant('physical-outside.elf', physical=0x2000000)
# Entry is still inside candidate physical text, but now names a different
# instruction relative to the GNU control (offset 1 instead of offset 0).
mutant('physical-shifted.elf', physical=0xffffff)

invalid_type = bytearray((work / 'mini-et-dyn.elf').read_bytes())
assert struct.unpack_from('<H', invalid_type, 16)[0] == 3
struct.pack_into('<H', invalid_type, 16, 1)
(work / 'et-rel.elf').write_bytes(invalid_type)
PY

python3 "$audit" "$work/gnu-high-half.elf" "$work/mini-high-half.elf"
python3 "$audit" "$work/gnu-et-dyn.elf" "$work/mini-et-dyn.elf"
for name in entry-outside physical-outside physical-shifted; do
    if python3 "$audit" "$work/gnu-high-half.elf" "$work/$name.elf" \
            >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        echo "MINILD_ELF_KERNEL_ENTRY=FAIL accepted_invalid=$name" >&2
        exit 1
    fi
    grep -q '^MINILD_FINAL_ELF_AUDIT=FAIL ' "$work/$name.stderr"
done
if python3 "$audit" "$work/gnu-et-dyn.elf" "$work/et-rel.elf" \
        >"$work/et-rel.stdout" 2>"$work/et-rel.stderr"; then
    echo 'MINILD_ELF_KERNEL_ENTRY=FAIL accepted_ET_REL' >&2
    exit 1
fi
grep -q '^MINILD_FINAL_ELF_AUDIT=FAIL ' "$work/et-rel.stderr"
echo 'MINILD_ELF_KERNEL_ENTRY=PASS GNU_high_half_LMA_entry=1 ET_DYN=1 invalid_entry_or_physical_map=3 ET_REL_rejected=1'
