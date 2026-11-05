#!/usr/bin/env bash
# Prove AArch64 GOT is a target-owned synthetic section before ET_EXEC layout.
# The GNU comparison links the same MiniAS objects when a cross linker exists.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
for tool in python3 readelf; do
    command -v "$tool" >/dev/null
done

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-aarch64-static-got.XXXXXX")
cleanup() {
    if [[ ${MINILD_AARCH64_STATIC_GOT_KEEP:-0} == 1 ]]; then
        echo "MINILD_AARCH64_STATIC_GOT_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap cleanup EXIT

cat > "$work/entry.s" <<'ASM'
.text
.globl _start
.type _start,@function
_start:
    adrp x0,:got:fixture_alpha
    ldr x1,[x0,:got_lo12:fixture_alpha]
    adrp x0,:got:fixture_beta
    ldr x2,[x0,:got_lo12:fixture_beta]
    adrp x0,:got:fixture_alpha
    ldr x3,[x0,:got_lo12:fixture_alpha]
    ret
.size _start,.-_start
ASM
cat > "$work/data.s" <<'ASM'
.data
.p2align 3
.globl fixture_alpha
.type fixture_alpha,@object
fixture_alpha:
    .quad 0x1122334455667788
.size fixture_alpha,8
.globl fixture_beta
.type fixture_beta,@object
fixture_beta:
    .quad 0x8877665544332211
.size fixture_beta,8
ASM

"$MINIAS" --target=aarch64 -march=armv8-a -mabi=lp64 \
    -o "$work/entry.o" "$work/entry.s"
"$MINIAS" --target=aarch64 -march=armv8-a -mabi=lp64 \
    -o "$work/data.o" "$work/data.s"
readelf -Wr "$work/entry.o" > "$work/entry.reloc"
test "$(grep -c R_AARCH64_ADR_GOT_PAGE "$work/entry.reloc")" -eq 3
test "$(grep -c R_AARCH64_LD64_GOT_LO12_NC "$work/entry.reloc")" -eq 3

"$MINILD" -m aarch64linux -static -e _start -o "$work/direct.elf" \
    "$work/entry.o" "$work/data.o"
"$MINILD" -m aarch64linux -r -o "$work/partial.o" \
    "$work/entry.o" "$work/data.o"
"$MINILD" -m aarch64linux -static -e _start -o "$work/roundtrip.elf" \
    "$work/partial.o"

python3 - "$work/direct.elf" "$work/roundtrip.elf" <<'PY'
from pathlib import Path
import struct
import sys


def ensure(value, message):
    if not value:
        raise SystemExit('MINILD_AARCH64_STATIC_GOT=FAIL ' + message)


class Elf:
    def __init__(self, path):
        b = Path(path).read_bytes()
        ensure(b[:6] == b'\x7fELF\x02\x01', 'bad_elf')
        ensure(struct.unpack_from('<HH', b, 16) == (2, 183), 'bad_elf_type')
        shoff = struct.unpack_from('<Q', b, 40)[0]
        shentsize, shnum, shstrndx = struct.unpack_from('<HHH', b, 58)
        ensure(shentsize == 64, 'bad_shentsize')
        sh = [struct.unpack_from('<IIQQQQIIQQ', b, shoff + i * 64)
              for i in range(shnum)]
        strings = b[sh[shstrndx][4]:sh[shstrndx][4] + sh[shstrndx][5]]

        def name(pool, offset):
            end = pool.index(0, offset)
            return pool[offset:end].decode()

        self.sections = {}
        self.symbols = {}
        for section in sh:
            self.sections[name(strings, section[0])] = (
                section[3], b[section[4]:section[4] + section[5]])
            if section[1] != 2:
                continue
            str_section = sh[section[6]]
            pool = b[str_section[4]:str_section[4] + str_section[5]]
            for pos in range(section[4], section[4] + section[5], 24):
                st_name, _, _, _, value, _ = struct.unpack_from('<IBBHQQ', b, pos)
                if st_name:
                    self.symbols[name(pool, st_name)] = value


def check_elf(path):
    elf = Elf(path)
    text_addr, code = elf.sections['.text']
    got_addr, got_data = elf.sections['.got']
    ensure(len(got_data) == 16 and got_addr % 8 == 0,
           'expected_two_aligned_got_entries')
    pairs = []
    for off in (0, 8, 16):
        adrp, ldr = struct.unpack_from('<II', code, off)
        ensure((adrp & 0x9f000000) == 0x90000000 and
               (ldr & 0xffc00000) == 0xf9400000,
               'expected_adrp_ldr_pair')
        imm21 = ((adrp >> 29) & 3) | (((adrp >> 5) & 0x7ffff) << 2)
        if imm21 & (1 << 20):
            imm21 -= 1 << 21
        page = ((text_addr + off) & ~0xfff) + imm21 * 4096
        slot = page + (((ldr >> 10) & 0xfff) << 3)
        ensure(got_addr <= slot and slot + 8 <= got_addr + len(got_data),
               'got_reference_out_of_range')
        value = struct.unpack_from('<Q', got_data, slot - got_addr)[0]
        pairs.append((slot, value))
    ensure(pairs[0] == pairs[2], 'same_symbol_did_not_share_got_slot')
    ensure(pairs[0][0] != pairs[1][0], 'different_symbols_shared_got_slot')
    ensure(pairs[0][1] == elf.symbols['fixture_alpha'] and
           pairs[1][1] == elf.symbols['fixture_beta'],
           'got_pointer_not_resolved_to_symbol')
    print(f'MINILD_AARCH64_GOT=PASS file={Path(path).name} '
          'relocs=6 unique_slots=2')


for output in sys.argv[1:]:
    check_elf(output)
PY

# AAELF64 explicitly rejects nonzero addends on GDAT(S) GOT-entry references.
python3 - "$work/entry.o" "$work/bad-addend.o" <<'PY'
from pathlib import Path
import struct
import sys

raw = bytearray(Path(sys.argv[1]).read_bytes())
shoff = struct.unpack_from('<Q', raw, 40)[0]
shnum = struct.unpack_from('<H', raw, 60)[0]
mutated = False
for i in range(shnum):
    sh = struct.unpack_from('<IIQQQQIIQQ', raw, shoff + i * 64)
    if sh[1] == 4 and sh[5] >= 24:
        struct.pack_into('<q', raw, sh[4] + 16, 1)
        mutated = True
        break
assert mutated, 'missing rela section'
Path(sys.argv[2]).write_bytes(raw)
PY
if "$MINILD" -m aarch64linux -static -e _start \
    -o "$work/bad.elf" "$work/bad-addend.o" "$work/data.o" \
    > "$work/bad.stdout" 2> "$work/bad.stderr"; then
    echo 'MINILD_AARCH64_STATIC_GOT=FAIL nonzero_addend_accepted' >&2
    exit 1
fi
grep -q 'invalid-aarch64-static-got-relocation:311' "$work/bad.stderr"

# Independent GNU oracle: use exactly the same MiniAS-produced objects, so
# this validates their ELF/relocation contract through GNU ld.  A developer
# host may lack cross tools, but CI must not report PASS after skipping this
# oracle when it explicitly requires GNU parity.
if [[ ${MINILD_REQUIRE_GNU:-0} == 1 ]] && \
   ! command -v aarch64-linux-gnu-ld >/dev/null 2>&1; then
    echo 'MINILD_AARCH64_STATIC_GOT=FAIL required_gnu_oracle_missing:aarch64-linux-gnu-ld' >&2
    exit 1
fi
if command -v aarch64-linux-gnu-ld >/dev/null; then
    aarch64-linux-gnu-ld -m aarch64linux -static -e _start \
        -o "$work/gnu.elf" "$work/entry.o" "$work/data.o"
    readelf -hW "$work/gnu.elf" | grep -q 'Machine:.*AArch64'
    readelf -sW "$work/gnu.elf" | grep -q 'fixture_alpha'
    readelf -sW "$work/gnu.elf" | grep -q 'fixture_beta'
    echo 'MINILD_AARCH64_GOT_GNU_PARITY=PASS producer=MiniAS linker=GNU'
else
    echo 'MINILD_AARCH64_GOT_GNU_PARITY=SKIP missing_aarch64_cross_ld'
fi
echo 'MINILD_AARCH64_STATIC_GOT=PASS direct=1 roundtrip=1 negative=1'
