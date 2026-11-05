#!/usr/bin/env bash
# A script output section is one ordered byte stream, including bytes emitted
# directly by the script and address-counter gaps BEFORE or AFTER fragments.
# This gate requires actual ELF bytes, sh_size and symbol indices to agree.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
command -v python3 >/dev/null || exit 8

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-output-edges.XXXXXX")
finish() {
    if [[ ${MINILD_OUTPUT_EDGE_KEEP:-0} == 1 ]]; then
        echo "MINILD_OUTPUT_EDGE_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap finish EXIT

cat >"$work/first.s" <<'ASM'
.section .text.stage0,"ax",@progbits
.balign 4
.globl _start
.type _start,@function
_start:
    addi a0,zero,1
    ret
.size _start,.-_start
ASM
cat >"$work/second.s" <<'ASM'
.section .text.stage1,"ax",@progbits
.balign 4
.globl second
.type second,@function
second:
    addi a0,zero,2
    ret
.size second,.-second
ASM
"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/first.o" "$work/first.s"
"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/second.o" "$work/second.s"

cat >"$work/trailing.lds" <<'LDS'
OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : {
    *(.text.stage0)
    *(.text.stage1)
    __tail_begin = .;
    . = . + 0x20;
    __tail_end = .;
  } = 0
}
LDS
cat >"$work/mixed.lds" <<'LDS'
OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : {
    BYTE(0x11);
    *(.text.stage0)
    BYTE(0x22);
    *(.text.stage1)
    BYTE(0x33);
  } = 0
}
LDS
cat >"$work/fill.lds" <<'LDS'
OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : {
    *(.text.stage0)
    __fill_begin = .;
    . = . + 0x17;
    __fill_end = .;
    *(.text.stage1)
  } = 0x5a
}
LDS

# Inspect real section payloads. A successful link with an undersized or
# relocated section header is a failure, not a partial pass.
cat >"$work/check.py" <<'PY'
import struct
import sys
from pathlib import Path

case, machine, pathname = sys.argv[1:]
blob = Path(pathname).read_bytes()
if blob[:6] != b"\x7fELF\x02\x01":
    raise SystemExit("invalid ELF64 little-endian magic")
if struct.unpack_from("<H", blob, 18)[0] != int(machine):
    raise SystemExit("unexpected ELF machine")
if struct.unpack_from("<H", blob, 16)[0] != 2:
    raise SystemExit("not final ET_EXEC")
shoff = struct.unpack_from("<Q", blob, 40)[0]
shentsize, shnum, shstrndx = struct.unpack_from("<HHH", blob, 58)
if shentsize != 64 or shstrndx >= shnum or shoff + shnum * 64 > len(blob):
    raise SystemExit("invalid ELF shdr table")
sections = [struct.unpack_from("<IIQQQQIIQQ", blob, shoff + i * 64)
            for i in range(shnum)]
shstr = sections[shstrndx]
strbytes = blob[shstr[4]:shstr[4] + shstr[5]]

def string_at(data, pos):
    if pos >= len(data) or data.find(b"\0", pos) < 0:
        raise SystemExit("bad ELF string")
    return data[pos:data.find(b"\0", pos)].decode("ascii")

names = [string_at(strbytes, sh[0]) for sh in sections]
matches = [i for i, name in enumerate(names) if name == ".text"]
if len(matches) != 1:
    raise SystemExit(f"expected one .text, got {len(matches)}")
si = matches[0]
sh = sections[si]
if sh[1] != 1 or (sh[2] & 6) != 6 or sh[4] + sh[5] > len(blob):
    raise SystemExit("bad output .text bounds/type/flags")
data = blob[sh[4]:sh[4] + sh[5]]
for name in (".text.stage0", ".text.stage1"):
    if name in names:
        raise SystemExit(f"work fragment leaked as an ELF section: {name}")
symtabs = [sh for i, sh in enumerate(sections)
           if names[i] == ".symtab" and sh[1] == 2]
if len(symtabs) != 1:
    raise SystemExit("missing .symtab")
stab = symtabs[0]
strings = sections[stab[6]]
symnames = blob[strings[4]:strings[4] + strings[5]]
symbols = {}
for off in range(stab[4], stab[4] + stab[5], 24):
    namepos, info, other, ndx, value, size = struct.unpack_from(
        "<IBBHQQ", blob, off
    )
    if namepos:
        symbols[string_at(symnames, namepos)] = (ndx, value, size)

