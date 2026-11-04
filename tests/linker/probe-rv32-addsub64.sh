#!/usr/bin/env bash
# R_RISCV_ADD64/SUB64 address a 64-bit *data field*, even in ELF32/ILP32.
# Keep the absolute R_RISCV_64 RV32 gate unchanged. Compare MiniLD against
# GNU BFD on exactly the same MiniAS-produced ELF32 relocatable objects.
set -Eeuo pipefail
: "${MINIAS:?MINIAS required}"
: "${MINILD:?MINILD required}"

gnu_ld=${RISCV_GNU_LD:-riscv64-linux-gnu-ld}
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-rv32-addsub64.XXXXXX")
finish() {
    if [[ ${MINILD_RV32_ADD64_KEEP:-0} == 1 ]]; then
        echo "MINILD_RV32_ADD64_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap finish EXIT

cat >"$work/entry.s" <<'ASM'
.section .text,"ax",@progbits
.globl _start
.type _start,@function
_start:
  addi a0,zero,0
  addi a7,zero,93
  ecall
.size _start,.-_start
.section .data,"aw",@progbits
.globl external_delta_target
.type external_delta_target,@object
external_delta_target:
  .word 0x12345678
.size external_delta_target,4
ASM
cat >"$work/delta.s" <<'ASM'
.extern external_delta_target
.section .data,"aw",@progbits
.p2align 3
.globl delta64
.type delta64,@object
delta64:
  .dword external_delta_target - .
.size delta64,8
ASM
for unit in entry delta; do
    "$MINIAS" --target=riscv -march=rv32i -mabi=ilp32 \
        -o "$work/$unit.o" "$work/$unit.s"
    readelf -h "$work/$unit.o" | grep -Eq 'Class:[[:space:]]+ELF32'
done
readelf -Wr "$work/delta.o" >"$work/delta.relocs"
grep -q 'R_RISCV_ADD64.*external_delta_target' "$work/delta.relocs"
grep -q 'R_RISCV_SUB64' "$work/delta.relocs"

"$MINILD" -m elf32lriscv -static -e _start \
    -o "$work/mini.elf" "$work/entry.o" "$work/delta.o"

# Do not infer behavior from successful link alone: read the exact symbol
# values and the final 64-bit little-endian value at delta64.
python3 - "$work/mini.elf" <<'PY'
from pathlib import Path
import struct
import sys

data = Path(sys.argv[1]).read_bytes()
assert data[:6] == b'\x7fELF\x01\x01', 'not little-endian ELF32'
shoff = struct.unpack_from('<I', data, 32)[0]
shentsize, shnum, shstrndx = struct.unpack_from('<HHH', data, 46)
assert shentsize == 40
headers = [struct.unpack_from('<IIIIIIIIII', data, shoff + i * shentsize)
           for i in range(shnum)]
shstr = headers[shstrndx]
names = data[shstr[4]:shstr[4] + shstr[5]]

def sec_name(header):
    off = header[0]
    return names[off:names.index(b'\0', off)].decode()

symbols = {}
for sec in headers:
    if sec[1] != 2:
        continue
    strtab = headers[sec[6]]
    strings = data[strtab[4]:strtab[4] + strtab[5]]
    for p in range(sec[4], sec[4] + sec[5], 16):
        name, value, size, info, other, shndx = struct.unpack_from(
            '<IIIBBH', data, p)
        name = strings[name:strings.index(b'\0', name)].decode()
        if name in ('delta64', 'external_delta_target'):
            symbols[name] = (value, shndx, size)
assert set(symbols) == {'delta64', 'external_delta_target'}, symbols
start, section_number, length = symbols['delta64']
target = symbols['external_delta_target'][0]
assert length == 8
section = headers[section_number]
offset = section[4] + start - section[3]
assert section[4] <= offset and offset + 8 <= section[4] + section[5]
actual = struct.unpack_from('<Q', data, offset)[0]
expected = (target - start) & ((1 << 64) - 1)
assert actual == expected, (hex(actual), hex(expected), hex(target), hex(start))
print(f'MINILD_RV32_ADD64=FIELD_PASS data=0x{actual:016x} '
      f'target=0x{target:x} delta=0x{start:x}')
PY

gnu_parity=NOT_RUN
if command -v "$gnu_ld" >/dev/null 2>&1; then
    "$gnu_ld" -m elf32lriscv -static -e _start \
        -o "$work/gnu.elf" "$work/entry.o" "$work/delta.o"
    python3 - "$work/gnu.elf" "$work/mini.elf" <<'PY'
from pathlib import Path
import struct
import sys

def inspect(path):
    blob = Path(path).read_bytes()
    shoff = struct.unpack_from('<I', blob, 32)[0]
    shnum, shstrndx = struct.unpack_from('<HH', blob, 48)
    headers = [struct.unpack_from('<IIIIIIIIII', blob, shoff + i * 40)
               for i in range(shnum)]
    strings = headers[shstrndx]
    names = blob[strings[4]:strings[4] + strings[5]]
    for sec in headers:
        name = names[sec[0]:names.index(b'\0', sec[0])]
        if name == b'.data':
            return blob[sec[4]:sec[4] + sec[5]]
    raise AssertionError('missing .data')

gnu, mini = map(inspect, sys.argv[1:])
assert gnu == mini, (gnu.hex(), mini.hex())
print('MINILD_RV32_ADD64=GNU_PARITY_PASS data_bytes=' + str(len(gnu)))
PY
    gnu_parity=PASS
elif [[ ${MINILD_REQUIRE_GNU:-0} == 1 ]]; then
    echo "MINILD_RV32_ADD64=INCONCLUSIVE missing_GNU=$gnu_ld" >&2
    exit 127
fi

# A malformed 8-byte field must fail before writing outside the owner
# section even though ELF32 normally has 32-bit addresses.
python3 - "$work/delta.o" "$work/bad-offset.o" <<'PY'
from pathlib import Path
import struct
import sys

data = bytearray(Path(sys.argv[1]).read_bytes())
shoff = struct.unpack_from('<I', data, 32)[0]
count = struct.unpack_from('<H', data, 48)[0]
changed = False
for i in range(count):
    sh = struct.unpack_from('<IIIIIIIIII', data, shoff + i * 40)
    if sh[1] != 4:
        continue
    for p in range(sh[4], sh[4] + sh[5], 12):
        info = struct.unpack_from('<I', data, p + 4)[0]
        if (info & 0xff) == 36:
            struct.pack_into('<I', data, p, 8)
            changed = True
assert changed, 'R_RISCV_ADD64 not found'
Path(sys.argv[2]).write_bytes(data)
PY
if "$MINILD" -m elf32lriscv -static -e _start \
    -o "$work/invalid.elf" "$work/entry.o" "$work/bad-offset.o" \
    >"$work/invalid.stdout" 2>"$work/invalid.stderr"; then
    echo 'MINILD_RV32_ADD64=FAIL invalid_8byte_field_accepted' >&2
    exit 1
fi
grep -q 'ADD_SUB64-offset-out-of-range' "$work/invalid.stderr"
echo "MINILD_RV32_ADD64=PASS ELF32_ADD64_SUB64_checked_field=8 GNU_parity=$gnu_parity"
