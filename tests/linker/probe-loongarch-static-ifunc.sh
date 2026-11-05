#!/usr/bin/env bash
# True LoongArch64 static IFUNC relocation, IPLT/IGOT address calculus,
# glibc-style apply_irel, GNU BFD and QEMU cross-oracle.
set -Eeuo pipefail
repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
MINIAS=${MINIAS:-"$repo/build/release/bin/minic-as"}
MINILD=${MINILD:-"$repo/build/release/bin/minic-ld"}
GNU_LD=${GNU_LD:-loongarch64-linux-gnu-ld.bfd}
QEMU_USER=${QEMU_USER:-qemu-loongarch64}
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-loong-ifunc.XXXXXX")
trap 'rm -rf -- "$work"' EXIT

"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/entry.o" \
    "$repo/tests/linker/fixtures/loongarch-static-ifunc-irelative.s"
readelf -Ws "$work/entry.o" | \
    grep -Eq '[[:space:]]IFUNC[[:space:]]+GLOBAL.*test_ifunc$'
"$MINILD" -m elf64loongarch -static -e _start \
    -o "$work/mini-direct.elf" "$work/entry.o"
"$MINILD" -m elf64loongarch -r -o "$work/partial.o" "$work/entry.o"
"$MINILD" -m elf64loongarch -static -e _start \
    -o "$work/mini-roundtrip.elf" "$work/partial.o"

python3 - "$work/mini-direct.elf" "$work/mini-roundtrip.elf" <<'PY'
from pathlib import Path
import struct
import sys

def check(path):
    b = Path(path).read_bytes()
    assert b[:6] == b'\x7fELF\x02\x01'
    assert struct.unpack_from('<HH', b, 16) == (2, 258)
    phoff, shoff = struct.unpack_from('<QQ', b, 32)
    phent, phnum = struct.unpack_from('<HH', b, 54)
    shent, shnum, shstr = struct.unpack_from('<HHH', b, 58)
    assert phent == 56 and shent == 64
    phs = [struct.unpack_from('<IIQQQQQQ', b, phoff + i * 56)
           for i in range(phnum)]
    shs = [struct.unpack_from('<IIQQQQIIQQ', b, shoff + i * 64)
           for i in range(shnum)]
    sp = shs[shstr]
    names = b[sp[4]:sp[4] + sp[5]]
    def getstr(pool, pos):
        return pool[pos:pool.index(0, pos)].decode()
    sections = {getstr(names, s[0]): s for s in shs}
    for name in ('.text', '.iplt', '.igot.plt', '.rela.dyn'):
        assert name in sections, f'{path}: missing {name}'
    text, iplt, got, rela = (sections[x] for x in
                             ('.text', '.iplt', '.igot.plt', '.rela.dyn'))
    assert (iplt[5], got[5], rela[5]) == (16, 8, 24)
    assert iplt[3] % 16 == 0 and got[3] % 8 == 0
    rela_offset, info, resolver = struct.unpack_from('<QQq', b, rela[4])
    assert rela_offset == got[3] and info == 12, (rela_offset, info)
    hi, ld, jump, nop = struct.unpack_from('<IIII', b, iplt[4])
    assert hi & 0xfe00001f == 0x1c00000f, hex(hi)
    assert ld & 0xffc003ff == 0x28c001ef, hex(ld)
    assert jump == 0x4c0001ed and nop == 0x03400000
    imm20 = (hi >> 5) & 0xfffff
    if imm20 & 0x80000: imm20 -= 0x100000
    imm12 = (ld >> 10) & 0xfff
    if imm12 & 0x800: imm12 -= 0x1000
    assert iplt[3] + (imm20 << 12) + imm12 == got[3]
    code = b[text[4]:text[4] + text[5]]
    branches = [(index, word) for index, (word,) in enumerate(
        struct.iter_unpack('<I', code)) if word & 0xfc000000 == 0x54000000]
    assert len(branches) == 1, branches
    index, branch = branches[0]
    imm = ((branch >> 10) & 0xffff) | ((branch & 0x3ff) << 16)
    if imm & 0x2000000: imm -= 0x4000000
    assert text[3] + index * 4 + imm * 4 == iplt[3]
    symtab = sections['.symtab']; strtab = shs[symtab[6]]
    symstrings = b[strtab[4]:strtab[4] + strtab[5]]
    syms = {}
    for off in range(symtab[4], symtab[4] + symtab[5], 24):
        name, stinfo, _, shndx, value, _ = struct.unpack_from('<IBBHQQ', b, off)
        if name:
            syms[getstr(symstrings, name)] = (stinfo & 15, shndx, value)
    assert syms['__rela_iplt_start'][2] == rela[3]
    assert syms['__rela_iplt_end'][2] == rela[3] + rela[5]
    assert syms['test_ifunc'][0] == 10
    assert resolver == syms['test_ifunc'][2]
    assert any(p[0] == 1 and p[1] & 1 and
               p[3] <= iplt[3] < p[3] + p[6] for p in phs)
    assert any(p[0] == 1 and p[1] & 2 and
               p[3] <= got[3] < p[3] + p[6] for p in phs)
    print(f'MINILD_LOONGARCH_IFUNC=PASS file={Path(path).name} '
          'stub=16 slot=8 irelative=1 boundaries=2 direct_branch=1')