def sym(name):
    if name not in symbols:
        raise SystemExit(f"missing {name}")
    ndx, value, _ = symbols[name]
    if ndx != si or not sh[3] <= value <= sh[3] + len(data):
        raise SystemExit(f"bad {name} symbol section/address")
    return value - sh[3]

first, second = sym("_start"), sym("second")
if first >= second or second % 4 != 0:
    raise SystemExit("section members not in source order or misaligned")
code0 = b"\x13\x05\x10\x00\x67\x80\x00\x00" if int(machine) == 243 else b"\x90\xc3"
code1 = b"\x13\x05\x20\x00\x67\x80\x00\x00" if int(machine) == 243 else b"\x90\xc3"
if data[first:first + len(code0)] != code0 or data[second:second + len(code1)] != code1:
    raise SystemExit("wrong machine bytes at output-member addresses")

if case == "trailing":
    start, end = sym("__tail_begin"), sym("__tail_end")
    if end - start != 0x20 or end != len(data) or data[start:end] != bytes(0x20):
        raise SystemExit(f"output tail not materialized: {start=} {end=} {len(data)=}")
elif case == "mixed":
    if first == 0 or data[0] != 0x11 or data[first + len(code0)] != 0x22:
        raise SystemExit("script BYTE before/between inputs missing")
    if data[-1] != 0x33 or second + len(code1) + 1 != len(data):
        raise SystemExit("script BYTE after inputs missing, or output size wrong")
elif case == "fill":
    start, end = sym("__fill_begin"), sym("__fill_end")
    if end - start != 0x17 or data[start:end] != bytes([0x5a]) * 0x17:
        raise SystemExit("nonzero output fill pattern wrong")
else:
    raise SystemExit(f"unexpected case={case}")
print(f"MINILD_OUTPUT_EDGE_CASE=PASS case={case} machine={machine} size={len(data)}")
PY

# Independent GNU ld.bfd reference uses x86-64 objects, identical script
# placement rules, and the same ELF semantics (not RISC-V instruction bytes).
gnu_cases=0
if command -v as >/dev/null && command -v ld >/dev/null; then
    cat >"$work/host_first.s" <<'ASM'
.section .text.stage0,"ax",@progbits
.balign 4
.globl _start
_start: nop
        ret
ASM
    cat >"$work/host_second.s" <<'ASM'
.section .text.stage1,"ax",@progbits
.balign 4
.globl second
second: nop
        ret
ASM
    as -o "$work/host_first.o" "$work/host_first.s"
    as -o "$work/host_second.o" "$work/host_second.s"
    for case in trailing mixed fill; do
        sed 's/OUTPUT_ARCH(riscv)/OUTPUT_ARCH(i386:x86-64)/' \
            "$work/$case.lds" >"$work/$case.host.lds"
        ld -m elf_x86_64 -static -T "$work/$case.host.lds" \
            -o "$work/$case.host.elf" "$work/host_first.o" "$work/host_second.o"
        python3 "$work/check.py" "$case" 62 "$work/$case.host.elf"
        gnu_cases=$((gnu_cases + 1))
    done
else
    echo "MINILD_OUTPUT_EDGE_GNU=INCONCLUSIVE missing_native_as_or_ld"
fi

failures=0
for case in trailing mixed fill; do
    if ! "$MINILD" -melf64lriscv -static -T "$work/$case.lds" \
        -o "$work/$case.elf" "$work/first.o" "$work/second.o" \
        >"$work/$case.stdout" 2>"$work/$case.stderr"; then
        echo "MINILD_OUTPUT_EDGE_CASE=BLOCKED case=$case" >&2
        sed -n '1,8p' "$work/$case.stderr" >&2
        failures=$((failures + 1))
    elif ! python3 "$work/check.py" "$case" 243 "$work/$case.elf"; then
        echo "MINILD_OUTPUT_EDGE_CASE=FAIL case=$case" >&2
        failures=$((failures + 1))
    fi
done
if [[ $failures != 0 ]]; then
    echo "MINILD_OUTPUT_EDGES=FAIL failures=$failures" >&2
    exit 1
fi
echo "MINILD_OUTPUT_EDGES=PASS cases=3 gnu_oracle=$gnu_cases"
