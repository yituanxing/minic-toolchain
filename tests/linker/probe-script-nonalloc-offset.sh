#!/usr/bin/env bash
# Static -T must give every explicitly selected, file-backed, non-ALLOC
# output section a real sh_offset even though it is outside PT_LOAD.
# Also check SHF_LINK_ORDER's sh_link after script section renaming.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
for tool in python3 readelf; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "MINILD_NONALLOC_OFFSET=INCONCLUSIVE missing_tool=$tool" >&2
        exit 8
    }
done

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-script-nonalloc.XXXXXX")
finish() {
    if [[ ${MINILD_NONALLOC_KEEP:-0} == 1 ]]; then
        echo "MINILD_NONALLOC_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap finish EXIT

cat >"$work/entry.s" <<'ASM'
.section .text.entry,"ax",@progbits
.globl _start
.type _start,@function
_start:
    call second
    addi a0,zero,0
    addi a7,zero,93
    ecall
.size _start,.-_start

# Target follows a live section even though these bytes are non-ALLOC.
.section .meta.note,"o",@progbits,_start
.balign 8
.globl meta_first
meta_first:
    .word 0x11223344
    .word 0x55667788

.section .debug.marker,"",@progbits
.balign 8
    .dword 0xa1b2c3d4e5f60718

# The present linker-script implementation discards unmatched non-ALLOC
# sections.  Its orphan-selection policy is tested separately from offsets.
.section .debug.orphan,"",@progbits
.balign 4
    .word 0xabcddcba

# ET_DYN's default layout must preserve these non-ALLOC payloads.
.section .comment,"",@progbits
    .asciz "minild-shared-nonalloc"
.section .debug_info,"",@progbits
    .byte 0x91,0x92,0x93,0x94,0x95
ASM

cat >"$work/second.s" <<'ASM'
.section .text.second,"ax",@progbits
.globl second
.type second,@function
second:
    addi a0,zero,3
    ret
.size second,.-second

# A second input with the same section name and a different link-order
# target must coalesce into .meta, with sh_link pointing at the merged .text.
.section .meta.note,"o",@progbits,second
.balign 8
.globl meta_second
meta_second:
    .dword 0x0102030405060708
ASM

cat >"$work/link.lds" <<'LDS'
OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text.entry) *(.text.second) }
  .meta : { *(.meta.note) }
  .debug_info : { *(.debug.marker) }
}
LDS

"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/entry.o" "$work/entry.s"
"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/second.o" "$work/second.s"

cat >"$work/check.py" <<'PY'
import struct
import sys
from pathlib import Path

