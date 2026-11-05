#!/usr/bin/env bash
# MiniAS -> MiniLD final OutputSection ownership oracle.
# A linker-script output section must own an ordered sequence of input
# fragments and emit ONE ELF section header. Name-only aliases on per-input
# section headers are not enough: symbol st_shndx, relocation results and
# SHF_LINK_ORDER sh_link must all reference the final output section.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
for required in python3 readelf; do
    command -v "$required" >/dev/null 2>&1 || {
        echo "MINILD_OUTPUT_SECTION=INCONCLUSIVE missing_tool=$required" >&2
        exit 8
    }
done

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-output-section.XXXXXX")
finish() {
    if [[ "${MINILD_OUTPUT_SECTION_KEEP:-0}" == 1 ]]; then
        echo "MINILD_OUTPUT_SECTION_ARTIFACTS=$work"
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
    call first
    call second
    addi a7,zero,93
    ecall
    # Deliberate address references keep BOTH rodata fragments live under GC.
    .dword data_alpha
    .dword data_beta
.size _start,.-_start
ASM

cat >"$work/alpha.s" <<'ASM'
.section .text.alpha,"ax",@progbits
.globl first
.type first,@function
first:
    addi a0,zero,11
    ret
.size first,.-first

.section .rodata.alpha,"a",@progbits
.balign 8
.globl data_alpha
.type data_alpha,@object
data_alpha:
    .dword first
.size data_alpha,.-data_alpha

.section .meta.alpha,"ao",@progbits,first
    .word 0xa1b2c3d4
ASM

cat >"$work/beta.s" <<'ASM'
.section .text.beta,"ax",@progbits
.p2align 5
.globl second
.type second,@function
second:
    addi a0,zero,22
    ret
.size second,.-second

.section .rodata.beta,"a",@progbits
.balign 16
.globl data_beta
.type data_beta,@object
data_beta:
    .dword second
.size data_beta,.-data_beta
ASM

cat >"$work/layout.lds" <<'LDS'
OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { KEEP(*(.text.entry)) *(.text.alpha) *(.text.beta) }
  .rodata : { *(.rodata.alpha) *(.rodata.beta) }
  .meta : { *(.meta.alpha) }
}
LDS

# The location counter can advance INSIDE one output section.  This must be
# materialized as a file-backed gap in that same section, never as a second
# shdr or as a gap only in sh_addr with an undersized sh_size.  The explicit
# `= 0` fill pattern is necessary: GNU ld's default code-section gap filler
# may be architecture-specific NOPs instead of zero bytes.
cat >"$work/hole.lds" <<'LDS'
OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : {
    KEEP(*(.text.entry))
    *(.text.alpha)
    __text_hole_begin = .;
    . = . + 0x40;
    __text_hole_end = .;
    *(.text.beta)
  } = 0
  .rodata : { *(.rodata.alpha) *(.rodata.beta) }
  .meta : { *(.meta.alpha) }
}
LDS

for name in entry alpha beta; do
    "$MINIAS" -march=rv64i -mabi=lp64 -o "$work/$name.o" "$work/$name.s"
done

cat >"$work/check.py" <<'PY'
import struct
import sys
from pathlib import Path

case, file_name = sys.argv[1:]
path = Path(file_name)
data = path.read_bytes()
if data[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<HH", data, 16) != (2, 243):
    raise SystemExit(f"{path}: final image is not RV64 ELF ET_EXEC")

shoff = struct.unpack_from("<Q", data, 40)[0]
shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 58)
if shentsize != 64 or shstrndx >= shnum or shoff + 64 * shnum > len(data):
    raise SystemExit(f"{path}: invalid ELF section table")
sections = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + 64 * i)
            for i in range(shnum)]
shstr = sections[shstrndx]
names = data[shstr[4]:shstr[4] + shstr[5]]
def string_at(blob, offset):
    if offset >= len(blob):
        raise SystemExit(f"{path}: invalid string offset {offset}")
    end = blob.find(b"\0", offset)
    if end < 0:
        raise SystemExit(f"{path}: unterminated ELF string")
    return blob[offset:end].decode("ascii")

by_name = {}
for index, sh in enumerate(sections):
    name = string_at(names, sh[0])
    by_name.setdefault(name, []).append((index, sh))

def unique_section(name, flags):
    matches = by_name.get(name, [])
    if len(matches) != 1:
        raise SystemExit(f"{path}: final {name} has {len(matches)} section headers, expected one")
    index, sh = matches[0]
    if sh[1] != 1 or sh[2] & flags != flags:
        raise SystemExit(f"{path}: {name} has wrong type/flags: {sh}")
    if sh[4] + sh[5] > len(data):
        raise SystemExit(f"{path}: {name} payload exceeds file")
    return index, sh, data[sh[4]:sh[4] + sh[5]]

text_i, text, text_data = unique_section(".text", 0x6)
rodata_i, rodata, rodata_data = unique_section(".rodata", 0x2)
meta_i, meta, meta_data = unique_section(".meta", 0x82)
for input_name in (".text.entry", ".text.alpha", ".text.beta",
                   ".rodata.alpha", ".rodata.beta", ".meta.alpha"):
    if input_name in by_name:
        raise SystemExit(f"{path}: input fragment {input_name} leaked as output shdr")
if meta[6] != text_i:
    raise SystemExit(f"{path}: LINK_ORDER sh_link={meta[6]}, expected .text={text_i}")
