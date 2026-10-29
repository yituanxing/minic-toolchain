#!/usr/bin/env bash
# Parser ownership: a Section's complete key includes COMDAT and the *source*
# LINK_ORDER anchor, while numbered subsections are indexed by immutable
# SectionID. Compare the complete emitted payload/ELF attributes to GNU after
# thousands of collisions, section reopens, and subsection switches.
set -Eeuo pipefail
: "${MINIAS:?point MINIAS at the exact-head minic-as executable}"
for tool in as readelf python3; do command -v "$tool" >/dev/null; done
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

count=4096
{
    printf '.text\nanchor_one: nop\nanchor_two: nop\n'
    for ((i=0;i<count;++i)); do
        printf '.section .text.index_%04d,"ax",@progbits\n' "$i"
        printf '.byte %d\n' "$((i & 255))"
        printf '.subsection 2\n.byte %d\n' "$(((i + 17) & 255))"
        printf '.subsection 0\n.byte %d\n' "$(((i + 33) & 255))"
    done
    # Multiplication by odd 73 permutes every SectionID, so hot reopens
    # revisit every collision chain without creating duplicate Sections.
    for ((i=0;i<count * 2;++i)); do
        id=$(((i * 73) % count))
        printf '.pushsection .text.index_%04d,"ax",@progbits\n' "$id"
        printf '.subsection 2\n.byte %d\n.popsection\n' "$((i & 255))"
    done
    # A physical name does not imply one Section. Two signatures and two
    # original anchors form orthogonal identity dimensions.
    printf '.section .text.same,"axG",@progbits,group_one,comdat\n.byte 0x11\n'
    printf '.section .text.same,"axG",@progbits,group_two,comdat\n.byte 0x22\n'
    printf '.section .text.same,"axG",@progbits,group_one,comdat\n.byte 0x33\n'
    printf '.section .meta.mix,"aoG",@progbits,anchor_one,group_one,comdat\n.byte 0x41\n'
    printf '.section .meta.mix,"aoG",@progbits,anchor_two,group_one,comdat\n.byte 0x42\n'
    printf '.section .meta.mix,"aoG",@progbits,anchor_one,group_two,comdat\n.byte 0x43\n'
    printf '.section .meta.mix,"aoG",@progbits,anchor_one,group_one,comdat\n.byte 0x44\n'
    # NULL reference differs from the explicit no-anchor LINK_ORDER key in
    # MiniAS's established SectionKey contract. GNU's first declaration
    # instead absorbs the later "o" flag, so test this policy separately.
    printf '.section .meta.empty,"a",@progbits\n.byte 0x51\n'
    printf '.section .meta.empty,"ao",@progbits\n.byte 0x52\n'
    printf '.section .meta.empty,"a",@progbits\n.byte 0x53\n'
    # Repeated declarations preserve the first sh_entsize. A contradictory
    # entsize is a separate MiniAS policy check below: GNU rejects it.
    # SHF_STRINGS without SHF_MERGE still defaults sh_entsize=1 in GAS.
    printf '.section .rodata.strings_only,"aS",@progbits\n.asciz "hello"\n'
    printf '.section .rodata.merge,"aMS",@progbits,1\n.asciz "a"\n'
    printf '.section .rodata.merge,"aMS",@progbits,1\n.asciz "b"\n'
    printf '.section .rodata.merge,"aMS",@progbits,1\n.asciz "c"\n'
} >"$work/indexes.s"

{
    printf '.section .rodata.firstwins,"aMS",@progbits,1\n.asciz "a"\n'
    printf '.section .rodata.firstwins,"aMS",@progbits,2\n.asciz "b"\n'
} >"$work/firstwins.s"

"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/indexes.s"
"$MINIAS" --target=x86_64 -o "$work/firstwins-mini.o" "$work/firstwins.s"
if ! as --64 -o "$work/gnu.o" "$work/indexes.s" 2>"$work/gnu-warnings"; then
    sed -n '1,40p' "$work/gnu-warnings" >&2
    exit 1
fi
readelf -W -S "$work/mini.o" >"$work/mini-sections"
readelf -W -S "$work/gnu.o" >"$work/gnu-sections"
readelf --section-groups "$work/mini.o" >"$work/mini-groups"
readelf --section-groups "$work/gnu.o" >"$work/gnu-groups"

python3 - "$work" "$count" <<'PY'
import collections
import pathlib
import struct
import sys

root = pathlib.Path(sys.argv[1])
count = int(sys.argv[2])

