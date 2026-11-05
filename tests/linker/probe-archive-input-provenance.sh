#!/usr/bin/env bash
# GNU BFD parity for script selectors qualified by independent archive/member
# names.  Archive inputs may have arbitrary absolute paths and thin members
# may have a path of their own; neither fact is a synthetic object filename.
set -Eeuo pipefail

: "${MINILD:?MINILD must point to minic-ld}"
for tool in as ar ld python3 readelf; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "MINILD_ARCHIVE_PROVENANCE=INCONCLUSIVE missing_tool=$tool" >&2
        exit 8
    }
done

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-archive-provenance.XXXXXXXX")
cleanup() {
    if [[ ${MINILD_ARCHIVE_PROVENANCE_KEEP:-0} == 1 ]]; then
        echo "MINILD_ARCHIVE_PROVENANCE_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap cleanup EXIT

cat > "$work/entry.s" <<'ASM'
.section .text.entry,"ax",@progbits
.globl _start
_start:
    .quad keep_fn
    .byte 0x01,0x02,0x03,0x04
ASM
cat > "$work/keep.s" <<'ASM'
.section .text.member,"ax",@progbits
.globl keep_fn
keep_fn:
    .byte 0xa1,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8
.section .text.dead,"ax",@progbits
.globl discard_me
discard_me:
    .byte 0xb1,0xb2,0xb3,0xb4
ASM
as --64 -o "$work/entry.o" "$work/entry.s"
as --64 -o "$work/keep.o" "$work/keep.s"
ar crs "$work/libregular.a" "$work/keep.o"
ar crsT "$work/libthin.a" "$work/keep.o"

for kind in regular thin standalone; do
    for gc in no_gc gc; do
        archive="$work/lib$kind.a"
        selector="*lib$kind.a:*keep.o"
        inputs=("$work/entry.o" "$archive")
        if [[ $kind == standalone ]]; then
            selector='*keep.o'
            inputs=("$work/entry.o" "$work/keep.o")
        fi
        cat > "$work/$kind-$gc.lds" <<LDS
ENTRY(_start)
SECTIONS {
  . = 0x400000;
  .text : { KEEP(*(.text.entry)) $selector(.text.member) }
  /DISCARD/ : { *(.text.member) *(.text.dead) }
}
LDS
        opts=(-static -e _start)
        if [[ $gc == gc ]]; then
            opts+=(--gc-sections)
        fi
        ld -m elf_x86_64 "${opts[@]}" -T "$work/$kind-$gc.lds" \
            -o "$work/$kind-$gc-gnu.elf" "${inputs[@]}"
        "$MINILD" -m elf_x86_64 "${opts[@]}" \
            -T "$work/$kind-$gc.lds" \
            -o "$work/$kind-$gc-mini.elf" "${inputs[@]}" \
            >"$work/$kind-$gc.stdout" 2>"$work/$kind-$gc.stderr" || {
                echo "MINILD_ARCHIVE_PROVENANCE=FAIL link:$kind:$gc" >&2
                sed -n '1,35p' "$work/$kind-$gc.stderr" >&2
                exit 1
            }
        python3 - "$work/$kind-$gc-gnu.elf" \
                    "$work/$kind-$gc-mini.elf" "$kind:$gc" <<'PY'
import struct
import subprocess
import sys
from pathlib import Path

def text_data(path):
    data = Path(path).read_bytes()
    if data[:6] != b'\x7fELF\x02\x01':
        raise SystemExit(f'{path}: not ELF64 LE')
    shoff = struct.unpack_from('<Q', data, 40)[0]
    entsize, count, names_index = struct.unpack_from('<HHH', data, 58)
    if entsize != 64 or names_index >= count:
        raise SystemExit(f'{path}: invalid section table')
    sections = [struct.unpack_from('<IIQQQQIIQQ', data, shoff + 64 * i)
                for i in range(count)]
    names_header = sections[names_index]
    names = data[names_header[4]:names_header[4] + names_header[5]]
    for section in sections:
        end = names.find(b'\x00', section[0])
        if names[section[0]:end] == b'.text':
            return section[3], data[section[4]:section[4] + section[5]]
    raise SystemExit(f'{path}: .text missing')

gnu, mini = (text_data(path) for path in sys.argv[1:3])
if gnu != mini:
    raise SystemExit(f'{sys.argv[3]}: GNU/MiniLD .text address or bytes differ'
    )
if len(gnu[1]) != 20 or b'\xa1\xa2\xa3\xa4\xa5\xa6\xa7\xa8' not in gnu[1]:
    raise SystemExit(f'{sys.argv[3]}: expected selected archive member absent')
for path in sys.argv[1:3]:
    symbols = subprocess.check_output(['readelf', '-W', '-s', path], text=True)
    if ' keep_fn\n' not in symbols:
        raise SystemExit(f'{sys.argv[3]}: archive definition absent')
print(f'MINILD_ARCHIVE_PROVENANCE_CASE=PASS {sys.argv[3]}')
PY
    done
done
echo 'MINILD_ARCHIVE_PROVENANCE=PASS modes=regular,thin,standalone gc=off,on GNU_BFD_parity=true'
