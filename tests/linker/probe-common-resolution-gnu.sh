#!/usr/bin/env bash
# GNU BFD parity for tentative COMMON resolution across -r, final ET_EXEC,
# weak/strong inputs, and the distinction between lazy archive candidates.
# The same canonical resolver serves every MiniLD ELF target.
set -Eeuo pipefail

: "${MINILD:?MINILD must point to minic-ld}"
for tool in as ar ld python3; do
    command -v "$tool" >/dev/null || {
        echo "MINILD_COMMON_GNU=INCONCLUSIVE missing_tool=$tool" >&2
        exit 8
    }
done

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-common-gnu.XXXXXX")
finish() {
    if [[ ${MINILD_COMMON_GNU_KEEP:-0} == 1 ]]; then
        echo "MINILD_COMMON_GNU_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap finish EXIT

printf '.globl _start\n.text\n_start: ret\n.comm tentative,8,4\n' |
    as -o "$work/initial.o" -
printf '.comm tentative,24,64\n' |
    as -o "$work/larger.o" -
printf '.globl tentative\n.type tentative,@object\n.data\n.balign 64\ntentative: .quad 47\n.size tentative,8\n.globl archive_strong_marker\narchive_strong_marker: .quad 1\n' |
    as -o "$work/strong.o" -
printf '.weak tentative\n.type tentative,@object\n.data\ntentative: .quad 23\n.size tentative,8\n.globl archive_weak_marker\narchive_weak_marker: .quad 1\n' |
    as -o "$work/weak.o" -
printf '.comm tentative,24,64\n.globl archive_common_marker\n.data\narchive_common_marker: .quad 1\n' |
    as -o "$work/archive-common.o" -
ar rcs "$work/libstrong.a" "$work/strong.o"
ar rcs "$work/libweak.a" "$work/weak.o"
ar rcs "$work/libcommon.a" "$work/archive-common.o"

compare() {
    local label=$1
    local kind=$2
    shift 2

    ld -m elf_x86_64 "$@" -o "$work/$label.gnu.elf"
    "$MINILD" -m elf_x86_64 "$@" -o "$work/$label.mini.elf"
    python3 - "$kind" "$work/$label.gnu.elf" "$work/$label.mini.elf" <<'PY'
import struct
import sys
from pathlib import Path

kind = sys.argv[1]

def inspect(path):
    data = Path(path).read_bytes()
    if data[:6] != b'\x7fELF\x02\x01':
        raise SystemExit(f'{path}: expected ELF64 little-endian')
    e_type = struct.unpack_from('<H', data, 16)[0]
    shoff = struct.unpack_from('<Q', data, 40)[0]
    shentsize, count, shstrndx = struct.unpack_from('<HHH', data, 58)
    if shentsize != 64 or shstrndx >= count:
        raise SystemExit(f'{path}: invalid section table')
    sections = [struct.unpack_from('<IIQQQQIIQQ', data, shoff + i * shentsize)
                for i in range(count)]
    def bytes_of(s):
        return data[s[4]:s[4] + s[5]]
    shnames = bytes_of(sections[shstrndx])
    def string(pool, off):
        return pool[off:pool.index(b'\0', off)].decode()
    names = [string(shnames, s[0]) for s in sections]
    symtab = next(s for s in sections if s[1] == 2)
    strings = bytes_of(sections[symtab[6]])
    symbols = {}
    for off in range(symtab[4], symtab[4] + symtab[5], 24):
        st_name, info, other, shndx, value, size = struct.unpack_from(
            '<IBBHQQ', data, off)
        symbols[string(strings, st_name)] = (info, shndx, value, size)
    return e_type, sections, names, symbols

def validate(path):
    e_type, sections, names, symbols = inspect(path)
    if 'tentative' not in symbols:
        raise SystemExit(f'{path}: missing tentative symbol')
    info, shndx, value, size = symbols['tentative']
    if kind == 'rel':
        if e_type != 1 or (shndx, value, size) != (0xfff2, 64, 24):
            raise SystemExit(f'{path}: -r COMMON did not merge max size/align')
    elif kind in ('common', 'weak'):
        if e_type != 2 or size != (24 if kind == 'common' else 8):
            raise SystemExit(f'{path}: wrong final tentative size')
        if shndx >= len(sections) or names[shndx] != '.bss':
            raise SystemExit(f'{path}: COMMON did not win into .bss')
        if value % (64 if kind == 'common' else 4):
            raise SystemExit(f'{path}: final COMMON address misaligned')
        if sections[shndx][8] < (64 if kind == 'common' else 4):
            raise SystemExit(f'{path}: final .bss header alignment lost')
    elif kind == 'strong':
        if e_type != 2 or size != 8 or shndx >= len(sections) or \
           names[shndx] != '.data':
            raise SystemExit(f'{path}: regular strong definition did not win')
    else:
        raise SystemExit(f'invalid test case: {kind}')
    return symbols

gnu = validate(sys.argv[2])
mini = validate(sys.argv[3])
if kind == 'strong':
    # An archive strong definition must be extracted, not merely promoted.
    if sys.argv[2].endswith('archive-strong.gnu.elf'):
        if 'archive_strong_marker' not in gnu or \
           'archive_strong_marker' not in mini:
            raise SystemExit('strong archive member was not extracted')
if 'archive-weak.' in sys.argv[2]:
    if 'archive_weak_marker' in gnu or 'archive_weak_marker' in mini:
        raise SystemExit('weak archive member extracted for COMMON')
if 'archive-common.' in sys.argv[2]:
    if 'archive_common_marker' in gnu or 'archive_common_marker' in mini:
        raise SystemExit('COMMON archive member extracted for COMMON')
PY
}

compare rel-forward rel -r "$work/initial.o" "$work/larger.o"
compare rel-reverse rel -r "$work/larger.o" "$work/initial.o"
compare common-forward common -static "$work/initial.o" "$work/larger.o"
compare common-reverse common -static "$work/larger.o" "$work/initial.o"
compare strong-forward strong -static "$work/initial.o" "$work/strong.o"
compare strong-reverse strong -static "$work/strong.o" "$work/initial.o"
compare weak-forward weak -static "$work/initial.o" "$work/weak.o"
compare weak-reverse weak -static "$work/weak.o" "$work/initial.o"
compare archive-strong strong -static "$work/initial.o" "$work/libstrong.a"
compare archive-weak weak -static "$work/initial.o" "$work/libweak.a"
compare archive-common weak -static "$work/initial.o" "$work/libcommon.a"

# GNU BFD's shared COMMON precedence is exercised above by its canonical
# strong/weak/COMMON rules.  The only presently admitted MiniLD full ET_DYN
# target is RV64: ensure that the very same resolver winner is also projected
# into both .symtab and .dynsym without adding new target or shared features.
rv_as=${MINIAS:-$(dirname "$MINILD")/minic-as}
if [[ ! -x "$rv_as" ]]; then
    echo 'MINILD_COMMON_GNU=INCONCLUSIVE missing_minic_as_for_rv64_shared' >&2
    exit 8
fi
printf '.text\n.globl _start\n_start: ret\n.comm tentative,8,4\n' > "$work/rv-initial.s"
printf '.comm tentative,24,64\n' > "$work/rv-larger.s"
printf '.data\n.balign 64\n.globl tentative\n.type tentative,@object\ntentative: .dword 47\n.size tentative,8\n' > "$work/rv-strong.s"
for item in initial larger strong; do
    "$rv_as" -march=rv64i -mabi=lp64 -o "$work/rv-$item.o" \
        "$work/rv-$item.s"
done

for variant in common strong; do
  for ordering in forward reverse; do
    if [[ $variant == common ]]; then
        peer=larger
    else
        peer=strong
    fi
    if [[ $ordering == forward ]]; then
        first=initial
        second=$peer
    else
        first=$peer
        second=initial
    fi
    "$MINILD" -m elf64lriscv -shared \
        -o "$work/rv-$variant-$ordering.so" \
        "$work/rv-$first.o" "$work/rv-$second.o"
    python3 - "$work/rv-$variant-$ordering.so" "$variant" <<'PY'
import struct
import sys
from pathlib import Path

path, expected = sys.argv[1:]
data = Path(path).read_bytes()
if data[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<HH', data, 16) != (3, 243):
    raise SystemExit(f'{path}: expected RV64 ET_DYN')
shoff = struct.unpack_from('<Q', data, 40)[0]
entsize, count, shstr = struct.unpack_from('<HHH', data, 58)
sections = [struct.unpack_from('<IIQQQQIIQQ', data, shoff + entsize * i)
            for i in range(count)]
pool = data[sections[shstr][4]:sections[shstr][4] + sections[shstr][5]]
def cstr(s, o):
    return s[o:s.index(b'\0', o)].decode()
names = [cstr(pool, s[0]) for s in sections]
for symbol_type in (2, 11):
    table = next(s for s in sections if s[1] == symbol_type)
    strings = sections[table[6]]
    string_data = data[strings[4]:strings[4] + strings[5]]
    candidates = []
    for off in range(table[4], table[4] + table[5], 24):
        n, info, other, idx, value, size = struct.unpack_from('<IBBHQQ', data, off)
        if cstr(string_data, n) == 'tentative':
            candidates.append((idx, value, size))
    if len(candidates) != 1:
        raise SystemExit(f'{path}: missing/duplicate tentative in symbol table {symbol_type}')
    idx, value, size = candidates[0]
    section_name, expected_size = ('.bss', 24) if expected == 'common' else ('.data', 8)
    if idx >= count or names[idx] != section_name or size != expected_size or value % 64:
        raise SystemExit(f'{path}: shared winner missing size/alignment/section')
    if sections[idx][8] < 64:
        raise SystemExit(f'{path}: shared winner section header underaligned')
PY
  done
done

echo 'MINILD_COMMON_GNU=PASS modes=ET_REL_ET_EXEC_ET_DYN pairs=both_orders archives=strong_common_weak align=max'
