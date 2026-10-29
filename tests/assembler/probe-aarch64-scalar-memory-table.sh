#!/usr/bin/env bash
set -Eeuo pipefail
: "${MINIAS:?Set MINIAS to the current MiniAS binary}"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# This probes the ISA-owned table shared by unsigned-imm12, signed-imm9,
# indexed and unprivileged forms. The expected words are independent A64
# instruction encodings, not computed from MiniAS's internal table.
cat >"$work/memory.s" <<'SRC'
.text
ldr w0,[x1,#8]
ldr x0,[x1,#8]
str w2,[x3,#-4]
ldrb w5,[x6,#3]
ldrh w7,[x8,#2]
ldrsw x9,[x10,#4]
ldrsb w11,[x12,#1]
ldrsb x11,[x12,#1]
ldrsh w13,[x14,#2]
ldrsh x13,[x14,#2]
ldur w0,[x1,#4]
stur x2,[x3,#-8]
ldursh x4,[x5,#-2]
ldtr w0,[x1,#-1]
sttrb w2,[x3,#2]
ldtrsw x4,[x5,#0]
ldr x6,[x7,x8,lsl #3]
str x2,[x3],#8
ldr x2,[x3,#8]!
SRC

"$MINIAS" --target=aarch64 -o "$work/memory.o" "$work/memory.s"
gnu_object=''
if command -v aarch64-linux-gnu-as >/dev/null; then
    aarch64-linux-gnu-as -o "$work/memory-gnu.o" "$work/memory.s"
    gnu_object="$work/memory-gnu.o"
fi
python3 - "$work/memory.o" "$gnu_object" <<'PY'
import struct
import sys

def text_section(path):
    elf = open(path, 'rb').read()
    assert elf[:6] == b'\x7fELF\x02\x01', 'expected AArch64 little-endian ELF64'
    section_offset = struct.unpack_from('<Q', elf, 40)[0]
    entry_size, count, strings = struct.unpack_from('<HHH', elf, 58)
    section = [struct.unpack_from('<IIQQQQIIQQ', elf,
                                section_offset + entry_size * index)
               for index in range(count)]
    string_offset, string_size = section[strings][4:6]
    names = elf[string_offset:string_offset + string_size]
    for header in section:
        index = header[0]
        name = names[index:names.find(b'\0', index)]
        if name == b'.text':
            return elf[header[4]:header[4] + header[5]]
    raise AssertionError('missing .text')

data = text_section(sys.argv[1])
words = struct.unpack('<' + 'I' * (len(data) // 4), data)
expected = (
    0xb9400820, 0xf9400420, 0xb81fc062, 0x39400cc5,
    0x79400507, 0xb9800549, 0x39c0058b, 0x3980058b,
    0x79c005cd, 0x798005cd, 0xb8404020, 0xf81f8062,
    0x789fe0a4, 0xb85ff820, 0x38002862, 0xb88008a4,
    0xf86878e6, 0xf8008462, 0xf8408c62,
)
if words != expected:
    for index, (actual, wanted) in enumerate(zip(words, expected)):
        if actual != wanted:
            print(f'A64 scalar-memory opcode mismatch index={index} '
                  f'actual={actual:08x} expected={wanted:08x}', file=sys.stderr)
    raise SystemExit(1)
if sys.argv[2]:
    gnu_text = text_section(sys.argv[2])
    if data != gnu_text:
        if len(gnu_text) % 4:
            raise AssertionError('GNU A64 .text is not instruction aligned')
        gnu_words = struct.unpack('<' + 'I' * (len(gnu_text) // 4), gnu_text)
        for index in range(max(len(words), len(gnu_words))):
            ours = f'{words[index]:08x}' if index < len(words) else '<missing>'
            gnu = f'{gnu_words[index]:08x}' if index < len(gnu_words) else '<missing>'
            if ours != gnu:
                print(f'MINIAS_A64_GNU_MISMATCH index={index} '
                      f'mini={ours} GNU={gnu}', file=sys.stderr)
        raise AssertionError('MiniAS A64 scalar-memory bytes differ from GNU GAS')
    oracle = 'exact'
else:
    oracle = 'SKIP(toolchain-unavailable)'
print(f'MINIAS_A64_SCALAR_MEMORY_TABLE=PASS encoded_words={len(words)} '
      f'GNU_bytes={oracle} scaled=1 unscaled=1 unprivileged=1 '
      'indexed=1 writeback=1')
PY

for invalid in \
    'ldrb x0,[x1]' \
    'ldrsw w0,[x1]' \
    'ldtr x0,[x1,x2]' \
    'ldur x0,[x1],#8' \
    'ldrsw x0,[x1],#512' \
    'ldrh w0,[x1,#8192]' \
    'str x0,[x1,#-257]' \
    'ldrb w0,[x1,#4096]'; do
    printf '.text\n%s\n' "$invalid" >"$work/invalid.s"
    if "$MINIAS" --target=aarch64 -o "$work/invalid.o" \
        "$work/invalid.s" >/dev/null 2>"$work/error"; then
        echo "MiniAS accepted invalid A64 memory operand: $invalid" >&2
        exit 1
    fi
done
echo 'MINIAS_A64_SCALAR_MEMORY_NEGATIVE=PASS invalid=8'
