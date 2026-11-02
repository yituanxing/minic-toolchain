#!/usr/bin/env bash
# Keep the Target symbol+absolute-suffix adapter on the shared grammar without
# changing GNU .set/.equ, dot anchors or x86 immediate relocation/addend bits.
# The legacy `symbol + 4 << 2` API precedence is separately pinned by the
# direct C syntax probe: GNU MCData is intentionally a different policy.
set -Eeuo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
: "${MINIAS:=$project_root/build/release/bin/minic-as}"
test -x "$MINIAS"
for tool in as readelf objcopy cmp python3; do command -v "$tool" >/dev/null; done

work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

cat >"$work/aliases.s" <<'ASM'
.data
entry:
    .byte 0x5a
.set dot_anchor, . + 0
.equ eq_alias, ((entry + ((5 * 3) - 7)))
.set set_alias, ((entry + (18 - 7)))
    .quad eq_alias - entry
    .quad set_alias - entry
    .quad dot_anchor - entry
    .quad (0xffffffffffffffff + 2)
    .quad ((-16) >> 2)
ASM
as --64 -o "$work/gnu-aliases.o" "$work/aliases.s"
for target in x86_64 riscv aarch64 loongarch64; do
    "$MINIAS" --target="$target" -o "$work/$target-aliases.o" \
        "$work/aliases.s"
done

python3 - "$work/gnu-aliases.o" "$work/x86_64-aliases.o" \
    "$work/riscv-aliases.o" "$work/aarch64-aliases.o" \
    "$work/loongarch64-aliases.o" <<'PY'
from pathlib import Path
import re
import struct
import subprocess
import sys

def section_data(path):
    sections = subprocess.check_output(['readelf', '-SW', path], text=True)
    m = re.search(r'^\s*\[\s*\d+\]\s+\.data\s+PROGBITS\s+'
                  r'[0-9a-fA-F]+\s+([0-9a-fA-F]+)\s+'
                  r'([0-9a-fA-F]+)\b', sections, re.M)
    if not m:
        raise AssertionError(f'missing .data in {path}')
    offset, size = (int(s, 16) for s in m.groups())
    return Path(path).read_bytes()[offset:offset + size]

data = section_data(sys.argv[1])
values = struct.unpack('<5Q', data[1:])
expected = (8, 11, 1, 1, 0x3ffffffffffffffc)
if data[0] != 0x5a or len(data) != 41 or values != expected:
    raise SystemExit(f'GNU .set/.equ/dot oracle moved: {data.hex()}')
pair_expected = bytes([0x5a]) + bytes(24) + data[25:]
failures = []
for path in sys.argv[2:]:
    actual = section_data(path)
    # RISC-V and LoongArch intentionally retain ADD64/SUB64 fixups even for
    # same-section symbol differences; x86-64 and AArch64 fold them here.
    pair = 'riscv-' in path or 'loongarch64-' in path
    expected_data = pair_expected if pair else data
    if actual != expected_data:
        failures.append(f'{path}: MiniAS DATA: {actual.hex()} '
                        f'!= {expected_data.hex()}')
    if pair:
        records = subprocess.check_output(['readelf', '-Wr', path], text=True)
        prefix = 'RISCV' if 'riscv-' in path else 'LARCH'
        if f'R_{prefix}_ADD64' not in records or f'R_{prefix}_SUB64' not in records:
            failures.append(f'{path}: missing target ADD64/SUB64 fixups')
if failures:
    raise SystemExit('\n'.join(failures))
PY

cat >"$work/reloc.s" <<'ASM'
.text
.globl suffix_reloc
.type suffix_reloc,@function
suffix_reloc:
    addq $(external_expression - 0xffffffff80000000), %rax
    addq $(external_expression + (4 << 2)), %r8
    addq $(external_expression - 4 + 2), %rax
ASM
as --64 -o "$work/gnu-reloc.o" "$work/reloc.s"
"$MINIAS" --target=x86_64 -o "$work/mini-reloc.o" "$work/reloc.s"
for kind in mini gnu; do
    objcopy -j .text -O binary "$work/$kind-reloc.o" \
        "$work/$kind-reloc.bin"
done
cmp "$work/gnu-reloc.bin" "$work/mini-reloc.bin"

python3 - "$work/mini-reloc.o" "$work/gnu-reloc.o" <<'PY'
import re
import subprocess
import sys

def relocations(path):
    rows = []
    for line in subprocess.check_output(['readelf', '-Wr', path], text=True).splitlines():
        m = re.match(r'^\s*([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+'
                     r'(R_X86_64_\w+)\s+[0-9a-fA-F]+\s+'
                     r'(external_expression)\s+([+-])\s+(\S+)', line)
        if m:
            rows.append((int(m[1], 16), m[2], m[3], m[4], m[5]))
    return rows

mini, gnu = map(relocations, sys.argv[1:])
if mini != gnu or len(mini) != 3:
    raise SystemExit(f'x86 symbol-suffix GNU relocations differ: {mini} != {gnu}')
print('MINIAS_SYMBOL_SUFFIX_GNU=PASS targets=4 aliases=3 '
      'dot=1 x86_bytes=exact x86_relocations=3')
PY
