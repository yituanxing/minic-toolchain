#!/usr/bin/env bash
# RV32/RV64 typed LI: the measured MCInst size must equal the emitted words.
# GNU GAS may choose a shorter LI algorithm, so compare executed architectural
# values rather than demanding identical machine-byte sequences for multiword
# constants. Only single-ADDI cases have a mandatory GNU byte oracle.
set -Eeuo pipefail
: "${MINIAS:?exact-head MiniAS binary required}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
trap 'echo "RV_TYPED_LI_FAIL command=$BASH_COMMAND" >&2' ERR

case_specs=(
    'zero|0|0000000000000000'
    'one|1|0000000000000001'
    'minus_one|-1|ffffffffffffffff'
    'plus_2047|2047|00000000000007ff'
    'plus_2048|2048|0000000000000800'
    'minus_2048|-2048|fffffffffffff800'
    'minus_2049|-2049|fffffffffffff7ff'
    'int32_max|2147483647|000000007fffffff'
    'int32_min|-2147483648|ffffffff80000000'
    'u32_sign_bit|0x80000000|0000000080000000'
)
rv64_extra=(
    'bit32|0x100000000|0000000100000000'
    'bit63|0x8000000000000000|8000000000000000'
    'all_ones|0xffffffffffffffff|ffffffffffffffff'
    'sparse_high|0x0100000000000001|0100000000000001'
    'dense_bytes|0x0123456789abcdef|0123456789abcdef'
    'signed64_min|-9223372036854775808|8000000000000000'
    'signed64_max|9223372036854775807|7fffffffffffffff'
)

gnu_available=1
for tool in riscv64-linux-gnu-as readelf python3; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        if [ "$tool" = readelf ] || [ "$tool" = python3 ]; then
            echo "Missing required local oracle: $tool" >&2
            exit 1
        fi
        gnu_available=0
    fi
done

for xlen in 32 64; do
    spec=("${case_specs[@]}")
    if [ "$xlen" = 64 ]; then
        spec+=("${rv64_extra[@]}")
        abi=lp64
    else
        abi=ilp32
    fi
    {
        echo '.text'
        echo '.option norvc'
        for entry in "${spec[@]}"; do
            IFS='|' read -r name literal expected <<<"$entry"
            printf '.globl li_case_%s\n.type li_case_%s,@function\n' "$name" "$name"
            printf 'li_case_%s:\n    li t0,%s\n' "$name" "$literal"
            printf '.size li_case_%s,.-li_case_%s\n' "$name" "$name"
        done
    } >"$work/rv$xlen.s"
    for entry in "${spec[@]}"; do
        IFS='|' read -r name literal expected <<<"$entry"
        printf '%s %s\n' "$name" "$expected"
    done >"$work/rv$xlen.expected"
    "$MINIAS" --target=riscv "-march=rv${xlen}i" "-mabi=$abi" \
        -o "$work/rv$xlen.mini.o" "$work/rv$xlen.s"
    if [ "$gnu_available" -eq 1 ]; then
        riscv64-linux-gnu-as "-march=rv${xlen}i" "-mabi=$abi" \
            -o "$work/rv$xlen.gnu.o" "$work/rv$xlen.s"
    fi
done

# RV32 rejects values that neither fit in 32 bits nor equal the sign
# extension of their low word. Accepting these would silently truncate LI.
for invalid in 0x100000000 -2147483649; do
    printf '.text\nli t0,%s\n' "$invalid" >"$work/rv32.invalid.s"
    if "$MINIAS" --target=riscv -march=rv32i -mabi=ilp32 \
        -o "$work/rv32.invalid.o" "$work/rv32.invalid.s" 2>"$work/rv32.invalid.err"; then
        echo "RV32 wrongly truncated LI immediate: $invalid" >&2
        exit 1
    fi
    grep -q 'rv32i-li-overflows-32-bits' "$work/rv32.invalid.err"
done

python3 - "$work" "$gnu_available" <<'PY'
from pathlib import Path
import re
import struct
import subprocess
import sys

work, gnu_available = Path(sys.argv[1]), int(sys.argv[2])

def signed(value, bits):
    value &= (1 << bits) - 1
    return value - (1 << bits) if value & (1 << (bits - 1)) else value

