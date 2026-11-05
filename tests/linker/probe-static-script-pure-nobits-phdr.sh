#!/usr/bin/env bash
# A stand-alone SHT_NOBITS PT_LOAD is valid even when its VMA is not page-aligned.
set -Eeuo pipefail
: "${MINILD:?MINILD must point to minic-ld}"
for exe in as ld readelf python3; do
    command -v "$exe" >/dev/null || {
        echo "MINILD_PURE_NOBITS_PHDR=INCONCLUSIVE missing=$exe" >&2
        exit 8
    }
done

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
fixture="$here/fixtures/x86-pure-nobits-phdr"
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-pure-nobits-phdr.XXXXXX")
cleanup() {
    if [[ ${MINILD_PURE_NOBITS_PHDR_KEEP:-0} == 1 ]]; then
        echo "MINILD_PURE_NOBITS_PHDR_ARTIFACTS=$work"
    else
        rm -r -- "$work"
    fi
}
trap cleanup EXIT

as -o "$work/fixture.o" "$fixture.S"
ld -m elf_x86_64 -T "$fixture.lds" -o "$work/gnu.elf" "$work/fixture.o"
"$MINILD" -melf_x86_64 -T "$fixture.lds" \
    -o "$work/mini.elf" "$work/fixture.o"

python3 - "$work/gnu.elf" "$work/mini.elf" <<'PY'
import struct
import sys


def parse(path):
    raw = open(path, 'rb').read()
    assert raw[:6] == b'\x7fELF\x02\x01', path
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
    for sec in shdrs:
        off = sec[0]
        sections[names[off:names.find(b'\0', off)].decode()] = sec
    assert len(phdrs) == 2, (path, phdrs)
    assert [p[0] for p in phdrs] == [1, 1], (path, phdrs)
    bss = sections['.bss']
    segment = phdrs[1]
    assert bss[1] == 8 and bss[3] == 0x21008 and bss[5] == 80, (
        path, 'bss-section', bss)
    assert segment[3] == 0x21008 and segment[5] == 0 and segment[6] == 80, (
        path, 'bss-segment', segment)
    assert segment[2] % segment[7] == segment[3] % segment[7], (
        path, 'page-congruence', segment)
    return bss, segment


gnu_bss, gnu_seg = parse(sys.argv[1])
mini_bss, mini_seg = parse(sys.argv[2])
assert mini_seg[7] == gnu_seg[7], (mini_seg, gnu_seg)
print('MINILD_PURE_NOBITS_PHDR=PASS unaligned_vma=true p_filesz=0 '
      'p_memsz=80 sh_type=NOBITS page_congruence=true GNU_match=true')
PY
