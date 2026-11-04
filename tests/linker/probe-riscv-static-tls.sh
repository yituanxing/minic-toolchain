#!/usr/bin/env bash
# Host-independent RISC-V TLS ET_EXEC relocation contract: RV32 local-exec
# and RV64 initial-exec. MiniAS currently does not parse GNU %tprel_* / IE
# textual modifiers, so change the relocation type of genuine MiniAS ELF
# records without changing their symbol or instruction bytes. This proves
# the MiniLD linker output, not GNU/QEMU runtime execution.
set -Eeuo pipefail
: "${MINIAS:?MINIAS required}"
: "${MINILD:?MINILD required}"

repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-riscv-tls.XXXXXX")
trap 'rm -rf -- "$work"' EXIT

"$MINIAS" --target=riscv -march=rv32i -mabi=ilp32 \
    -o "$work/rv32.original.o" \
    "$repo/tests/linker/fixtures/riscv-static-tls-le.S"
"$MINIAS" --target=riscv -march=rv64i -mabi=lp64 \
    -o "$work/rv64.original.o" \
    "$repo/tests/linker/fixtures/riscv64-static-tls-ie.S"
for profile in 'rv32i ilp32 rv32' 'rv64i lp64 rv64'; do
    read -r march mabi prefix <<<"$profile"
    "$MINIAS" --target=riscv "-march=$march" "-mabi=$mabi" \
        -o "$work/$prefix.weak.original.o" \
        "$repo/tests/linker/fixtures/riscv-static-tls-weak.S"
done

python3 - "$work/rv32.original.o" "$work/rv32.tls.o" \
          "$work/rv64.original.o" "$work/rv64.tls.o" <<'PY'
import struct
from pathlib import Path
import sys

rv32 = bytearray(Path(sys.argv[1]).read_bytes())
assert rv32[:5] == b'\x7fELF\x01'
shoff = struct.unpack_from('<I', rv32, 32)[0]
shnum = struct.unpack_from('<H', rv32, 48)[0]
count = {}
for j in range(shnum):
    h = struct.unpack_from('<IIIIIIIIII', rv32, shoff + 40 * j)
    if h[1] != 4 or h[7] != 1:
        continue
    for off in range(h[4], h[4] + h[5], 12):
        src, info, addend = struct.unpack_from('<IIi', rv32, off)
        typ = info & 255
        if (src == 0 and typ == 26):
            typ = 29
        elif (src == 8 and typ == 27):
            typ = 30
        elif (src == 12 and typ == 26):
            typ = 29
        elif (src == 20 and typ == 28):
            typ = 31
        elif (src == 36 and typ == 1):
            src, typ = 4, 32
        elif (src == 40 and typ == 1):
            src, typ = 16, 32
        else:
            raise AssertionError(('unexpected RV32 relocation', src, typ))
        struct.pack_into('<IIi', rv32, off, src,
                         (info & 0xffffff00) | typ, addend)
        count[typ] = count.get(typ, 0) + 1
assert count == {29: 2, 30: 1, 31: 1, 32: 2}, count
Path(sys.argv[2]).write_bytes(rv32)

rv64 = bytearray(Path(sys.argv[3]).read_bytes())
assert rv64[:5] == b'\x7fELF\x02'
shoff = struct.unpack_from('<Q', rv64, 40)[0]
shnum = struct.unpack_from('<H', rv64, 60)[0]
updated = 0
for j in range(shnum):
    h = struct.unpack_from('<IIQQQQIIQQ', rv64, shoff + 64 * j)
    if h[1] != 4 or h[7] != 1:
        continue
    for off in range(h[4], h[4] + h[5], 24):
        src, info, addend = struct.unpack_from('<QQq', rv64, off)
        if src == 0 and (info & 0xffffffff) == 20:
            struct.pack_into('<QQq', rv64, off, src,
                             (info & ~0xffffffff) | 21, addend)
            updated += 1
assert updated == 1, updated
Path(sys.argv[4]).write_bytes(rv64)
PY

python3 - "$work/rv32.weak.original.o" "$work/rv32.weak.o" \
          "$work/rv32.strong.o" "$work/rv64.weak.original.o" \
          "$work/rv64.weak.o" "$work/rv64.strong.o" <<'PY'