def elf_text(path):
    dump = subprocess.check_output(['readelf', '-x', '.text', str(path)], text=True)
    buf = bytearray()
    for line in dump.splitlines():
        match = re.match(r'^\s*0x[0-9a-fA-F]+\s+(.+)$', line)
        if not match:
            continue
        for field in match.group(1).split():
            if not re.fullmatch(r'[0-9a-fA-F]{8}', field):
                break
            buf += bytes.fromhex(field)
    return bytes(buf)

def elf_cases(path):
    listing = subprocess.check_output(['readelf', '-sW', str(path)], text=True)
    cases = {}
    for line in listing.splitlines():
        match = re.match(
            r'^\s*\d+:\s+([0-9a-fA-F]+)\s+(\d+)\s+\S+\s+\S+\s+\S+\s+\S+\s+li_case_(\w+)\s*$',
            line)
        if match:
            offset, size, name = match.groups()
            if name in cases:
                raise AssertionError(f'duplicate symbol {name}: {path}')
            cases[name] = int(offset, 16), int(size)
    return cases

def execute_li(code, xlen, name):
    assert len(code) in range(4, 65, 4), (name, xlen, len(code))
    mask = (1 << xlen) - 1
    registers = [0] * 32
    for (word,) in struct.iter_unpack('<I', code):
        opc, rd = word & 127, (word >> 7) & 31
        f3, rs1 = (word >> 12) & 7, (word >> 15) & 31
        immediate = signed(word >> 20, 12)
        if opc == 0x37:  # LUI, sign-extended on RV64
            result = signed(word & 0xfffff000, 32)
        elif opc == 0x13 and f3 == 0:  # ADDI
            result = registers[rs1] + immediate
        elif opc == 0x13 and f3 == 1:  # SLLI
            shift = (word >> 20) & (63 if xlen == 64 else 31)
            result = registers[rs1] << shift
        elif opc == 0x13 and f3 == 6:  # ORI (GNU variant)
            result = registers[rs1] | (immediate & mask)
        elif opc == 0x1b and f3 == 0 and xlen == 64:  # ADDIW
            result = signed(registers[rs1] + immediate, 32)
        else:
            raise AssertionError(f'unsupported LI opcode {word:08x}, {name} rv{xlen}')
        if rd != 5:
            raise AssertionError(f'LI unexpectedly writes x{rd} in {name}')
        registers[rd] = result & mask
    return registers[5], len(code) // 4

total = 0
gnu_checks = 0
byte_checks = 0
for xlen in (32, 64):
    expected = dict(line.split() for line in (work / f'rv{xlen}.expected').read_text().splitlines())
    objects = {'mini': work / f'rv{xlen}.mini.o'}
    if gnu_available:
        objects['gnu'] = work / f'rv{xlen}.gnu.o'
    views = {key: (elf_text(path), elf_cases(path)) for key, path in objects.items()}
    for name, hex_value in expected.items():
        results = {}
        emitted = {}
        for tool, (text, cases) in views.items():
            if name not in cases:
                raise AssertionError(f'missing li_case_{name} in {tool} rv{xlen}')
            off, size = cases[name]
            if off % 4 or size % 4 or size == 0 or off + size > len(text):
                raise AssertionError(f'bad symbol size/offset {tool} rv{xlen} {name}: {off} {size}')
            chunk = text[off:off + size]
            got, words = execute_li(chunk, xlen, name)
            want = int(hex_value, 16) & ((1 << xlen) - 1)
            if got != want or words * 4 != size:
                raise AssertionError(f'wrong LI {tool} rv{xlen} {name}: got={got:x} want={want:x} size={size}')
            results[tool] = got
            emitted[tool] = chunk
        if gnu_available:
            assert results['mini'] == results['gnu']
            gnu_checks += 1
            if name in ('zero', 'one', 'minus_one', 'plus_2047', 'minus_2048'):
                if emitted['mini'] != emitted['gnu']:
                    raise AssertionError(f'single ADDI byte mismatch rv{xlen} {name}')
                byte_checks += 1
        total += 1
print(f'MINIAS_RV_TYPED_LI=PASS mini_semantics={total} '
      f'measure_encode_consistent={total} '
      f'GNU_semantics={gnu_checks if gnu_available else "SKIP(no-cross-as)"} '
      f'GNU_exact_single_addi={byte_checks if gnu_available else "SKIP(no-cross-as)"}')
PY