if meta_data != b"\xd4\xc3\xb2\xa1":
    raise SystemExit(f"{path}: metadata payload corrupted: {meta_data.hex()}")

symtab_matches = [(i, s) for i, s in by_name.get(".symtab", [])
                  if s[1] == 2]
if len(symtab_matches) != 1:
    raise SystemExit(f"{path}: missing/duplicated .symtab")
_, symtab = symtab_matches[0]
if symtab[6] >= len(sections):
    raise SystemExit(f"{path}: symtab sh_link out of bounds")
strtab = sections[symtab[6]]
string_bytes = data[strtab[4]:strtab[4] + strtab[5]]
symbol_bytes = data[symtab[4]:symtab[4] + symtab[5]]
if len(symbol_bytes) % 24:
    raise SystemExit(f"{path}: malformed symtab size")
symbols = {}
for pos in range(0, len(symbol_bytes), 24):
    name_off, info, other, shndx, value, size = struct.unpack_from(
        "<IBBHQQ", symbol_bytes, pos
    )
    if name_off == 0:
        continue
    name = string_at(string_bytes, name_off)
    symbols.setdefault(name, []).append((shndx, value, size, info))

def require_symbol(name, section_index, section):
    found = symbols.get(name, [])
    if len(found) != 1:
        raise SystemExit(f"{path}: symbol {name} count={len(found)}, expected one")
    shndx, value, size, info = found[0]
    if shndx != section_index:
        raise SystemExit(f"{path}: {name} st_shndx={shndx}, expected {section_index}")
    if not section[3] <= value < section[3] + section[5]:
        raise SystemExit(f"{path}: {name}=0x{value:x} outside output section range")
    return value

start = require_symbol("_start", text_i, text)
first = require_symbol("first", text_i, text)
second = require_symbol("second", text_i, text)
alpha = require_symbol("data_alpha", rodata_i, rodata)
beta = require_symbol("data_beta", rodata_i, rodata)
if first >= second or first == start or second % 32 or alpha == beta or beta % 16:
    raise SystemExit(f"{path}: fragment order/alignment broken:"
                     f" _start={start:x} first={first:x} second={second:x}"
                     f" alpha={alpha:x} beta={beta:x}")
pad_begin = alpha - rodata[3] + 8
pad_end = beta - rodata[3]
if (pad_end - pad_begin != 8 or
        rodata_data[pad_begin:pad_end] != b"\0" * 8):
    raise SystemExit(f"{path}: missing 8-byte zero-filled alignment hole in .rodata")
for symbol_address, target in ((alpha, first), (beta, second)):
    rel = symbol_address - rodata[3]
    if (rel < 0 or rel + 8 > len(rodata_data) or
            struct.unpack_from("<Q", rodata_data, rel)[0] != target):
        raise SystemExit(f"{path}: finalized rodata relocation does not match target")
functions = ((first, b"\x13\x05\xb0\x00\x67\x80\x00\x00"),
             (second, b"\x13\x05\x60\x01\x67\x80\x00\x00"))
for address, code in functions:
    offset = address - text[3]
    if text_data[offset:offset + len(code)] != code:
        raise SystemExit(f"{path}: function machine code at symbol RVA incorrect")

if case == "hole":
    begin = require_symbol("__text_hole_begin", text_i, text)
    end = require_symbol("__text_hole_end", text_i, text)
    if end - begin != 0x40:
        raise SystemExit(f"{path}: script += hole has length {end - begin}, expected 64")
    hole_offset = begin - text[3]
    if text_data[hole_offset:hole_offset + 0x40] != b"\0" * 0x40:
        raise SystemExit(f"{path}: output .text did not file-back the VMA hole with zeros")
    if not (first + 8 <= begin < end <= second < text[3] + text[5]):
        raise SystemExit(f"{path}: .text sh_size/fragment ordering excludes script hole")
print(f"MINILD_OUTPUT_SECTION_CHECK=PASS case={case}"
      f" sections=3 symbols=5 linked_meta=1 explicit_hole={int(case == 'hole')}")
PY

for stage in plain gc after_r hole; do
    inputs=("$work/entry.o" "$work/alpha.o" "$work/beta.o")
    if [[ $stage == after_r ]]; then
        "$MINILD" -melf64lriscv -r -o "$work/combined.o" "${inputs[@]}"
        inputs=("$work/combined.o")
    fi
    extra=()
    if [[ $stage != plain ]]; then extra=(--gc-sections); fi
    script="$work/layout.lds"
    if [[ $stage == hole ]]; then
        script="$work/hole.lds"
        extra=()
    fi
    if ! "$MINILD" -melf64lriscv -static -T "$script" "${extra[@]}" \
        -o "$work/$stage.elf" "${inputs[@]}" \
        >"$work/$stage.stdout" 2>"$work/$stage.stderr"; then
        echo "MINILD_OUTPUT_SECTION=BLOCKED stage=$stage" >&2
        sed -n '1,16p' "$work/$stage.stderr" >&2
        exit 1
    fi
    if ! python3 "$work/check.py" "$stage" "$work/$stage.elf"; then
        echo "MINILD_OUTPUT_SECTION=FAIL stage=$stage reason=output-mapping" >&2
        exit 1
    fi
done
echo "MINILD_OUTPUT_SECTION=PASS stages=4"
