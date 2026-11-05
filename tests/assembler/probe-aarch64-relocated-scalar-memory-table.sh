#!/usr/bin/env bash
set -Eeuo pipefail
: "${MINIAS:?Set MINIAS to the current MiniAS binary}"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# Relocated scalar operands must use exactly the same A64 width/register
# descriptor as ordinary scaled/unscaled memory operands. Each lane probes
# one independent R_AARCH64_LDST{8,16,32,64}_ABS_LO12_NC relocation.
cat >"$work/relocated.s" <<'SRC'
.text
ldr x0,[x1, #:lo12:external_data]
ldr w2,[x3, #:lo12:external_data]
str x4,[x5, #:lo12:external_data]
str w6,[x7, #:lo12:external_data]
ldrb w8,[x9, #:lo12:external_data]
strb w10,[x11, #:lo12:external_data]
ldrh w12,[x13, #:lo12:external_data]
strh w14,[x15, #:lo12:external_data]
ldrsw x16,[x17, #:lo12:external_data]
ldr x18,[x19, #:got_lo12:external_data]
SRC

"$MINIAS" --target=aarch64 -o "$work/mini.o" "$work/relocated.s"
if command -v aarch64-linux-gnu-as >/dev/null 2>&1; then
    aarch64-linux-gnu-as -o "$work/gnu.o" "$work/relocated.s"
    gnu="$work/gnu.o"
else
    gnu=''
fi
python3 - "$work/mini.o" "$gnu" <<'PY'
import struct
import sys

def read(path):
    data = open(path, 'rb').read()
    assert data[:6] == b'\x7fELF\x02\x01', f'{path}: expected ELF64 LE'
    assert struct.unpack_from('<H', data, 18)[0] == 183, 'expected EM_AARCH64'
    offset = struct.unpack_from('<Q', data, 40)[0]
    size, count, names = struct.unpack_from('<HHH', data, 58)
    headers = [struct.unpack_from('<IIQQQQIIQQ', data, offset + i * size)
               for i in range(count)]
    strings = data[headers[names][4]:headers[names][4] + headers[names][5]]
    sections = {}
    for h in headers:
        name = strings[h[0]:strings.index(b'\0', h[0])].decode()
        sections[name] = (h, data[h[4]:h[4] + h[5]])
    code = sections['.text'][1]
    rela = sections['.rela.text'][1]
    assert len(code) == 40, f'{path}: expected 10 A64 scalar words'
    assert len(rela) == 10 * 24, f'{path}: expected ten ELF64 RELA records'
    reloc = [struct.unpack_from('<QQq', rela, i * 24) for i in range(10)]
    assert [r[0] for r in reloc] == list(range(0, 40, 4)), 'bad relocation offsets'
    assert [r[2] for r in reloc] == [0] * 10, 'nonzero LO12 addends'
    types = [r[1] & 0xffffffff for r in reloc]
    assert types == [286, 285, 286, 285, 278, 278, 284, 284, 285, 312], (
        f'{path}: wrong A64 relocation widths: {types}')
    return code, types

mini = read(sys.argv[1])
if sys.argv[2]:
    gnu = read(sys.argv[2])
    assert mini == gnu, 'A64 symbolic scalar memory bytes or relocations differ from GNU'
    oracle = 'exact'
else:
    oracle = 'SKIP(toolchain-unavailable)'
print(f'MINIAS_A64_RELOCATED_SCALAR_MEMORY=PASS forms=10 GNU={oracle} '
      'table=single-target-source relocation=typed')
PY

for bad in \
    'ldrsw w0,[x1,#:lo12:external_data]' \
    'ldrb x0,[x1,#:lo12:external_data]' \
    'ldrh x0,[x1,#:lo12:external_data]' \
    'ldur x0,[x1,#:lo12:external_data]' \
    'ldtr w0,[x1,#:lo12:external_data]' \
    'ldrsb x0,[x1,#:lo12:external_data]' \
    'str w0,[x1,#:got_lo12:external_data]' \
    'ldr w0,[x1,#:got_lo12:external_data]'; do
    printf '.text\n%s\n' "$bad" >"$work/invalid.s"
    if "$MINIAS" --target=aarch64 -o "$work/invalid.o" \
        "$work/invalid.s" >/dev/null 2>"$work/error"; then
        echo "MiniAS accepted invalid AArch64 relocated scalar memory: $bad" >&2
        exit 1
    fi
done
echo 'MINIAS_A64_RELOCATED_SCALAR_NEGATIVE=PASS rejects=8'
