#!/usr/bin/env bash
# One host-GNU oracle for the shared MC expression grammar.  All four MiniAS
# targets assemble identical, instruction-free data, so this requires no
# cross assembler, target linker, emulator, or Linux corpus.
set -Eeuo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
: "${MINIAS:=$project_root/build/release/bin/minic-as}"
test -x "$MINIAS"
command -v as >/dev/null
command -v readelf >/dev/null
command -v python3 >/dev/null

work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

cat >"$work/grammar.s" <<'ASM'
.data
.set repeat_count, 3
.set skip_count, 3
.globl shared_expression_grammar
shared_expression_grammar:
    .byte (3 && 2)
    .byte (3 & 2)
    .byte (2 || 4)
    .byte (2 | 4)
    .byte (!0)
    .byte (~0)
    .byte ((2 + 3) * 4)
    .byte (17 << 2)
    .byte (((-16) >> 2) & 0xff)
    .byte (16 >> 2)
    .long (4 < 7)
    .long (4 == 7)
    .long ((4 + 3) == 7)
    .long ((3 << 4) & 0x3f)
    .quad ((-16) >> 2)
    .quad 0xffffffffffffffff
    .quad (0xffffffffffffffff + 2)
    .fill (repeat_count + 0), (1 + 1), (0x1234 | 0x08)
    .space (skip_count + 1), 0x5a
    # GNU absolute comparisons are signed predicates over unsigned
    # bit-pattern shifts. The AArch64 Linux mov_q conditional depends on this.
    .if (((~(1 << 20)) >> 47) == 0x1ffff)
        .byte 0xa1
    .else
        .byte 0xa2
    .endif
    .if (((-14) >> 3) == 0x1ffffffffffffffe)
        .byte 0xb1
    .else
        .byte 0xb2
    .endif
ASM

as -o "$work/gnu.o" "$work/grammar.s"
for target in x86_64 riscv aarch64 loongarch64; do
    "$MINIAS" --target="$target" -o "$work/$target.o" "$work/grammar.s"
done

# Parse real ELF section file ranges rather than comparing readelf's visual
# hexdump (which varies by binutils version) or trusting symbol-name grep.
# An explicit independently calculated byte oracle also proves the GNU input
# exercises boolean/bitwise separation, signed shifts and 64-bit wrapping.
python3 - "$work/gnu.o" "$work/x86_64.o" "$work/riscv.o" \
    "$work/aarch64.o" "$work/loongarch64.o" <<'PY'
import pathlib
import re
import subprocess
import sys

def section_bytes(path):
    sections = subprocess.check_output(
        ['readelf', '-SW', path], text=True)
    match = re.search(
        r'^\s*\[\s*\d+\]\s+\.data\s+PROGBITS\s+'
        r'[0-9a-fA-F]+\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\b',
        sections, flags=re.MULTILINE)
    if match is None:
        raise AssertionError(f'missing ELF .data section: {path}')
    offset, size = (int(v, 16) for v in match.groups())
    blob = pathlib.Path(path).read_bytes()
    if offset + size > len(blob):
        raise AssertionError(f'truncated ELF .data section: {path}')
    return blob[offset:offset + size]

expected = (
    bytes.fromhex('01 02 01 06 01 ff 14 44 fc 04')
    + bytes.fromhex('ff ff ff ff 00 00 00 00 ff ff ff ff 30 00 00 00')
    + bytes.fromhex('fc ff ff ff ff ff ff 3f')
    + b'\xff' * 8 + b'\x01' + b'\x00' * 7
    + bytes.fromhex('3c 12') * 3 + b'\x5a' * 4
    + bytes.fromhex('a1 b1')
)
oracle = section_bytes(sys.argv[1])
if oracle != expected:
    raise AssertionError(
        f'host GNU as oracle changed: got={oracle.hex()} '
        f'expected={expected.hex()}')
for path in sys.argv[2:]:
    actual = section_bytes(path)
    if actual != oracle:
        raise AssertionError(
            f'{path}: .data differs from GNU as: got={actual.hex()} '
            f'expected={oracle.hex()}')
print(f'MINIAS_MC_SHARED_GRAMMAR_DATA=PASS targets=4 bytes={len(oracle)}')
PY

# In contrast to wrapping absolute .quad values, signed deferred fragment
# layout must reject 2^63-1 + 1.  Division by zero must likewise fail.
# Both errors occur after a valid data fragment, so neither may leave a
# partially emitted ELF object behind.
cat >"$work/overflow.s" <<'ASM'
.data
.byte 0x7e
.set signed_limit, 0x7fffffffffffffff
.space (signed_limit + 1), 0
ASM
cat >"$work/zero-divisor.s" <<'ASM'
.data
.byte 0x7e
.fill (4 / 0), 1, 0
ASM
for target in x86_64 riscv aarch64 loongarch64; do
    for test_case in overflow zero-divisor; do
        output="$work/$target.$test_case.o"
        if "$MINIAS" --target="$target" -o "$output" \
            "$work/$test_case.s" >"$work/$target.$test_case.log" 2>&1; then
            echo "invalid expression accepted: $target/$test_case" >&2
            exit 1
        fi
        if test -e "$output"; then
            echo "failed expression left an ELF object: $target/$test_case" >&2
            exit 1
        fi
    done
done

echo 'MINIAS_MC_SHARED_GRAMMAR=PASS oracle=host-gnu-as targets=4 negatives=8'