def elf_sections(path):
    image = path.read_bytes()
    if image[:6] != b'\x7fELF\x02\x01':
        raise SystemExit(f'expected ELF64 little-endian {path}')
    header = struct.unpack_from('<16sHHIQQQIHHHHHH', image)
    offset, stride, number, strings = header[6], header[11], header[12], header[13]
    raw = [struct.unpack_from('<IIQQQQIIQQ', image, offset + i * stride)
           for i in range(number)]
    strings_sec = raw[strings]
    names = image[strings_sec[4]:strings_sec[4] + strings_sec[5]]
    result = []
    for entry in raw:
        name_pos = entry[0]
        end = names.find(b'\0', name_pos)
        name = names[name_pos:end].decode('ascii')
        kind, flags, pos, size, linked, alignment, entsize = (
            entry[1], entry[2], entry[4], entry[5], entry[6], entry[8], entry[9])
        data = b'' if kind == 8 else image[pos:pos + size]
        result.append((name, kind, flags, size, linked, alignment, entsize, data))
    return result

def selected(sections):
    interesting = ('.text.index_', '.text.same', '.meta.mix',
                   '.rodata.merge', '.rodata.strings_only')
    return sorted((name, kind, flags, size, alignment, entsize, data,
                   sections[linked][0] if linked and (flags & 128) else '')
                  for name, kind, flags, size, linked, alignment, entsize, data
                  in sections if name.startswith(interesting))

mini = elf_sections(root / 'mini.o')
gnu = elf_sections(root / 'gnu.o')
firstwins = elf_sections(root / 'firstwins-mini.o')
ours, expected = selected(mini), selected(gnu)
if ours != expected:
    for index, pair in enumerate(zip(ours, expected)):
        if pair[0] != pair[1]:
            raise SystemExit(f'ELF section mismatch at {index}: {pair!r}')
    raise SystemExit(f'ELF section count mismatch: MiniAS={len(ours)} GNU={len(expected)}')

only_strings = [entry for entry in ours
                if entry[0] == '.rodata.strings_only']
if len(only_strings) != 1 or only_strings[0][5] != 1 or \
   only_strings[0][6] != b'hello\0':
    raise SystemExit(f'S-only GNU sh_entsize default mismatch: {only_strings}')

counts = collections.Counter(name for name, *_ in ours)
if sum(name.startswith('.text.index_') for name in counts) != count:
    raise SystemExit('4096 unique sections collapsed or lost')
if any(counts[f'.text.index_{i:04d}'] != 1 for i in range(count)):
    raise SystemExit('duplicate unique Section key')
if counts['.text.same'] != 2 or counts['.meta.mix'] != 3 or \
   counts['.rodata.merge'] != 1:
    raise SystemExit(f'cross-key identity failed: {counts}')
first = [entry for entry in firstwins if entry[0] == '.rodata.firstwins']
if len(first) != 1 or first[0][6] != 1 or first[0][7] != b'a\0b\0':
    raise SystemExit('first section declaration did not retain its entsize')
if sorted(data for name, *_, data, linked in ours if name == '.text.same') != \
   [b'\x11\x33', b'\x22']:
    raise SystemExit('COMDAT reopen appended to wrong same-named Section')
if sorted(data for name, *_, data, linked in ours if name == '.meta.mix') != \
   [b'\x41\x44', b'\x42', b'\x43']:
    raise SystemExit('LINK_ORDER/COMDAT cross-product lost identity')
if sorted(entry[7] for entry in mini if entry[0] == '.meta.empty') != \
   [b'\x51\x53', b'\x52']:
    raise SystemExit('NULL SHF_LINK_ORDER key aliased empty-string key')
for i in range(count):
    chunk = next(data for name, *_, data, linked in ours
                 if name == f'.text.index_{i:04d}')
    if len(chunk) != 5 or chunk[:2] != bytes([i & 255, (i + 33) & 255]):
        raise SystemExit(f'hot Section/Subsection reselect changed payload: {i}')

# The initial table has 16 slots and the 4096 unique names necessarily
# collide many times; all retained values must still compare GNU-equal.
print('MINIAS_SECTION_SUBSECTION_KEY_INDEX=PASS '
      f'unique_sections={count} parser_subsections={count * 2} '
      f'hot_push_pop={count * 2} comdat_same_name=2 '
      'link_order_cross_product=3 null_vs_empty=2 '
      'merge_first_entsize=1 GNU_ELF_section_payload=exact')
PY
