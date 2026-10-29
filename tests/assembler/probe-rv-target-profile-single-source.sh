#!/usr/bin/env bash
# RISC-V ISA profile data is semantic (not a prebuilt vendor ELF blob).
# Tag_File is serialized once and ULEB values must not be truncated to one
# byte when directives appear in arbitrary source order.
set -Eeuo pipefail
: "${MINIAS:?exact-head MiniAS binary required}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

for xlen in 32 64; do
    if [ "$xlen" = 32 ]; then abi=ilp32; else abi=lp64; fi
    for stack in 128 256 16384 4294967295; do
        file="$work/rv${xlen}-${stack}.s"
        printf '.attribute 6,1\n.attribute 4,%s\n.attribute arch,"rv%si2p1"\n.text\n.globl f\nf:\n nop\n' \
            "$stack" "$xlen" >"$file"
        "$MINIAS" --target=riscv "-march=rv${xlen}i" "-mabi=$abi" \
            -o "$work/rv${xlen}-${stack}.o" "$file"
    done
    printf '.attribute 4,4294967296\n.text\n' >"$work/overflow.s"
    if "$MINIAS" --target=riscv "-march=rv${xlen}i" "-mabi=$abi" \
       -o "$work/overflow.o" "$work/overflow.s" \
       2>"$work/overflow.err"; then
        echo 'RISC-V attribute numeric width was silently truncated' >&2
        exit 1
    fi
    grep -q 'unsupported-riscv-attribute' "$work/overflow.err"
done

python3 - "$work" <<'PY'
from pathlib import Path
import re
import struct
import subprocess
import sys

root = Path(sys.argv[1])

def uleb(value):
    out = bytearray()
    while True:
        next_ = value & 0x7f
        value >>= 7
        out.append(next_ | (0x80 if value else 0))
        if not value:
            return bytes(out)

def section(path):
    output = subprocess.check_output(['readelf', '-x', '.riscv.attributes',
                                      str(path)], text=True)
    data = bytearray()
    for row in output.splitlines():
        match = re.match(r'^\s*0x[0-9a-fA-F]+\s+(.+)$', row)
        if match:
            for field in match.group(1).split():
                if not re.fullmatch(r'(?:[0-9a-fA-F]{2}){1,4}', field):
                    break
                data.extend(bytes.fromhex(field))
    return bytes(data)

count = 0
for xlen in (32, 64):
    for stack in (128, 256, 16384, 4294967295):
        payload = b'\x04' + uleb(stack) + b'\x05' + \
                  f'rv{xlen}i2p1'.encode() + b'\0\x06\x01'
        size = 16 + len(payload)
        expected = b'A' + struct.pack('<I', size - 1) + b'riscv\0' + \
                   b'\x01' + struct.pack('<I', size - 11) + payload
        path = root / f'rv{xlen}-{stack}.o'
        actual = section(path)
        assert actual == expected, (xlen, stack, actual.hex(), expected.hex())
        count += 1
print(f'MINIAS_RV_TARGET_PROFILE_SINGLE_SOURCE=PASS byte_exact={count} '
      'uleb_multiples=4 overflow_rejected=2')
PY
