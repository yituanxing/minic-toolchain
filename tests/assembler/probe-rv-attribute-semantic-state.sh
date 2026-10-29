#!/usr/bin/env bash
# RISC-V ABI attributes are target semantic state, not mutable offsets in an
# already serialized ELF blob. GNU's canonical Tag_File order is stable even
# if the source directives arrive in reverse order.
set -Eeuo pipefail
: "${MINIAS:?exact-head MiniAS binary required}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

for xlen in 32 64; do
    if [ "$xlen" = 32 ]; then abi=ilp32; else abi=lp64; fi
    for order in ascending descending arch_first; do
        file="$work/rv${xlen}.${order}.s"
        case "$order" in
            ascending)
                printf '.attribute 4,16\n.attribute 5,"rv%si2p1"\n.attribute 6,1\n' "$xlen" >"$file" ;;
            descending)
                printf '.attribute 6,1\n.attribute 4,16\n.attribute 5,"rv%si2p1"\n' "$xlen" >"$file" ;;
            arch_first)
                printf '.attribute arch,"rv%si2p1"\n.attribute unaligned_access,1\n.attribute stack_align,16\n' "$xlen" >"$file" ;;
        esac
        printf '.text\n.globl abi_probe\nabi_probe:\n  nop\n' >>"$file"
        "$MINIAS" --target=riscv "-march=rv${xlen}i" "-mabi=$abi" \
            -o "$work/rv${xlen}.${order}.o" "$file"
    done

    # Idempotent duplicate attributes must not alter the binary.
    printf '.attribute 4,16\n.attribute 6,1\n.attribute 4,16\n.attribute 6,1\n.text\n' \
        >"$work/rv${xlen}.duplicate.s"
    "$MINIAS" --target=riscv "-march=rv${xlen}i" "-mabi=$abi" \
        -o "$work/rv${xlen}.duplicate.o" "$work/rv${xlen}.duplicate.s"
    printf '.attribute 4,16\n.attribute 4,32\n.text\n' >"$work/rv${xlen}.conflict.s"
    if "$MINIAS" --target=riscv "-march=rv${xlen}i" "-mabi=$abi" \
            -o "$work/rv${xlen}.conflict.o" "$work/rv${xlen}.conflict.s" \
            2>"$work/rv${xlen}.conflict.err"; then
        echo "RISC-V attribute conflict was accepted" >&2
        exit 1
    fi
    grep -q 'conflicting-riscv-attribute:4' "$work/rv${xlen}.conflict.err"
    printf '.text\ncsrr a0, (0x300|0x4)\ncsrr a0, 0x300+4\n' \
        >"$work/rv${xlen}.csr.s"
    "$MINIAS" --target=riscv "-march=rv${xlen}i_zicsr" "-mabi=$abi" \
        -o "$work/rv${xlen}.csr.o" "$work/rv${xlen}.csr.s"
    printf '.text\ncsrr a0, 0x1000\n' >"$work/rv${xlen}.csr-invalid.s"
    if "$MINIAS" --target=riscv "-march=rv${xlen}i_zicsr" "-mabi=$abi" \
            -o "$work/rv${xlen}.csr-invalid.o" "$work/rv${xlen}.csr-invalid.s" \
            2>"$work/rv${xlen}.csr-invalid.err"; then
        echo "RISC-V CSR greater than 12 bits was accepted" >&2
        exit 1
    fi
done

python3 - "$work" <<'PY'
from pathlib import Path
import re
import struct
import subprocess
import sys

work = Path(sys.argv[1])

def dump(path, section):
    output = subprocess.check_output(['readelf', '-x', section, str(path)], text=True)
    result = bytearray()
    for line in output.splitlines():
        match = re.match(r'^\s*0x[0-9a-fA-F]+\s+(.+)$', line)
        if not match:
            continue
        for field in match.group(1).split():
            # readelf prints the final short group as 2/4/6 hex digits.
            if not re.fullmatch(r'(?:[0-9a-fA-F]{2}){1,4}', field):
                break
            result.extend(bytes.fromhex(field))
    return bytes(result)

count = 0
for xlen in (32, 64):
    isa = f'rv{xlen}i2p1'.encode()
    payload = b'\x04\x10\x05' + isa + b'\0\x06\x01'
    total = 16 + len(payload)
    expected = b'A' + struct.pack('<I', total - 1) + b'riscv\0' + \
               b'\x01' + struct.pack('<I', total - 11) + payload
    for order in ('ascending', 'descending', 'arch_first'):
        path = work / f'rv{xlen}.{order}.o'
        attributes = dump(path, '.riscv.attributes')
        assert attributes == expected, (xlen, order, attributes.hex(), expected.hex())
        count += 1
    path = work / f'rv{xlen}.duplicate.o'
    assert dump(path, '.riscv.attributes') == expected, xlen
    count += 1
    assert dump(work / f'rv{xlen}.csr.o', '.text') == \
        struct.pack('<II', 0x30402573, 0x30402573), xlen

print(f'MINIAS_RV_ATTRIBUTE_SEMANTIC_STATE=PASS byte_exact={count} '
      'profiles=RV32I/RV64I orders=ascending,descending,arch_first,duplicate '
      'csr_constant_expr=4 conflicts_rejected=2 csr_range_rejected=2')
PY