path = Path(sys.argv[1])
blob = path.read_bytes()
if blob[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<HH", blob, 16) != (2, 243):
    raise SystemExit(f"{path}: not an RV64 ET_EXEC")
shoff = struct.unpack_from("<Q", blob, 40)[0]
entsize, count, names_index = struct.unpack_from("<HHH", blob, 58)
if entsize != 64 or names_index >= count or shoff + count * entsize > len(blob):
    raise SystemExit(f"{path}: section table out of bounds")
headers = [struct.unpack_from("<IIQQQQIIQQ", blob, shoff + n * entsize)
           for n in range(count)]
name_header = headers[names_index]
if name_header[4] + name_header[5] > len(blob):
    raise SystemExit(f"{path}: shstrtab out of bounds")
strings = blob[name_header[4]:name_header[4] + name_header[5]]

def get_name(sh):
    if sh[0] >= len(strings):
        raise SystemExit("section name offset out of bounds")
    end = strings.find(b"\0", sh[0])
    if end < 0:
        raise SystemExit("unterminated section name")
    return strings[sh[0]:end].decode()

named = {get_name(header): (index, header)
         for index, header in enumerate(headers)}
expected = {
    ".meta": bytes.fromhex("44332211887766550807060504030201"),
    ".debug_info": bytes.fromhex("1807f6e5d4c3b2a1"),
}
for name, payload in expected.items():
    if name not in named:
        raise SystemExit(f"{path}: missing {name}")
    section_index, section = named[name]
    _ = section_index
    sh_type, flags, address, offset, size = section[1:6]
    if sh_type != 1 or flags & 2 or address != 0:
        raise SystemExit(f"{path}: {name} is not non-ALLOC PROGBITS")
    if offset == 0 or offset == (1 << 64) - 1 or offset + size > len(blob):
        raise SystemExit(f"{path}: {name} has invalid sh_offset={offset:#x}")
    if offset + size > shoff or blob[offset:offset + size] != payload:
        raise SystemExit(f"{path}: {name} file bytes/size differ from object")
    if offset % section[8]:
        raise SystemExit(f"{path}: {name} sh_offset violates sh_addralign")

if any(name in named for name in
       (".meta.note", ".debug.marker", ".text.entry", ".debug.orphan")):
    raise SystemExit(f"{path}: -T input-section names leaked into ELF")
meta = named[".meta"][1]
if meta[2] & 0x80 == 0 or meta[6] != named[".text"][0]:
    raise SystemExit(f"{path}: SHF_LINK_ORDER sh_link did not remap to .text")

symtab = named[".symtab"][1]
strtab = headers[symtab[6]]
symbol_names = blob[strtab[4]:strtab[4] + strtab[5]]
symbol_entries = blob[symtab[4]:symtab[4] + symtab[5]]

def symbol(name):
    hits = []
    for pos in range(0, len(symbol_entries), 24):
        offset, info, other, index, value, size = struct.unpack_from(
            "<IBBHQQ", symbol_entries, pos
        )
        end = symbol_names.find(b"\0", offset)
        if end >= 0 and symbol_names[offset:end].decode() == name:
            hits.append((index, value))
    if len(hits) != 1:
        raise SystemExit(f"{path}: missing/duplicate symbol {name}")
    return hits[0]

for name, expected_offset in (("meta_first", 0), ("meta_second", 8)):
    index, value = symbol(name)
    if index != named[".meta"][0] or value != expected_offset:
        raise SystemExit(
            f"{path}: {name} shndx={index} st_value={value:#x}; "
            f"expected .meta+{expected_offset:#x}"
        )
print(f"MINILD_NONALLOC_OFFSET=PASS output={path.name} explicit=2 link_order=1")
PY

for case in normal gc; do
    flags=()
    if [[ $case == gc ]]; then
        flags+=(--gc-sections)
    fi
    "$MINILD" -melf64lriscv -static -T "$work/link.lds" \
        "${flags[@]}" -o "$work/$case.elf" "$work/entry.o" "$work/second.o"
    readelf -W -S "$work/$case.elf" >"$work/readelf-$case.txt"
    python3 "$work/check.py" "$work/$case.elf"
done

cat >"$work/check-shared.py" <<'PY'
import struct
import sys
from pathlib import Path

path = Path(sys.argv[1])
blob = path.read_bytes()
if blob[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<HH", blob, 16) != (3, 243):
    raise SystemExit(f"{path}: not an RV64 ET_DYN")
shoff = struct.unpack_from("<Q", blob, 40)[0]
entsize, count, names_index = struct.unpack_from("<HHH", blob, 58)
if entsize != 64 or names_index >= count or shoff + count * entsize > len(blob):
    raise SystemExit(f"{path}: section table out of bounds")
sections = [struct.unpack_from("<IIQQQQIIQQ", blob, shoff + n * entsize)
            for n in range(count)]
names_header = sections[names_index]
if names_header[4] + names_header[5] > len(blob):
    raise SystemExit("shstrtab out of bounds")
names = blob[names_header[4]:names_header[4] + names_header[5]]

def section_name(sh):
    if sh[0] >= len(names):
        raise SystemExit("invalid sh_name")
    end = names.find(b"\0", sh[0])
    if end < 0:
        raise SystemExit("unterminated sh_name")
    return names[sh[0]:end].decode()

mapped = {}
for index, section in enumerate(sections):
    mapped.setdefault(section_name(section), []).append((index, section))

expected = {
    ".comment": b"minild-shared-nonalloc\0",
    ".debug_info": bytes.fromhex("9192939495"),
}
intervals = []
for name, payload in expected.items():
    entries = mapped.get(name, [])
    if len(entries) != 1:
        raise SystemExit(f"{path}: expected one {name}, got {len(entries)}")
    index, sh = entries[0]
    _ = index
    offset, size = sh[4:6]
    if sh[1] != 1 or sh[2] & 2 or sh[3] != 0:
        raise SystemExit(f"{path}: invalid non-ALLOC {name} type/flags/vaddr")
    if offset in (0, (1 << 64) - 1) or offset + size > shoff:
        raise SystemExit(f"{path}: invalid {name} offset {offset:#x}")
    if offset % max(1, sh[8]) or blob[offset:offset + size] != payload:
        raise SystemExit(f"{path}: invalid {name} alignment/payload")
    intervals.append((offset, offset + size, name))
if max(intervals[0][0], intervals[1][0]) < min(intervals[0][1], intervals[1][1]):
    raise SystemExit(f"{path}: non-ALLOC output sections overlap")
symtab = mapped[".symtab"][0][1]
if section_name(sections[symtab[6]]) != ".strtab":
    raise SystemExit(f"{path}: symtab sh_link does not reference .strtab")
print("MINILD_SHARED_NONALLOC=PASS sections=.comment,.debug_info")
PY

"$MINILD" -melf64lriscv -shared --hash-style=both \
    -soname libminild-nonalloc.so -o "$work/shared.so" \
    "$work/entry.o" "$work/second.o"
readelf -W -S "$work/shared.so" >"$work/readelf-shared.txt"
python3 "$work/check-shared.py" "$work/shared.so"

# -S is not yet an implemented debug-strip operation in shared mode.
# It must fail closed on nonempty debug sections rather than emit a DSO that
# incorrectly claims stripping occurred.  The existing A4 gate covers this
# policy; repeat here for the exact bytes protected by the offset regression.
if "$MINILD" -melf64lriscv -shared -S --hash-style=both \
    -soname libminild-nonalloc.so -o "$work/shared-stripped.so" \
    "$work/entry.o" "$work/second.o" \
    >"$work/strip.stdout" 2>"$work/strip.stderr"; then
    echo "MINILD_SHARED_NONALLOC_STRIP=FAIL accepted_unstripped_debug" >&2
    exit 1
fi
grep -Fq 'unsupported-shared-strip-debug-section:.debug' \
    "$work/strip.stderr"
test ! -e "$work/shared-stripped.so"
echo 'MINILD_SHARED_NONALLOC_STRIP=PASS fail_closed'
