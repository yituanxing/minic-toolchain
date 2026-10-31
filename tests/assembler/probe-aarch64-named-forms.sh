#!/usr/bin/env bash
# Regression lock for AArch64's Target-private named instruction descriptors.
# The known A64 machine words were recorded from the pre-unification encoder;
# when cross GNU GAS is installed, it is also an independent byte oracle.
set -Eeuo pipefail
: "${MINIAS:?MINIAS must point at minic-as}"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

cat > "$work/named-forms.s" <<'ASM'
.arch armv8.4-a+crypto+sha3
.text
madd x0,x1,x2,x3
umaddl x4,w5,w6,x7
smull x8,w9,w10
neg x0,x1
negs w2,w3,lsl #1
ngc x4,x5
ubfm x0,x1,#5,#20
sbfx x2,x3,#5,#10
bfi x4,x5,#3,#12
csel w0,w1,w2,eq
csneg x3,x4,x5,ne
addv b0,v1.16b
umaxv h2,v3.8h
ushr v0.16b,v1.16b,#3
sri v2.16b,v3.16b,#2
shl v4.8h,v5.8h,#3
crc32b w0,w1,w2
crc32cx w3,w4,x5
aese v0.16b,v1.16b
aesimc v2.16b,v3.16b
eor v0.16b,v1.16b,v2.16b
cmeq v3.16b,v4.16b,v5.16b
zip1 v6.4s,v7.4s,v8.4s
add v9.4s,v10.4s,v11.4s
pmull v10.8h,v10.8b,v0.8b
pmull2 v11.8h,v12.16b,v13.16b
sha1su0 v8.4s,v9.4s,v10.4s
sha1su1 v8.4s,v9.4s
sha1c q12,s7,v4.4s
sha1m q3,s4,v5.4s
sha1p q0,s1,v2.4s
sha256h q24,q25,v22.4s
sha256h2 q28,q29,v30.4s
sha256su0 v0.4s,v1.4s
sha256su1 v0.4s,v1.4s,v2.4s
eor3 v0.16b,v1.16b,v2.16b,v3.16b
bcax v4.16b,v5.16b,v6.16b,v7.16b
rax1 v8.2d,v9.2d,v10.2d
xar v11.2d,v12.2d,v13.2d,#42
sha512h q0,q1,v2.2d
sha512su0 v6.2d,v7.2d
sha512su1 v8.2d,v9.2d,v10.2d
ASM

"$MINIAS" --target=aarch64 -o "$work/mini.o" "$work/named-forms.s"
if command -v aarch64-linux-gnu-as >/dev/null; then
    aarch64-linux-gnu-as -o "$work/gnu.o" "$work/named-forms.s"
    oracle="$work/gnu.o"
else
    oracle=''
fi
python3 - "$work/mini.o" "$oracle" <<'PY'
import struct
import sys

def text_section(path):
    elf = open(path, 'rb').read()
    assert elf[:6] == b'\x7fELF\x02\x01'
    shoff = struct.unpack_from('<Q', elf, 40)[0]
    shentsize, shnum, shstr = struct.unpack_from('<HHH', elf, 58)
    sections = [struct.unpack_from('<IIQQQQIIQQ', elf, shoff + shentsize*i)
                for i in range(shnum)]
    name_off, name_size = sections[shstr][4:6]
    names = elf[name_off:name_off+name_size]
    for section in sections:
        i = section[0]
        if names[i:names.find(b'\x00', i)] == b'.text':
            return elf[section[4]:section[4]+section[5]]
    raise AssertionError('missing .text')

ours = text_section(sys.argv[1])
expected_hex = (
    '200c029ba41ca69b287d2a9be00301cbe207036be40305da205045d3'
    '62384593a42c7db32000821a831485da20b8314e62a8706e20040d6f'
    '62440e6fa454134f2040c21a835cc59a2048284e6278284e201c226e'
    '838c256ee638884e4985ab4e4ae1200e8be12d4e28310a5e2819285e'
    'ec00045e8320055e2010025e3843165ebc531e5e2028285e2060025e'
    '200c02cea41c26ce288d6ace8ba98dce208062cee680c0ce28896ace'
)
assert ours.hex() == expected_hex, (
    'A64 named-form machine code changed:\nactual=' + ours.hex())
assert len(ours) == 42*4
if sys.argv[2]:
    assert ours == text_section(sys.argv[2]), 'GNU AArch64 encoding mismatch'
    oracle = 'exact'
else:
    oracle = 'SKIP(toolchain-unavailable)'
print(f'MINIAS_A64_NAMED_FORMS=PASS instructions=42 families=13 '
      f'GNU_bytes={oracle}')
PY

# The shape bits are ISA constraints, not mnemonic spelling heuristics.
for invalid in \
    'crc32cx w0,w1,w2' \
    'crc32cw w0,w1,x2' \
    'aese v0.8b,v1.8b' \
    'umaxv b0,v1.4s' \
    'shl v0.16b,v1.8b,#2' \
    'sha512h v0.2d,v1.2d,v2.2d' \
    'sha256h v0.4s,v1.4s,v2.4s' \
    'zip1 v0.16b,v1.8b,v2.16b'; do
    printf '.arch armv8.4-a+crypto+sha3\n.text\n%s\n' "$invalid" \
      > "$work/invalid.s"
    if "$MINIAS" --target=aarch64 -o "$work/invalid.o" \
         "$work/invalid.s" > "$work/out" 2> "$work/err"; then
        echo "A64 named-form shape unexpectedly accepted: $invalid" >&2
        exit 1
    fi
done
echo 'MINIAS_A64_NAMED_FORMS_NEGATIVE=PASS forms=8'
