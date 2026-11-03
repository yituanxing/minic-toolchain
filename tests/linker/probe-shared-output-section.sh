#!/usr/bin/env bash
# Host-only MiniAS -> MiniLD ET_DYN writer / output section index contract.
# No cross-GNU binutils or emulator is required: verify raw ELF indexes and
# the synthetic dynamic metadata which must refer to the final shdr table.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
for tool in python3 readelf; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "MINILD_SHARED_OUTPUT=INCONCLUSIVE missing_tool=$tool" >&2
        exit 8
    }
done

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-shared-output.XXXXXX")
finish() {
    if [[ "${MINILD_SHARED_OUTPUT_KEEP:-0}" == 1 ]]; then
        echo "MINILD_SHARED_OUTPUT_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap finish EXIT

cat >"$work/input.s" <<'ASM'
.section .text.answer,"ax",@progbits
.globl exported_answer
.type exported_answer,@function
exported_answer:
  addi a0,zero,42
  ret
.size exported_answer,.-exported_answer

.section .text.second,"ax",@progbits
.globl exported_second
.type exported_second,@function
exported_second:
  addi a0,zero,7
  ret
.size exported_second,.-exported_second

.section .data.value,"aw",@progbits
.balign 8
.globl exported_ptr
.type exported_ptr,@object
exported_ptr:
  .dword exported_answer
.size exported_ptr,8
.globl external_ptr
.type external_ptr,@object
external_ptr:
  .dword outside_symbol
.size external_ptr,8

.section .meta.answer,"ao",@progbits,exported_answer
  .word 0x11223344

.section .note.minild,"a",@note
  .balign 4
  .long 4,4,1
  .ascii "GNU\0"
  .long 0x11223344
ASM

cat >"$work/exports.map" <<'MAP'
LINUX_4.15 {
  global:
    exported_answer;
    exported_ptr;
  local: *;
};
MAP

cat >"$work/vdso.lds" <<'LDS'
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
  .text : { *(.text.answer) *(.text.second) } :text
  .meta : { *(.meta.answer) } :text
  .data : { *(.data.value) } :text
  .dynamic : { *(.dynamic) } :text :dynamic
}
PHDRS {
  text PT_LOAD FLAGS(5) FILEHDR PHDRS;
  dynamic PT_DYNAMIC FLAGS(4);
  note PT_NOTE FLAGS(4);
  eh_frame_hdr PT_GNU_EH_FRAME;
}
VERSION {
  LINUX_4.15 {
    global: exported_answer; exported_ptr;
    local: *;
  };
}
LDS

"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/input.o" "$work/input.s"

cat >"$work/check.py" <<'PY'
import struct
import sys
from pathlib import Path

mode, filename = sys.argv[1:]
data = Path(filename).read_bytes()
if data[:6] != b"\x7fELF\x02\x01":
    raise SystemExit("not a little-endian ELF64 file")
e_type, e_machine = struct.unpack_from("<HH", data, 16)
if (e_type, e_machine) != (3, 243):
    raise SystemExit("shared output is not RISC-V ET_DYN")
shoff = struct.unpack_from("<Q", data, 40)[0]
entsize, count, shstrndx = struct.unpack_from("<HHH", data, 58)
if entsize != 64 or count < 8 or shstrndx >= count or shoff + count * 64 > len(data):
    raise SystemExit("invalid ELF section table")
sections = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * 64)
            for i in range(count)]
shstr = sections[shstrndx]
names = data[shstr[4]:shstr[4] + shstr[5]]

def cstr(blob, start):
    if start >= len(blob):
        raise SystemExit("invalid ELF name offset")
    end = blob.find(b"\0", start)
    if end < 0:
        raise SystemExit("unterminated ELF string")
    return blob[start:end].decode("ascii")

by_name = {}
for index, section in enumerate(sections):
    by_name.setdefault(cstr(names, section[0]), []).append((index, section))

def unique(name, section_type):
    matches = by_name.get(name, [])
    if len(matches) != 1 or matches[0][1][1] != section_type:
        raise SystemExit(f"{name}: expected one type={section_type}, got {matches}")
    return matches[0]

def contents(section):
    start, size = section[4:6]
    if start + size > len(data):
        raise SystemExit("out of bounds section")
    return data[start:start + size]

dynsym_i, dynsym = unique(".dynsym", 11)
dynstr_i, dynstr = unique(".dynstr", 3)
symtab_i, symtab = unique(".symtab", 2)
strtab_i, strtab = unique(".strtab", 3)
_, dynamic = unique(".dynamic", 6)
_, hash_section = unique(".hash", 5)
_, gnu_hash = unique(".gnu.hash", 0x6ffffff6)
_, rela = unique(".rela.dyn", 4)
text_i, text = unique(".text" if mode == "vdso" else ".text.answer", 1)
second_i, second = ((text_i, text) if mode == "vdso" else
                    unique(".text.second", 1))
data_i, data_sec = unique(".data" if mode == "vdso" else ".data.value", 1)
meta_i, meta = unique(".meta" if mode == "vdso" else ".meta.answer", 1)

for name, section, target in ((".dynsym", dynsym, dynstr_i),
                               (".symtab", symtab, strtab_i),
                               (".dynamic", dynamic, dynstr_i),
                               (".hash", hash_section, dynsym_i),
                               (".gnu.hash", gnu_hash, dynsym_i),
                               (".rela.dyn", rela, dynsym_i),
                               (".meta", meta, text_i)):
    if section[6] != target:
        raise SystemExit(f"{name}.sh_link={section[6]}, expected {target}")
