#!/usr/bin/env bash
# Stress the ELF ObjectModel's section-name association plan. Every metadata
# section has an independent GNU SHF_LINK_ORDER target but no same-named
# STT_SECTION symbol. A linear scan per section is quadratic at this scale.
set -Eeuo pipefail
: "${MINIAS:?point MINIAS at the exact-head minic-as executable}"
for tool in as readelf python3; do command -v "$tool" >/dev/null; done
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

count=1024
{
    for ((i=0;i<count;++i)); do
        printf '.section .text.link_%04d,"ax",@progbits\n' "$i"
        printf 'nop\n'
        printf '.section .meta.link_%04d,"ao",@progbits,.text.link_%04d\n' "$i" "$i"
        printf '.long other_object\n'
    done
} >"$work/many-link-order.s"

"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/many-link-order.s"
as --64 -o "$work/gnu.o" "$work/many-link-order.s"
readelf -W -S "$work/mini.o" >"$work/mini.sections"
readelf -W -S "$work/gnu.o" >"$work/gnu.sections"
readelf -W -r "$work/mini.o" >"$work/mini.relocations"
readelf -W -r "$work/gnu.o" >"$work/gnu.relocations"

python3 - "$work" "$count" <<'PY'
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1])
count = int(sys.argv[2])
section_pattern = re.compile(
    r'^\s*\[\s*(\d+)\]\s+(\S+)\s+\S+\s+\S+\s+\S+\s+'
    r'\S+\s+\S+\s+([A-Z]+)\s+(\d+)\s+\d+\s+\d+\s*$')

def plan(producer):
    sections = {}
    for line in (root / f'{producer}.sections').read_text().splitlines():
        match = section_pattern.match(line)
        if match:
            index, name, flags, link = match.groups()
            sections[name] = (int(index), flags, int(link))
    relations = {}
    for n in range(count):
        text_name = f'.text.link_{n:04d}'
        meta_name = f'.meta.link_{n:04d}'
        if text_name not in sections or meta_name not in sections:
            raise SystemExit(f'{producer}: missing link-order section {n}')
        text = sections[text_name]
        meta = sections[meta_name]
        if text[1] != 'AX' or meta[1] != 'AL' or meta[2] != text[0]:
            raise SystemExit(f'{producer}: wrong sh_link at {n}: {text}, {meta}')
        relations[meta_name] = text_name
    relocations = (root / f'{producer}.relocations').read_text()
    if relocations.count('R_X86_64_32') != count:
        raise SystemExit(f'{producer}: expected {count} relocations')
    return relations

mini, gnu = plan('mini'), plan('gnu')
if mini != gnu:
    raise SystemExit('MiniAS and GNU link-order association plans differ')
print(f'MINIAS_ELF_LINK_ORDER_INDEX=PASS section_pairs={count} '
      f'external_relocations={count} GNU_sh_link=exact')
PY
