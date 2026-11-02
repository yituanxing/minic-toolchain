#!/usr/bin/env bash
# One immutable target data layout drives GNU .init_array, DWARF and CFI
# independently of ELF's object format/class/relo policy. Validate the
# complete six-profile source-to-object path against GNU GAS.
set -Eeuo pipefail
repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
: "${MINIAS:=$repo/build/release/bin/minic-as}"
test -x "$MINIAS"
if [[ ${MINIAS_REQUIRE_SIX_GNU:-0} == 1 ]]; then
    for gas in riscv64-linux-gnu-as aarch64-linux-gnu-as loongarch64-linux-gnu-as; do
        if ! command -v "$gas" >/dev/null; then
            echo "required six-profile GNU oracle unavailable: $gas" >&2
            exit 1
        fi
    done
fi
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

python3 - "$repo" <<'PY'
from pathlib import Path
import re
import sys

src=Path(sys.argv[1])/'assembler/src'
api=(src/'minias_target_api.h').read_text()
driver=(src/'assembler.c').read_text()
core='\n'.join((src/name).read_text() for name in (
    'common_lowering.c','cfi.c','cfi_writer.c','debug_line.c'))
writer=(src/'elf_writer.c').read_text()
object_h=(src/'minias_object_model.h').read_text()
for token in ('typedef struct MiniAsTargetDataLayout',
              'uint8_t address_size;', 'MiniAsTargetDataLayout layout;',
              'typedef enum MiniAsObjectFormat', 'MINIAS_OBJECT_FORMAT_ELF',
              'MiniAsObjectFormat format;', 'MiniAsElfObjectSpec elf;'):
    if token not in api:
        raise SystemExit('missing split target layout/object format: '+token)
for filename in ('common_lowering.c','cfi.c','cfi_writer.c','debug_line.c'):
    body=(src/filename).read_text()
    if not re.search(r'(?:target_spec->layout\.address_size|data_layout)',body):
        raise SystemExit('Common address-width still not data layout: '+filename)
    if re.search(r'object\.elf32|object\.elf\.rel_relocations',body):
        raise SystemExit('Common layout consumes ELF wire policy: '+filename)
if re.search(r'object\.elf32|abi\.elf32|initial_elf_flags',api+driver+core+writer):
    raise SystemExit('obsolete ELF class/flag field survived')
if 'unsupported-object-format' not in driver or \
   'MINIAS_OBJECT_FORMAT_ELF' not in driver:
    raise SystemExit('Driver lacks fail-closed object format dispatch')
if 'minias_write_elf_object(' not in writer or \
   'target_abi.address_size' not in writer or \
   'ELFCLASS32' not in writer or 'ELFCLASS64' not in writer:
    raise SystemExit('ELF writer still takes caller-selected ELF class')
if 'minias_write_elf32(' in writer or 'minias_write_elf64(' in writer:
    raise SystemExit('old split ELF32/64 write paths survived')
if 'struct MiniAs {' in object_h:
    raise SystemExit('ObjectModel leaks Common mutable session')
print('MINIAS_TARGET_DATALAYOUT_BOUNDARY=PASS layout=immutable addr=4,8 '
      'format=ELF_only writer=one_class_projection legacy=absent')
PY

# Public terminal view must not reveal Common's mutable session internals.
printf '%s\n' '
#include "minias_elf_object_view.h"
int invalid(struct MiniAs *session) { return (int)session->section_count; }
' | "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -I"$repo/assembler/src" \
    -x c -fsyntax-only - >"$work/broad-view.log" 2>&1 && {
        echo 'ELF terminal view exposes Common session layout' >&2
        exit 1
    }

run_case() {
    local name=$1 target=$2 march=$3 mabi=$4 width=$5 machine=$6 code=$7 cfa=$8
    local gas=$9
    local object="$work/$name.mini.o" reference="$work/$name.gnu.o"
    local source="$work/$name.s"
    local scalar='.quad'
    local -a gas_args=()
    if [[ "$width" == 4 ]]; then scalar='.long'; fi
    case "$name" in
      rv32) gas_args=(-march=rv32i -mabi=ilp32) ;;
      rv64) gas_args=(-march=rv64i -mabi=lp64) ;;
      i386) gas_args=(--32) ;;
      amd64) gas_args=(--64) ;;
      aarch64) gas_args=(-march=armv8-a) ;;
      loong) gas_args=(-mabi=lp64d) ;;
    esac
    cat >"$source" <<ASM