if meta[2] & (0x80 | 0x2) != 0x82:
    raise SystemExit("SHF_LINK_ORDER/ALLOC flags missing")
if contents(meta) != b"\x44\x33\x22\x11":
    raise SystemExit("SHF_LINK_ORDER payload changed")

def read_symbols(section, str_section):
    blob = contents(section)
    strings = contents(str_section)
    if len(blob) % 24:
        raise SystemExit("invalid ELF symbol table size")
    result = {}
    for pos in range(0, len(blob), 24):
        name, info, other, shndx, value, size = struct.unpack_from(
            "<IBBHQQ", blob, pos
        )
        if name:
            result[cstr(strings, name)] = (shndx, value, size, info)
    return result

dynsymbols = read_symbols(dynsym, dynstr)
symbols = read_symbols(symtab, strtab)
for name, shndx, section in (("exported_answer", text_i, text),
                             ("exported_second", second_i, second),
                             ("exported_ptr", data_i, data_sec)):
    if name not in symbols:
        raise SystemExit(f"missing static symbol {name}")
    actual, value, _, _ = symbols[name]
    if actual != shndx or not section[3] <= value < section[3] + section[5]:
        raise SystemExit(f"{name}: wrong static section/value: {symbols[name]}")

if mode not in ("versioned", "vdso"):
    for name, shndx in (("exported_answer", text_i),
                        ("exported_second", second_i),
                        ("exported_ptr", data_i)):
        if name not in dynsymbols or dynsymbols[name][0] != shndx:
            raise SystemExit(f"{name}: dynamic st_shndx mismatch")
else:
    version_i, versym = unique(".gnu.version", 0x6fffffff)
    verdef_i, verdef = unique(".gnu.version_d", 0x6ffffffd)
    if versym[6] != dynsym_i or verdef[6] != dynstr_i:
        raise SystemExit("version metadata linked to old shdr indexes")
    for name, shndx in (("exported_answer", text_i),
                        ("exported_ptr", data_i)):
        if name not in dynsymbols or dynsymbols[name][0] != shndx:
            raise SystemExit(f"{name}: versioned dynamic st_shndx mismatch")
    if "exported_second" in dynsymbols:
        raise SystemExit("version script failed to hide exported_second")

if "outside_symbol" not in dynsymbols or dynsymbols["outside_symbol"][0] != 0:
    raise SystemExit("unresolved dynamic symbol must remain SHN_UNDEF")
if mode == "vdso":
    if any(name in by_name for name in
           (".text.answer", ".text.second", ".data.value", ".meta.answer")):
        raise SystemExit("input sections leaked into scripted ELF section headers")
    answer = symbols["exported_answer"][1] - text[3]
    second_off = symbols["exported_second"][1] - text[3]
    text_bytes = contents(text)
    if (answer < 0 or second_off < 0 or
            text_bytes[answer:answer + 4] != b"\x13\x05\xa0\x02" or
            text_bytes[second_off:second_off + 4] != b"\x13\x05\x70\x00"):
        raise SystemExit("scripted .text contents do not match final symbol offsets")

    phoff = struct.unpack_from("<Q", data, 32)[0]
    phentsize, phcount = struct.unpack_from("<HH", data, 54)
    if phentsize != 56 or phcount != 4 or phoff + phentsize * phcount > len(data):
        raise SystemExit("vDSO PHDR layout was not preserved")
    phdrs = [struct.unpack_from("<IIQQQQQQ", data, phoff + i * 56)
             for i in range(phcount)]
    _, note = unique(".note", 7)
    _, dyn = unique(".dynamic", 6)
    if ([ph[0] for ph in phdrs] != [1, 2, 4, 0x6474e550] or
            phdrs[0][2] != 0 or phdrs[0][3] != 0 or
            phdrs[1][3] != dyn[3] or phdrs[1][2] != dyn[4] or
            phdrs[2][3] != note[3] or phdrs[2][2] != note[4]):
        raise SystemExit("vDSO PT_LOAD/PT_DYNAMIC/PT_NOTE contract broken")
if ((mode != "vdso" and text_i == second_i) or dynsym_i >= count or
        symtab_i >= count or meta_i >= count):
    raise SystemExit("ELF section indexes invalid")
print(f"MINILD_SHARED_OUTPUT_CHECK=PASS mode={mode}"
      f" sections={count} dynsym={dynsym_i} symtab={symtab_i}"
      f" link_order={text_i} versioned={mode in ('versioned', 'vdso')}")
PY

"$MINILD" -melf64lriscv -r -o "$work/after_r.o" "$work/input.o"
for mode in normal versioned after_r vdso; do
    extra=()
    input="$work/input.o"
    if [[ $mode == versioned ]]; then
        extra=(--version-script "$work/exports.map")
    elif [[ $mode == after_r ]]; then
        input="$work/after_r.o"
    elif [[ $mode == vdso ]]; then
        extra=(-T "$work/vdso.lds")
    fi
    if ! "$MINILD" -melf64lriscv -shared --hash-style=both \
        -soname libminild-shared.so "${extra[@]}" \
        -o "$work/$mode.so" "$input" \
        >"$work/$mode.stdout" 2>"$work/$mode.stderr"; then
        echo "MINILD_SHARED_OUTPUT=BLOCKED mode=$mode" >&2
        sed -n '1,12p' "$work/$mode.stderr" >&2
        exit 1
    fi
    python3 "$work/check.py" "$mode" "$work/$mode.so" || {
        echo "MINILD_SHARED_OUTPUT=FAIL mode=$mode" >&2
        exit 1
    }
done
echo "MINILD_SHARED_OUTPUT=PASS modes=4"
