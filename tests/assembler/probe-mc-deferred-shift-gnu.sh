#!/usr/bin/env bash
# Same-section symbolic differences enter the deferred MCExpr evaluator,
# whereas numeric-only operands use the eager expression evaluator. Both
# must agree with GNU GAS on logical 64-bit shifts, including >> 63.
set -Eeuo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
: "${MINIAS:=$project_root/build/release/bin/minic-as}"
test -x "$MINIAS"
for tool in as readelf python3; do command -v "$tool" >/dev/null; done

work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

cat >"$work/deferred-shift.s" <<'ASM'
.data
first:
    .byte 0x11
second:
    .byte 0x22
    .space ((first-second) >> 63) + 1, 0x7e
    .fill ((first-second) >> 62), 1, 0x5a
    .uleb128 ((first-second) >> 63) + 5
    .org ((first-second) >> 63) + 12, 0x55
    .byte 0x99
ASM

as -o "$work/gnu.o" "$work/deferred-shift.s"
for target in x86_64 riscv aarch64 loongarch64; do
    "$MINIAS" --target="$target" -o "$work/$target.o" \
        "$work/deferred-shift.s"
done

python3 - "$work/gnu.o" "$work/x86_64.o" "$work/riscv.o" \
    "$work/aarch64.o" "$work/loongarch64.o" <<'PY'
from pathlib import Path
import re
import subprocess
import sys

def data(path):
    sections = subprocess.check_output(['readelf', '-SW', path], text=True)
    match = re.search(
        r'^\s*\[\s*\d+\]\s+\.data\s+PROGBITS\s+'
        r'[0-9a-fA-F]+\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\b',
        sections, re.M)
    if match is None:
        raise AssertionError(f'missing ELF .data section: {path}')
    start, size = (int(v, 16) for v in match.groups())
    contents = Path(path).read_bytes()
    if start + size > len(contents):
        raise AssertionError(f'ELF section extends beyond file: {path}')
    return contents[start:start + size]

expected = bytes.fromhex('11 22 7e 7e 5a 5a 5a 06 55 55 55 55 55 99')
reference = data(sys.argv[1])
if reference != expected:
    raise AssertionError(f'GNU data={reference.hex()} != {expected.hex()}')
for path in sys.argv[2:]:
    actual = data(path)
    if actual != reference:
        raise AssertionError(f'{path}: {actual.hex()} != {reference.hex()}')
print('MINIAS_DEFERRED_SHIFT_GNU=PASS targets=4 data_bytes=14')
PY

# Different section identities are not a constant delta, even if their
# currently assigned numeric values happen to match. A shift cannot erase
# the underlying relocation and must fail closed with no output object.
cat >"$work/cross-section.s" <<'ASM'
.data
first:
    .byte 0
.text
other:
    .byte 0
.data
    .space ((first-other) >> 63) + 1, 0
ASM
for target in x86_64 riscv aarch64 loongarch64; do
    output="$work/$target.cross-section.o"
    if "$MINIAS" --target="$target" -o "$output" \
        "$work/cross-section.s" >"$work/$target.cross-section.log" 2>&1; then
        echo "cross-section symbolic shift unexpectedly accepted: $target" >&2
        exit 1
    fi
    if test -e "$output"; then
        echo "failed symbolic shift left an ELF object: $target" >&2
        exit 1
    fi
done

echo 'MINIAS_DEFERRED_SHIFT_RELOCATION_GUARD=PASS targets=4'
