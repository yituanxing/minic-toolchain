#!/usr/bin/env bash
# GNU BFD oracle: scripted PT_LOAD/PT_NOTE/PT_TLS/PT_GNU_STACK must fit
# entirely before the first section, even with -n's one-byte alignment.
set -Eeuo pipefail
: "${MINILD:?MINILD must point to minic-ld}"
for executable in as ld readelf python3; do
    command -v "$executable" >/dev/null || {
        echo "MINILD_SCRIPT_HEADER_FOOTPRINT=INCONCLUSIVE missing=$executable" >&2
        exit 8
    }
done

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-script-footprint.XXXXXX")
cleanup() {
    if [[ ${MINILD_SCRIPT_HEADER_FOOTPRINT_KEEP:-0} == 1 ]]; then
        echo "MINILD_SCRIPT_HEADER_FOOTPRINT_ARTIFACTS=$work"
    else
        rm -r -- "$work"
    fi
}
trap cleanup EXIT

cat >"$work/input.S" <<'ASM'
.section .text.entry,"ax",@progbits
.globl _start
_start:
    ret
.section .tdata.value,"awT",@progbits
.globl tls_value
tls_value:
    .long 0x33445566
.section .note.audit,"a",@note
    .long 4,4,1
    .ascii "TEST"
    .long 0x12345678
.section .data.value,"aw",@progbits
    .long 0x11223344
.section .note.GNU-stack,"",@progbits
ASM

cat >"$work/layout.lds" <<'LDS'
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text.entry) }
  . = 0x20000;
  .tdata : { *(.tdata.value) }
  . = 0x30000;
  .note.audit : { *(.note.audit) }
  . = 0x40000;
  .data : { *(.data.value) }
}
LDS

for target in elf_x86_64 elf_i386; do
    case "$target" in
        elf_x86_64) as_mode=--64 ;;
        elf_i386) as_mode=--32 ;;
    esac
    as "$as_mode" -o "$work/$target.o" "$work/input.S"
    for align_mode in normal nmagic; do
        flags=()
        [[ $align_mode == nmagic ]] && flags+=(-n)
        ld -m "$target" -static "${flags[@]}" -T "$work/layout.lds" \
            -e _start -o "$work/$target.$align_mode.gnu.elf" \
            "$work/$target.o"
        "$MINILD" -m "$target" -static "${flags[@]}" \
            -T "$work/layout.lds" -e _start \
            -o "$work/$target.$align_mode.mini.elf" \
            "$work/$target.o" >"$work/$target.$align_mode.log" 2>&1 || {
                sed -n '1,35p' "$work/$target.$align_mode.log" >&2
                exit 1
            }
        readelf -lW "$work/$target.$align_mode.gnu.elf" \
            >"$work/$target.$align_mode.gnu.phdrs"
        readelf -lW "$work/$target.$align_mode.mini.elf" \
            >"$work/$target.$align_mode.mini.phdrs"
        python3 - "$work/$target.$align_mode.gnu.elf" \
            "$work/$target.$align_mode.mini.elf" "$align_mode" <<'PY'
import struct
import sys


def examine(path):
    data = open(path, 'rb').read()
    assert data[:4] == b'\x7fELF' and data[5] == 1, path
    is_64 = data[4] == 2
    if is_64:
        phoff, shoff = struct.unpack_from('<QQ', data, 32)
        phent, phnum, shent, shnum, shstrndx = struct.unpack_from('<HHHHH', data, 54)
        phfmt, shfmt = '<IIQQQQQQ', '<IIQQQQIIQQ'
    else:
        phoff, shoff = struct.unpack_from('<II', data, 28)
        phent, phnum, shent, shnum, shstrndx = struct.unpack_from('<HHHHH', data, 42)
        phfmt, shfmt = '<IIIIIIII', '<IIIIIIIIII'
    sections = [struct.unpack_from(shfmt, data, shoff + i * shent)
                for i in range(shnum)]
    shstr = sections[shstrndx]
    sh_offset, sh_size = (shstr[4], shstr[5])
    names = data[sh_offset:sh_offset + sh_size]
    by_name = {}
    for sh in sections:
        index = sh[0]
        name = names[index:names.find(b'\x00', index)].decode()
        by_name[name] = sh
    phdrs = [struct.unpack_from(phfmt, data, phoff + i * phent)
             for i in range(phnum)]
    header_end = phoff + phnum * phent
    for name in ('.text', '.tdata', '.note.audit', '.data'):
        section = by_name[name]
        offset, size = section[4], section[5]
        assert offset >= header_end, (path, name, offset, header_end)
        assert offset + size <= len(data), (path, name, offset, size)
    types = [ph[0] for ph in phdrs]
    assert types.count(7) == 1, (path, types)  # PT_TLS
    assert types.count(4) == 1, (path, types)  # PT_NOTE
    assert types.count(0x6474e551) == 1, (path, types)  # PT_GNU_STACK
    return is_64, phdrs, by_name, data


gnu = examine(sys.argv[1])
mini = examine(sys.argv[2])
assert mini[0] == gnu[0]
assert len(mini[1]) == len(gnu[1]), (len(mini[1]), len(gnu[1]))
for name in ('.text', '.tdata', '.note.audit', '.data'):
    a, b = gnu[2][name], mini[2][name]
    assert a[3] == b[3] and a[5] == b[5], (name, a, b)
    assert mini[3][b[4]:b[4] + b[5]] == gnu[3][a[4]:a[4] + a[5]], name
    if sys.argv[3] == 'nmagic':
        assert a[4] == b[4], (name, 'GNU file offset differs', a[4], b[4])
if sys.argv[3] == 'nmagic':
    # Compact -n output is especially sensitive to every PHDR byte.
    assert mini[1] == gnu[1], (mini[1], gnu[1])
PY
        echo "MINILD_SCRIPT_HEADER_FOOTPRINT=PASS target=$target mode=$align_mode GNU_BFD=true"
    done
done
