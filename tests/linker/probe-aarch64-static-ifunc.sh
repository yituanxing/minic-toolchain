#!/usr/bin/env bash
# Real AArch64 static IFUNC: IPLT stubs, IRELATIVE materialization and glibc
# weak __rela_iplt boundaries.  GNU BFD and QEMU are independent CI oracles.
set -Eeuo pipefail
: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
for tool in python3 readelf timeout cmp; do command -v "$tool" >/dev/null; done
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-a64-ifunc.XXXXXX")
finish() {
    if [[ ${MINILD_AARCH64_IFUNC_KEEP:-0} == 1 ]]; then
        echo "MINILD_AARCH64_IFUNC_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap finish EXIT

"$MINIAS" --target=aarch64 -march=armv8-a -mabi=lp64 \
    -o "$work/entry.o" \
    "$repo/tests/linker/fixtures/aarch64-static-ifunc-irelative.s"
readelf -Ws "$work/entry.o" >"$work/entry.symbols"
grep -Eq '[[:space:]]IFUNC[[:space:]]+GLOBAL.*test_ifunc$' \
    "$work/entry.symbols"

"$MINILD" -m aarch64linux -static -e _start \
    -o "$work/mini-direct.elf" "$work/entry.o"
"$MINILD" -m aarch64linux -r \
    -o "$work/partial.o" "$work/entry.o"
"$MINILD" -m aarch64linux -static -e _start \
    -o "$work/mini-roundtrip.elf" "$work/partial.o"

python3 - "$work/mini-direct.elf" "$work/mini-roundtrip.elf" <<'PY'
from pathlib import Path
import struct
import sys

def verify(path):
    raw = Path(path).read_bytes()
    assert raw[:6] == b'\x7fELF\x02\x01'
    assert struct.unpack_from('<HH', raw, 16) == (2, 183)
    phoff, shoff = struct.unpack_from('<QQ', raw, 32)
    phentsize, phnum = struct.unpack_from('<HH', raw, 54)
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', raw, 58)
    assert phentsize == 56 and shentsize == 64
    phs = [struct.unpack_from('<IIQQQQQQ', raw, phoff + 56 * i)
           for i in range(phnum)]
    shs = [struct.unpack_from('<IIQQQQIIQQ', raw, shoff + 64 * i)
           for i in range(shnum)]
    sp = shs[shstrndx]
    names = raw[sp[4]:sp[4] + sp[5]]
    def string(pool, off):
        return pool[off:pool.index(0, off)].decode()
    sections = {string(names, s[0]): s for s in shs}
    for name in ('.text', '.iplt', '.igot.plt', '.rela.plt'):
        assert name in sections, f'missing {name}'
    text, iplt, got, rela = (sections[x] for x in
                             ('.text', '.iplt', '.igot.plt', '.rela.plt'))
    assert iplt[5] == 16 and got[5] == 8 and rela[5] == 24
    assert iplt[3] % 16 == 0 and got[3] % 8 == 0
    entries = struct.unpack_from('<QQq', raw, rela[4])
    assert entries == (got[3], 1032, 0x400064) or (
        entries[0] == got[3] and entries[1] == 1032)
    # IPLT must load the address from precisely the IRELATIVE destination.
    adrp, ldr, br, nop = struct.unpack_from('<IIII', raw, iplt[4])
    assert adrp & 0x9f00001f == 0x90000010
    assert ldr & 0xffc003ff == 0xf9400211
    assert br == 0xd61f0220 and nop == 0xd503201f
    imm = ((adrp >> 29) & 3) | (((adrp >> 5) & 0x7ffff) << 2)
    if imm & (1 << 20): imm -= 1 << 21
    slot = (iplt[3] & ~0xfff) + imm * 4096 + ((ldr >> 10) & 0xfff) * 8
    assert slot == got[3]
    code = raw[text[4]:text[4] + text[5]]
    branches = [(index, word) for index, (word,) in enumerate(
        struct.iter_unpack('<I', code)) if word & 0xfc000000 == 0x94000000]
    assert len(branches) == 1, branches
    index, word = branches[0]
    branch_delta = word & 0x03ffffff
    if branch_delta & (1 << 25): branch_delta -= 1 << 26
    assert text[3] + 4 * index + 4 * branch_delta == iplt[3]
    symtab = sections['.symtab']
    strtab = shs[symtab[6]]
    symbols = raw[strtab[4]:strtab[4] + strtab[5]]
    found = {}
    for off in range(symtab[4], symtab[4] + symtab[5], 24):
        name, info, _, shndx, value, _ = struct.unpack_from('<IBBHQQ', raw, off)
        if name:
            found[string(symbols, name)] = (info & 0xf, shndx, value)
    assert found['__rela_iplt_start'][2] == rela[3]
    assert found['__rela_iplt_end'][2] == rela[3] + rela[5]
    assert found['test_ifunc'][0] == 10
    assert entries[2] == found['test_ifunc'][2]
    assert any(p[0] == 1 and p[1] & 1 and
               p[3] <= iplt[3] < p[3] + p[6] for p in phs)
    assert any(p[0] == 1 and p[1] & 2 and
               p[3] <= got[3] < p[3] + p[6] for p in phs)
    print(f'MINILD_AARCH64_IFUNC=PASS file={Path(path).name} '
          'stub=16 slot=8 irelative=1 iplt_boundaries=2 branch=1')

for path in sys.argv[1:]:
    verify(path)
PY

# An IFUNC cannot be treated as an ordinary arbitrary scalar relocation: it
# would expose its resolver address as the implementation without apply_irel.
python3 - "$work/entry.o" "$work/bad-reloc.o" <<'PY'
from pathlib import Path
import struct
import sys
raw = bytearray(Path(sys.argv[1]).read_bytes())
shoff = struct.unpack_from('<Q', raw, 40)[0]
shnum = struct.unpack_from('<H', raw, 60)[0]
patched = False
for i in range(shnum):
    s = struct.unpack_from('<IIQQQQIIQQ', raw, shoff + i * 64)
    if s[1] != 4:
        continue
    for off in range(s[4], s[4] + s[5], 24):
        info = struct.unpack_from('<Q', raw, off + 8)[0]
        if (info & 0xffffffff) == 283:  # R_AARCH64_CALL26
            struct.pack_into('<Q', raw, off + 8,
                             (info & ~0xffffffff) | 260)  # PREL64
            patched = True
            break
    if patched: break
assert patched, 'missing CALL26 IFUNC relocation'
Path(sys.argv[2]).write_bytes(raw)
PY
if "$MINILD" -m aarch64linux -static -e _start \
    -o "$work/should-not-exist.elf" "$work/bad-reloc.o" \
    >"$work/bad.stdout" 2>"$work/bad.stderr"; then
    echo 'MINILD_AARCH64_IFUNC=FAIL unsupported_ifunc_relocation_accepted' >&2
    exit 1
fi
grep -Fq 'unsupported-aarch64-static-ifunc-reference:260' "$work/bad.stderr"
echo 'MINILD_AARCH64_IFUNC=PASS invalid_ifunc_fixup=fail_closed'

ld=aarch64-linux-gnu-ld.bfd
qemu=qemu-aarch64
if [[ ${MINILD_REQUIRE_GNU:-0} == 1 ]] && ! command -v "$ld" >/dev/null; then
    echo 'MINILD_AARCH64_IFUNC=FAIL required_gnu_oracle_missing' >&2
    exit 1
fi
if [[ ${MINILD_REQUIRE_QEMU:-0} == 1 ]] && ! command -v "$qemu" >/dev/null; then
    echo 'MINILD_AARCH64_IFUNC=FAIL required_qemu_oracle_missing' >&2
    exit 1
fi
if command -v "$ld" >/dev/null; then
    "$ld" -m aarch64linux -static -e _start \
        -o "$work/gnu.elf" "$work/entry.o"
    readelf -Wr "$work/gnu.elf" | grep -Fq R_AARCH64_IRELATIVE
    echo 'MINILD_AARCH64_IFUNC=PASS gnu_bfd=1 producer=MiniAS'
else
    echo 'MINILD_AARCH64_IFUNC=SKIP missing_aarch64_cross_ld'
fi
if command -v "$ld" >/dev/null && command -v "$qemu" >/dev/null; then
    for flavor in mini-direct mini-roundtrip gnu; do
        result=0
        timeout --signal=TERM 15s "$qemu" "$work/$flavor.elf" \
            >"$work/$flavor.stdout" 2>"$work/$flavor.stderr" || result=$?
        if (( result != 0 )) || [[ -s "$work/$flavor.stdout" ||
                                  -s "$work/$flavor.stderr" ]]; then
            echo "MINILD_AARCH64_IFUNC=FAIL runtime=$flavor status=$result" >&2
            exit 1
        fi
    done
    echo 'MINILD_AARCH64_IFUNC=PASS gnu_bfd=1 qemu=3 resolver_calls=1'
else
    echo 'MINILD_AARCH64_IFUNC=SKIP cross_gnu_qemu_runtime_missing'
fi
