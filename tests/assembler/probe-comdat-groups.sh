#!/usr/bin/env bash
# Cross-target, real ELF COMDAT/SHT_GROUP contract. A group may contain
# multiple sections and the relocation sections belonging to them.
set -Eeuo pipefail
: "${MINIAS:?MiniAS required}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/comdat.s" <<'SRC'
.section .text.shared,"axG",@progbits,shared,comdat
.globl shared
.type shared,@function
shared:
    call external
    ret
.size shared,.-shared
.section .rodata.shared,"aG",@progbits,shared,comdat
.long shared
.text
plain:
    ret
SRC
"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/comdat.s"
as --64 -o "$work/gnu.o" "$work/comdat.s"
readelf --section-groups "$work/mini.o" >"$work/mini.group"
readelf --section-groups "$work/gnu.o" >"$work/gnu.group"
python3 - "$work/mini.group" "$work/gnu.group" <<'PY'
import re
import sys
def members(path):
    s = open(path,encoding='utf-8').read()
    if not re.search(r"COMDAT group section.*\[shared\]",s):
        raise SystemExit(f"missing COMDAT signature: {path}: {s}")
    a = set(re.findall(r"\[\s*\d+\]\s+(\.\S+)",s))
    return {x for x in a if x in
            {'.text.shared','.rodata.shared','.rela.text.shared',
             '.rela.rodata.shared'}}
a,b=map(members,sys.argv[1:])
expected={'.text.shared','.rodata.shared','.rela.text.shared',
          '.rela.rodata.shared'}
if a != expected or b != expected:
    raise SystemExit(f"GROUP membership mismatch: MiniAS {a}, GNU {b}")
print('MINIAS_COMDAT_GROUP=PASS signature=shared members=4')
PY
readelf -rW "$work/mini.o" | grep -q 'R_X86_64_PLT32'
readelf -rW "$work/mini.o" | grep -q 'R_X86_64_32'
cp "$work/mini.o" "$work/mini2.o"
ld -r "$work/mini.o" "$work/mini2.o" -o "$work/merged.o"
test "$(readelf -Ws "$work/merged.o" | grep -Ec ' FUNC .* shared$')" -eq 1
# GNU permits two distinct COMDAT sections with the same ELF section name
# when their group signatures differ. A Section key cannot be only its name.
cat >"$work/same-name.s" <<'SRC'
.section .text.same,"axG",@progbits,first,comdat
.long 0x01020304
.section .text.same,"axG",@progbits,second,comdat
.long 0x11223344
SRC
"$MINIAS" --target=x86_64 -o "$work/same-mini.o" "$work/same-name.s"
as --64 -o "$work/same-gnu.o" "$work/same-name.s"
readelf --section-groups "$work/same-mini.o" >"$work/same-mini.groups"
readelf --section-groups "$work/same-gnu.o" >"$work/same-gnu.groups"
python3 - "$work/same-mini.groups" "$work/same-gnu.groups" <<'PY_SAME'
import re, sys
for path in sys.argv[1:]:
    text=open(path,encoding='utf-8').read()
    signatures=re.findall(r'COMDAT group section.*?\[(first|second)\]',text)
    names=re.findall(r'\[\s*\d+\]\s+(\.text\.same)\s*$',text,re.M)
    if sorted(signatures)!=['first','second'] or len(names)!=2:
        raise SystemExit(f'same-name COMDAT identity loss: {path}\n{text}')
print('MINIAS_COMDAT_DUPLICATE_SECTION=PASS signatures=2 same_name_members=2')
PY_SAME
echo 'MINIAS_COMDAT_GROUP_LINK=PASS ld_multiple_objects=deduplicated'

# Exercise the same ObjectModel on genuine ELF32 RV32I, including
# relocation sections grouped with data, and GNU ld -r COMDAT dedup.
if ! command -v riscv64-linux-gnu-as >/dev/null ||
   ! command -v riscv64-linux-gnu-ld >/dev/null; then
    echo 'MINIAS_COMDAT_ELF32=SKIP(toolchain-unavailable) GNU_RV32_REQUIRED=YES'
    exit 0
fi
cat >"$work/rv32-group.s" <<'SRC_RV'
.section .text.rv32,"axG",@progbits,rv32_comdat,comdat
.globl rv32_comdat
.type rv32_comdat,@function
rv32_comdat:
    addi a0,a0,1
    ret
.size rv32_comdat,.-rv32_comdat
.section .rodata.rv32,"aG",@progbits,rv32_comdat,comdat
    .word external_rv32_data
SRC_RV
"$MINIAS" -march=rv32i -mabi=ilp32 \
    -o "$work/rv32-mini.o" "$work/rv32-group.s"
riscv64-linux-gnu-as -march=rv32i -mabi=ilp32 \
    -o "$work/rv32-gnu.o" "$work/rv32-group.s"
readelf -h "$work/rv32-mini.o" | grep -q 'Class:.*ELF32'
readelf --section-groups "$work/rv32-mini.o" >"$work/rv32-mini.groups"
readelf --section-groups "$work/rv32-gnu.o" >"$work/rv32-gnu.groups"
python3 - "$work/rv32-mini.groups" "$work/rv32-gnu.groups" <<'PY_RV'
import re,sys
def group(p):
    s=open(p,encoding='utf-8').read()
    if not re.search(r'COMDAT group section.*\[rv32_comdat\]',s):
        raise SystemExit(f'missing RV32 COMDAT signature: {p}')
    names=set(re.findall(r'\[\s*\d+\]\s+(\.\S+)',s))
    return names & {'.text.rv32','.rodata.rv32','.rela.rodata.rv32'}
expected={'.text.rv32','.rodata.rv32','.rela.rodata.rv32'}
a,b=map(group,sys.argv[1:])
if a!=expected or b!=expected:
    raise SystemExit(f'RV32 group mismatch: MiniAS={a}, GNU={b}')
print('MINIAS_COMDAT_ELF32=PASS signature=rv32_comdat members=3')
PY_RV
riscv64-linux-gnu-ld -m elf32lriscv -r \
    "$work/rv32-mini.o" "$work/rv32-mini.o" -o "$work/rv32-merged.o"
test "$(readelf -Ws "$work/rv32-merged.o" | grep -Ec ' FUNC .* rv32_comdat$')" -eq 1
echo 'MINIAS_COMDAT_ELF32_LINK=PASS ld_r=deduplicated'
