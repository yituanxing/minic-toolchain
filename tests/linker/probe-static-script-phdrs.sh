#!/usr/bin/env bash
# Cross-check GNU scripted static PT_LOAD/PT_NOTE and :phdr inheritance.
set -Eeuo pipefail
: "${MINILD:?MINILD must point to minic-ld}"
for executable in as ld python3 readelf; do
    command -v "$executable" >/dev/null || {
        echo "MINILD_SCRIPT_PHDRS=INCONCLUSIVE missing=$executable" >&2
        exit 8
    }
done
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-script-phdrs.XXXXXX")
cleanup() {
    if [[ ${MINILD_SCRIPT_PHDRS_KEEP:-0} == 1 ]]; then
        echo "MINILD_SCRIPT_PHDRS_ARTIFACTS=$work"
    else
        rm -r -- "$work"
    fi
}
trap cleanup EXIT

cat >"$work/fixture.s" <<'ASM'
.section .head,"ax",@progbits
  nop
.section .text.entry,"ax",@progbits
.globl _start
_start:
  mov $60,%eax
  xor %edi,%edi
  syscall
.section .data.word,"aw",@progbits
.quad 0x12345678abcdef00
.section .note.fixture,"a",@note
.long 4,4,1
.ascii "TEST"
.long 0x12345678
ASM
as -o "$work/fixture.o" "$work/fixture.s"
cat >"$work/fixture.lds" <<'LDS'
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)
PHDRS {
  text PT_LOAD FLAGS(7);
  note PT_NOTE FLAGS(4);
}
SECTIONS {
  . = 0x10000;
  .head : { *(.head) }
  .text : { *(.text.entry) } :text
  . = ALIGN(0x20000);
  .data : { *(.data.word) }
  .note.fixture : { *(.note.fixture) } :note
}
LDS
ld -m elf_x86_64 -n -T "$work/fixture.lds" \
    -o "$work/gnu.elf" "$work/fixture.o"
"$MINILD" -melf_x86_64 -n -T "$work/fixture.lds" \
    -o "$work/mini.elf" "$work/fixture.o"
readelf -lW "$work/gnu.elf" >"$work/gnu.phdrs"
readelf -lW "$work/mini.elf" >"$work/mini.phdrs"

python3 - "$work/gnu.elf" "$work/mini.elf" <<'PY'
import struct
import sys


def parse(path):
    raw = open(path, 'rb').read()
    assert raw[:6] == b'\x7fELF\x02\x01'
    phoff = struct.unpack_from('<Q', raw, 32)[0]
    shoff = struct.unpack_from('<Q', raw, 40)[0]
    phentsize, phnum, shentsize, shnum, shstrndx = struct.unpack_from(
        '<HHHHH', raw, 54)
    phdrs = [struct.unpack_from('<IIQQQQQQ', raw, phoff + n * phentsize)
             for n in range(phnum)]
    shdrs = [struct.unpack_from('<IIQQQQIIQQ', raw, shoff + n * shentsize)
             for n in range(shnum)]
    shstr = shdrs[shstrndx]
    names = raw[shstr[4]:shstr[4] + shstr[5]]
    sections = {}
    for sh in shdrs:
        start = sh[0]
        name = names[start:names.find(b'\0', start)].decode()
        sections[name] = sh
    assert len(phdrs) == 2, (path, phdrs)
    assert [p[0] for p in phdrs] == [1, 4], (path, phdrs)
    assert [p[1] for p in phdrs] == [7, 4], (path, phdrs)
    for name, idx in [('.head', 0), ('.text', 0),
                      ('.data', 0), ('.note.fixture', 1)]:
        sec, ph = sections[name], phdrs[idx]
        assert ph[3] <= sec[3] and sec[3] + sec[5] <= ph[3] + ph[6], (
            path, name, ph, sec)
        assert ph[2] <= sec[4] and sec[4] + sec[5] <= ph[2] + ph[5], (
            path, name, ph, sec)
        assert raw[sec[4]:sec[4] + sec[5]], (path, name)
        if idx == 0:
            assert sec[4] - ph[2] == sec[3] - ph[3], (
                path, name, 'nonaffine-load', ph, sec)
    assert phdrs[0][7] == 1, (path, phdrs)
    assert sections['.data'][3] - sections['.text'][3] >= 0xf000, path
    gap = raw[sections['.text'][4] + sections['.text'][5]:
              sections['.data'][4]]
    assert len(gap) >= 0xf000 and not any(gap), (path, 'missing-zero-gap')
    return phdrs, sections


gnu_ph, gnu_sections = parse(sys.argv[1])
mini_ph, mini_sections = parse(sys.argv[2])
for name in ('.head', '.text', '.data', '.note.fixture'):
    assert mini_sections[name][3] == gnu_sections[name][3], name
print('MINILD_SCRIPT_PHDRS=PASS GNU_types_flags=true '
      'initial_default_LOAD=true text_data_inherit=true '
      'note_explicit=true 64KB_gap_affine_zero_filled=true GNU_addresses=true')
PY

for invalid in PT_DYNAMIC FILEHDR; do
    case "$invalid" in
        PT_DYNAMIC) declaration='text PT_DYNAMIC FLAGS(7);' ;;
        FILEHDR) declaration='text PT_LOAD FILEHDR PHDRS FLAGS(7);' ;;
    esac
    sed "s/text PT_LOAD FLAGS(7);/$declaration/" "$work/fixture.lds" \
        >"$work/invalid.lds"
    if "$MINILD" -melf_x86_64 -n -T "$work/invalid.lds" \
        -o "$work/rejected.elf" "$work/fixture.o" \
        2>"$work/invalid.stderr"; then
        echo "MINILD_SCRIPT_PHDRS=FAIL invalid=$invalid" >&2
        exit 1
    fi
    grep -q 'unsupported-static-script-PHDR-type-or-header' \
        "$work/invalid.stderr"
done
echo 'MINILD_SCRIPT_PHDRS_NEGATIVE=PASS dynamic_and_header_rejected=true'
