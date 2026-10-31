#!/usr/bin/env bash
set -Eeuo pipefail
: "${MINIAS:?Set MINIAS to the current MiniAS binary}"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# The symbol difference is never an eager absolute operand.  Layout must
# choose unsigned imm12 or signed imm9 through one AArch64 memory descriptor
# after both labels have settled.  The eight instruction words below are
# independent architecture constants, not derived from MiniAS's table.
cat >"$work/late-memory.s" <<'SRC'
.text
span_begin:
.byte 1,2,3,4,5,6,7,8
span_end:
ldr w0,[x1,#span_end-span_begin]
ldr x2,[x3,#span_end-span_begin]
str w4,[x5,#span_begin-span_end]
str x6,[x7,#span_begin-span_end]
ldrb w8,[x9,#span_end-span_begin]
strb w10,[x11,#span_begin-span_end]
ldrh w12,[x13,#span_end-span_begin]
strh w14,[x15,#span_begin-span_end]
SRC

"$MINIAS" --target=aarch64 -o "$work/mini.o" "$work/late-memory.s"
gnu_object=''
if command -v aarch64-linux-gnu-as >/dev/null; then
    aarch64-linux-gnu-as -o "$work/gnu.o" "$work/late-memory.s"
    gnu_object="$work/gnu.o"
fi
python3 - "$work/mini.o" "$gnu_object" <<'PY'
import struct
import sys

def text_section(path):
    elf = open(path, 'rb').read()
    assert elf[:6] == b'\x7fELF\x02\x01'
    hdr_offset = struct.unpack_from('<Q', elf, 40)[0]
    size, count, strings = struct.unpack_from('<HHH', elf, 58)
    headers = [struct.unpack_from('<IIQQQQIIQQ', elf, hdr_offset + size * i)
               for i in range(count)]
    start, length = headers[strings][4:6]
    names = elf[start:start + length]
    for header in headers:
        index = header[0]
        if names[index:names.find(b'\0', index)] == b'.text':
            return elf[header[4]:header[4] + header[5]]
    raise AssertionError('missing .text')

ours = text_section(sys.argv[1])
assert ours[:8] == bytes(range(1, 9))
words = struct.unpack('<8I', ours[8:40])
expected = (0xb9400820, 0xf9400462, 0xb81f80a4, 0xf81f80e6,
            0x39402128, 0x381f816a, 0x794011ac, 0x781f81ee)
assert words == expected, [
    (i, f'{a:08x}', f'{b:08x}')
    for i, (a, b) in enumerate(zip(words, expected)) if a != b
]
if sys.argv[2]:
    assert ours == text_section(sys.argv[2]), 'GNU object .text bytes differ'
    oracle = 'exact'
else:
    oracle = 'SKIP(toolchain-unavailable)'
print(f'MINIAS_A64_DEFERRED_MEMORY_DESCRIPTOR=PASS instructions={len(words)} '
      f'GNU_bytes={oracle} scaled=1 unscaled=1')
PY

# Neither a numeric substitution nor a late symbol difference can hide an
# out-of-range ISA displacement.  A changed layout must still fail closed.
for invalid in \
    'ldr x0,[x1,#span_end-span_begin]' \
    'strb w0,[x1,#span_begin-span_end]'; do
    cat >"$work/invalid.s" <<SRC
.text
span_begin:
.space 32768
span_end:
$invalid
SRC
    if "$MINIAS" --target=aarch64 -o "$work/invalid.o" \
        "$work/invalid.s" >"$work/stdout" 2>"$work/stderr"; then
        echo "MiniAS accepted invalid deferred memory offset: $invalid" >&2
        exit 1
    fi
done
echo 'MINIAS_A64_DEFERRED_MEMORY_DESCRIPTOR_NEGATIVE=PASS invalid=2'