.file 1 "layout.c"
.cfi_sections .debug_frame
.text
.globl layout_probe
.type layout_probe,@function
layout_probe:
.cfi_startproc simple
.cfi_def_cfa $cfa,16
.loc 1 7 0
$code
.cfi_endproc
.size layout_probe,.-layout_probe
.section .init_array.layout,"aw",@init_array
$scalar layout_probe
ASM
    "$MINIAS" "--target=$target" "-march=$march" "-mabi=$mabi" \
        -o "$object" "$source"
    "$gas" "${gas_args[@]}" -o "$reference" "$source"
    python3 - "$object" "$reference" "$width" "$machine" "$name" <<'PY'
from pathlib import Path
import struct
import sys

def elf(path):
    b=Path(path).read_bytes()
    assert b[:4]==b'\x7fELF' and b[5]==1
    cls=b[4]
    machine=struct.unpack_from('<H',b,18)[0]
    if cls==1:
        shoff=struct.unpack_from('<I',b,32)[0]
        shentsize,shnum,shstr=struct.unpack_from('<HHH',b,46)
        fmt='<IIIIIIIIII'
    else:
        shoff=struct.unpack_from('<Q',b,40)[0]
        shentsize,shnum,shstr=struct.unpack_from('<HHH',b,58)
        fmt='<IIQQQQIIQQ'
    records=[struct.unpack_from(fmt,b,shoff+i*shentsize) for i in range(shnum)]
    names=records[shstr]
    strings=b[names[4]:names[4]+names[5]]
    sections={}
    for rec in records:
        start=rec[0]
        end=strings.index(b'\0',start)
        name=strings[start:end].decode()
        sections[name]=rec
    return cls,machine,sections

candidate=elf(sys.argv[1])
reference=elf(sys.argv[2])
width=int(sys.argv[3]); machine=int(sys.argv[4]); name=sys.argv[5]
assert candidate[0]==reference[0]==(1 if width==4 else 2),name
assert candidate[1]==reference[1]==machine,name
for section in ('.init_array.layout','.debug_frame','.debug_line'):
    assert section in candidate[2] and section in reference[2],(name,section)
mini=candidate[2]['.init_array.layout']
gnu=reference[2]['.init_array.layout']
for key in (1,2,5,8,9):
    assert mini[key]==gnu[key],(name,key,mini[key],gnu[key])
assert mini[1]==14 and mini[5]==width and mini[9]==width,(name,mini)
print(f'MINIAS_DATALAYOUT_GNU=PASS profile={name} '
      f'address_size={width} machine={machine} ctor_entsize={width} '
      'CFI=present DWARF=present')
PY
}

run_case amd64 x86_64 x86-64 sysv 8 62 nop 7 as
run_case i386 x86_64 i386 i386 4 3 nop 4 as
if command -v riscv64-linux-gnu-as >/dev/null; then
    run_case rv32 riscv rv32i ilp32 4 243 'addi x0,x0,0' 2 riscv64-linux-gnu-as
    run_case rv64 riscv rv64i lp64 8 243 'addi x0,x0,0' 2 riscv64-linux-gnu-as
else
    echo 'MINIAS_DATALAYOUT_GNU=SKIP profiles=rv32,rv64 reason=missing_GNU_AS'
fi
if command -v aarch64-linux-gnu-as >/dev/null; then
    run_case aarch64 aarch64 armv8-a lp64 8 183 nop 31 aarch64-linux-gnu-as
else
    echo 'MINIAS_DATALAYOUT_GNU=SKIP profile=aarch64 reason=missing_GNU_AS'
fi
if command -v loongarch64-linux-gnu-as >/dev/null; then
    run_case loong loongarch64 loongarch64 lp64d 8 258 'addi.d $r0,$r0,0' 3 loongarch64-linux-gnu-as
else
    echo 'MINIAS_DATALAYOUT_GNU=SKIP profile=loong reason=missing_GNU_AS'
fi
