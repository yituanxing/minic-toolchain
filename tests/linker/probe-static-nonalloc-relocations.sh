#!/usr/bin/env bash
# Default-layout static ELF debug relocations: local symbols in non-ALLOC
# sections have section-relative values, not runtime virtual addresses.
# Cross-object merged debug sections, GNU BFD parity, and both dangerous
# relaxations (ALLOC -> debug, unresolved strong) are tested together.
set -Eeuo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
minias=${MINIAS:-"$repo/build/release/bin/minic-as"}
minild=${MINILD:-"$repo/build/release/bin/minic-ld"}
gnu_ld=${GNU_LD:-ld.bfd}
for tool in "$minias" "$minild" "$gnu_ld" readelf objcopy python3 timeout; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "MINILD_STATIC_NONALLOC=FAIL missing_tool=$tool" >&2
        exit 1
    }
done
"$gnu_ld" --version | grep -Fq 'GNU ld' || {
    echo 'MINILD_STATIC_NONALLOC=FAIL GNU_reference_is_not_BFD' >&2
    exit 1
}

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-static-nonalloc.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
prefix="$repo/tests/linker/fixtures/static-nonalloc-debug"
for part in main extra negative undef; do
    "$minias" --target=x86_64 -march=x86-64 -mabi=sysv \
        -o "$work/$part.o" "$prefix-$part.S"
done

for flavor in gnu mini; do
    if [[ $flavor == gnu ]]; then linker=$gnu_ld; else linker=$minild; fi
    "$linker" -m elf_x86_64 -static -e _start \
        -o "$work/$flavor.elf" "$work/main.o" "$work/extra.o" \
        >"$work/$flavor.log" 2>&1 || {
            echo "MINILD_STATIC_NONALLOC=FAIL link=$flavor" >&2
            sed -n '1,30p' "$work/$flavor.log" >&2
            exit 1
        }
    objcopy --dump-section ".debug_info=$work/$flavor.debug_info" \
        "$work/$flavor.elf"
    entry=$(readelf -h "$work/$flavor.elf" |
        awk '/Entry point address:/ {print $4}')
    python3 - "$work/$flavor.debug_info" "$entry" "$flavor" <<'PY'
import pathlib
import struct
import sys

data = pathlib.Path(sys.argv[1]).read_bytes()
entry = int(sys.argv[2], 0)
if len(data) != 20:
    raise SystemExit(f'{sys.argv[3]}: unexpected debug payload: {len(data)}')
first, second, alloc_address, extra = struct.unpack('<IIQI', data)
if (first, second, extra) != (3, 4, 7):
    raise SystemExit(
        f'{sys.argv[3]}: debug-local relocations mismatch: '
        f'{first},{second},{extra}')
if alloc_address != entry:
    raise SystemExit(
        f'{sys.argv[3]}: debug-to-allocated relocation mismatch: '
        f'0x{alloc_address:x} != 0x{entry:x}')
PY
    timeout 3 "$work/$flavor.elf" >/dev/null
done

if "$minild" -m elf_x86_64 -static -e _start \
    -o "$work/bad-alloc.elf" "$work/negative.o" \
    >"$work/bad-alloc.log" 2>&1; then
    echo 'MINILD_STATIC_NONALLOC=FAIL accepted_ALLOC_to_nonALLOC' >&2
    exit 1
fi
grep -Fq 'unsupported-static-symbol:' "$work/bad-alloc.log" || {
    echo 'MINILD_STATIC_NONALLOC=FAIL missing_ALLOC_rejection_diagnostic' >&2
    sed -n '1,20p' "$work/bad-alloc.log" >&2
    exit 1
}

if "$minild" -m elf_x86_64 -static -e _start \
    -o "$work/bad-undef.elf" "$work/main.o" "$work/undef.o" \
    >"$work/bad-undef.log" 2>&1; then
    echo 'MINILD_STATIC_NONALLOC=FAIL accepted_strong_debug_undefined' >&2
    exit 1
fi
grep -Fq 'undefined-reference:definitely_missing_debug_symbol' \
    "$work/bad-undef.log" || {
    echo 'MINILD_STATIC_NONALLOC=FAIL missing_strong_undefined_diagnostic' >&2
    sed -n '1,20p' "$work/bad-undef.log" >&2
    exit 1
}

# A malformed relocation's symbol index must be rejected before the new
# non-ALLOC helper can dereference the resolver table.
python3 - "$work/main.o" "$work/bad-symbol-index.o" <<'PY'
import pathlib
import struct
import sys

raw = bytearray(pathlib.Path(sys.argv[1]).read_bytes())
if raw[:6] != b'\x7fELF\x02\x01':
    raise SystemExit('fixture is not ELF64 little endian')
shoff = struct.unpack_from('<Q', raw, 40)[0]
shentsize, shnum, shstrndx = struct.unpack_from('<HHH', raw, 58)
headers = [shoff + i * shentsize for i in range(shnum)]
names_off = struct.unpack_from('<Q', raw, headers[shstrndx] + 24)[0]

def name(at):
    start = names_off + struct.unpack_from('<I', raw, at)[0]
    return bytes(raw[start:raw.index(0, start)]).decode('ascii')

found = False
for at in headers:
    if name(at) != '.rela.debug_info':
        continue
    offset = struct.unpack_from('<Q', raw, at + 24)[0]
    old = struct.unpack_from('<Q', raw, offset + 8)[0]
    struct.pack_into('<Q', raw, offset + 8,
                     (0xFFFFFFFE << 32) | (old & 0xFFFFFFFF))
    found = True
    break
if not found:
    raise SystemExit('missing debug relocation section')
pathlib.Path(sys.argv[2]).write_bytes(raw)
PY
if "$minild" -m elf_x86_64 -static -e _start \
    -o "$work/bad-index.elf" "$work/bad-symbol-index.o" \
    >"$work/bad-index.log" 2>&1; then
    echo 'MINILD_STATIC_NONALLOC=FAIL accepted_out_of_range_symbol_index' >&2
    exit 1
fi
grep -Eq 'relocation-symbol-out-of-range|invalid.*symbol' \
    "$work/bad-index.log" || {
    echo 'MINILD_STATIC_NONALLOC=FAIL missing_bad_index_diagnostic' >&2
    sed -n '1,20p' "$work/bad-index.log" >&2
    exit 1
}

echo 'MINILD_STATIC_NONALLOC=PASS merged_debug_fragments=2 local_nonalloc_relocs=3 debug_to_alloc=1 gnu_bfd_parity=1 runtime=2 alloc_to_nonalloc=rejected strong_undefined=rejected bad_symbol_index=rejected'