import struct
from pathlib import Path
import sys

for orig, weak_out, strong_out, elf_class in (
    (*sys.argv[1:4], 1), (*sys.argv[4:7], 2)
):
    obj = bytearray(Path(orig).read_bytes())
    assert obj[:5] == b'\x7fELF' + bytes((elf_class,))
    if elf_class == 1:
        shoff, = struct.unpack_from('<I', obj, 32)
        shnum, = struct.unpack_from('<H', obj, 48)
        hfmt, shsize, relsize, symstride = '<IIIIIIIIII', 40, 12, 16
    else:
        shoff, = struct.unpack_from('<Q', obj, 40)
        shnum, = struct.unpack_from('<H', obj, 60)
        hfmt, shsize, relsize, symstride = '<IIQQQQIIQQ', 64, 24, 24
    headers = [struct.unpack_from(hfmt, obj, shoff + i * shsize)
               for i in range(shnum)]
    rels = [h for h in headers if h[1] == 4]
    assert len(rels) == 1
    rel = rels[0]
    assert rel[5] == 4 * relsize
    # Keep both GOT_HI20 source symbols intact: weak undefined and defined
    # TLS receive distinct slots with offsets zero and eight respectively.
    symidx = None
    patched = 0
    for off in range(rel[4], rel[4] + rel[5], relsize):
        if elf_class == 1:
            src, info, addend = struct.unpack_from('<IIi', obj, off)
            typ = info & 255
            index = info >> 8
            info_pos = off + 4
            fmt = '<I'
            typmask = 255
        else:
            src, info, addend = struct.unpack_from('<QQq', obj, off)
            typ = info & 0xffffffff
            index = info >> 32
            info_pos = off + 8
            fmt = '<Q'
            typmask = 0xffffffff
        if typ != 20:
            assert typ == 24, ('unexpected weak TLS relocation', src, typ)
            continue
        assert src in (0, 16) and addend == 0
        struct.pack_into(fmt, obj, info_pos, (info & ~typmask) | 21)
        if src == 0:
            symidx = index
        patched += 1
    assert patched == 2 and symidx is not None, (patched, symidx)
    symtab = next(h for h in headers if h[1] == 2)
    info_off = symtab[4] + symidx * symstride + (12 if elf_class == 1 else 4)
    assert obj[info_off] == 0x26, (elf_class, obj[info_off])
    Path(weak_out).write_bytes(obj)
    obj[info_off] = 0x16  # TLS GLOBAL UNDEF must remain a hard error.
    Path(strong_out).write_bytes(obj)
PY

readelf -Wr "$work/rv32.tls.o" > "$work/rv32.relocations"
readelf -Wr "$work/rv64.tls.o" > "$work/rv64.relocations"
for name in R_RISCV_TPREL_HI20 R_RISCV_TPREL_LO12_I \
            R_RISCV_TPREL_LO12_S R_RISCV_TPREL_ADD; do
    grep -Fq "$name" "$work/rv32.relocations"
done
grep -Fq R_RISCV_TLS_GOT_HI20 "$work/rv64.relocations"

"$MINILD" -m elf32lriscv -static -e _start \
    -o "$work/rv32.exe" "$work/rv32.tls.o"
"$MINILD" -m elf64lriscv -static -e _start \
    -o "$work/rv64.exe" "$work/rv64.tls.o"
"$MINILD" -m elf32lriscv -static -e _start \
    -o "$work/rv32.weak.exe" "$work/rv32.weak.o"
"$MINILD" -m elf64lriscv -static -e _start \
    -o "$work/rv64.weak.exe" "$work/rv64.weak.o"
for prefix in rv32 rv64; do
    if "$MINILD" -m "elf${prefix#rv}lriscv" -static -e _start \
       -o "$work/$prefix.strong.exe" "$work/$prefix.strong.o" \
       >"$work/$prefix.strong.log" 2>&1; then
        echo 'MINILD_RISCV_STATIC_TLS=FAIL strong_undefined_tls_accepted' >&2
        exit 1
    fi
    grep -Fq 'invalid-riscv-static-tls-symbol' "$work/$prefix.strong.log"
done