for path in sys.argv[1:]: check(path)
PY

# B16 relocation into a function resolver is not a legal static IFUNC call.
# The target must fail before trying to encode the original BL instruction.
python3 - "$work/entry.o" "$work/bad.o" <<'PY'
from pathlib import Path
import struct
import sys
b = bytearray(Path(sys.argv[1]).read_bytes())
shoff = struct.unpack_from('<Q', b, 40)[0]
num = struct.unpack_from('<H', b, 60)[0]
patched = 0
for i in range(num):
    s = struct.unpack_from('<IIQQQQIIQQ', b, shoff + 64 * i)
    if s[1] != 4: continue
    for at in range(s[4], s[4] + s[5], 24):
        info = struct.unpack_from('<Q', b, at + 8)[0]
        if (info & 0xffffffff) == 66:
            off = struct.unpack_from('<Q', b, at)[0]
            target = struct.unpack_from('<IIQQQQIIQQ', b,
                                        shoff + 64 * s[7])
            word = struct.unpack_from('<I', b, target[4] + off)[0]
            if word & 0xfc000000 == 0x54000000:
                struct.pack_into('<Q', b, at + 8,
                                 (info & ~0xffffffff) | 64)
                patched += 1
assert patched == 1, patched
Path(sys.argv[2]).write_bytes(b)
PY
if "$MINILD" -m elf64loongarch -static -e _start \
    -o "$work/bad.elf" "$work/bad.o" \
    >"$work/bad.stdout" 2>"$work/bad.stderr"; then
    echo 'MINILD_LOONGARCH_IFUNC=FAIL unsupported_ifunc_reference_accepted' >&2
    exit 1
fi
grep -Fq 'unsupported-loongarch-static-ifunc-reference:64' \
    "$work/bad.stderr"
echo 'MINILD_LOONGARCH_IFUNC=PASS invalid_ifunc_reloc=fail_closed'

if [[ ${MINILD_REQUIRE_GNU:-0} == 1 ]] && \
   ! command -v "$GNU_LD" >/dev/null; then
    echo 'MINILD_LOONGARCH_IFUNC=FAIL required_gnu_oracle_missing' >&2
    exit 1
fi
if [[ ${MINILD_REQUIRE_QEMU:-0} == 1 ]] && \
   ! command -v "$QEMU_USER" >/dev/null; then
    echo 'MINILD_LOONGARCH_IFUNC=FAIL required_qemu_oracle_missing' >&2
    exit 1
fi
if command -v "$GNU_LD" >/dev/null; then
    "$GNU_LD" -m elf64loongarch -static -e _start \
        -o "$work/gnu.elf" "$work/entry.o"
    readelf -Wr "$work/gnu.elf" | grep -Fq R_LARCH_IRELATIVE
    echo 'MINILD_LOONGARCH_IFUNC=PASS gnu_bfd=1 producer=MiniAS'
else
    echo 'MINILD_LOONGARCH_IFUNC=SKIP missing_loongarch_cross_ld'
fi
if command -v "$GNU_LD" >/dev/null && \
   command -v "$QEMU_USER" >/dev/null; then
    for flavor in gnu mini-direct mini-roundtrip; do
        result=0
        timeout --signal=TERM 15s "$QEMU_USER" "$work/$flavor.elf" \
            >"$work/$flavor.stdout" 2>"$work/$flavor.stderr" || result=$?
        if (( result != 0 )) || [[ -s "$work/$flavor.stdout" ||
                                  -s "$work/$flavor.stderr" ]]; then
            echo "MINILD_LOONGARCH_IFUNC=FAIL runtime=$flavor status=$result" >&2
            exit 1
        fi
    done
    echo 'MINILD_LOONGARCH_IFUNC=PASS gnu_bfd=1 qemu=3 resolver_calls=1'
else
    echo 'MINILD_LOONGARCH_IFUNC=SKIP cross_gnu_qemu_runtime_missing'
fi