python3 - "$work/rv32.exe" "$work/rv64.exe" \
          "$work/rv32.weak.exe" "$work/rv64.weak.exe" <<'PY'
import struct
from pathlib import Path
import sys

def sections(elf, is64):
    if is64:
        shoff, = struct.unpack_from('<Q', elf, 40)
        shnum, shstr = struct.unpack_from('<HH', elf, 60)
        hs = [struct.unpack_from('<IIQQQQIIQQ', elf, shoff + i * 64)
              for i in range(shnum)]
    else:
        shoff, = struct.unpack_from('<I', elf, 32)
        shnum, shstr = struct.unpack_from('<HH', elf, 48)
        hs = [struct.unpack_from('<IIIIIIIIII', elf, shoff + i * 40)
              for i in range(shnum)]
    names = elf[hs[shstr][4]:hs[shstr][4] + hs[shstr][5]]
    result = {}
    for h in hs:
        end = names.find(b'\0', h[0])
        result[names[h[0]:end].decode()] = h
    return result

def tls_segment(elf, is64):
    if is64:
        off, = struct.unpack_from('<Q', elf, 32)
        entry_size, count = struct.unpack_from('<HH', elf, 54)
        phs = [struct.unpack_from('<IIQQQQQQ', elf, off + i * entry_size)
               for i in range(count)]
        found = [(p[3], p[5], p[6], p[7]) for p in phs if p[0] == 7]
    else:
        off, = struct.unpack_from('<I', elf, 28)
        entry_size, count = struct.unpack_from('<HH', elf, 42)
        phs = [struct.unpack_from('<IIIIIIII', elf, off + i * entry_size)
               for i in range(count)]
        found = [(p[2], p[4], p[5], p[7]) for p in phs if p[0] == 7]
    assert len(found) == 1, found
    return found[0]

rv32 = Path(sys.argv[1]).read_bytes()
rv64 = Path(sys.argv[2]).read_bytes()
assert rv32[:5] == b'\x7fELF\x01' and rv32[16:18] == b'\x02\0'
assert rv64[:5] == b'\x7fELF\x02' and rv64[16:18] == b'\x02\0'
s32 = sections(rv32, False)
s64 = sections(rv64, True)
for elf, sects, is64 in ((rv32, s32, False), (rv64, s64, True)):
    base, file_size, mem_size, align = tls_segment(elf, is64)
    assert file_size == sects['.tdata'][5] and align >= 8, (
        base, file_size, align)
    assert sects['.tdata'][3] == base
    assert sects['.tbss'][3] == base + 8
    assert mem_size == sects['.tbss'][3] + sects['.tbss'][5] - base

text = s32['.text']
ins = [struct.unpack_from('<I', rv32, text[4] + i * 4)[0]
       for i in range(6)]
assert (ins[0] >> 12) == 0 and (ins[3] >> 12) == 0, ins
assert ins[1] & 0xfe00707f == 0x33 and ins[4] & 0xfe00707f == 0x33
assert (ins[2] >> 20) & 0xfff == 0
store_lo12 = ((ins[5] >> 25) & 127) << 5 | ((ins[5] >> 7) & 31)
assert store_lo12 == 8, store_lo12

got = s64['.got']
assert got[5] == 8 and got[8] == 8, (got[5], got[8])
value, = struct.unpack_from('<Q', rv64, got[4])
assert value == 8, ('RV64 IE GOT value must be TP-relative', value)
for file, is64 in ((sys.argv[3], False), (sys.argv[4], True)):
    weak = Path(file).read_bytes()
    got_weak = sections(weak, is64)['.got']
    width = 8 if is64 else 4
    assert got_weak[5] == 2 * width
    values = struct.unpack_from('<QQ' if is64 else '<II',
                                weak, got_weak[4])
    assert values == (0, 8), ('weak and defined TLS IE GOT differ', file,
                              values)
print('MINILD_RISCV_STATIC_TLS=PASS rv32_le_hi_lo_i_lo_s_add=1 '
      'rv64_ie_got=1 weak_undefined_tls=2 defined_nonzero_tprel=2 '
      'strong_undefined_rejected=2 '
      'xlen_slot=1 pt_tls=2 structural=1 runtime=UNVERIFIED')
PY
