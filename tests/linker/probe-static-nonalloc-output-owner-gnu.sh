#!/usr/bin/env bash
# Static non-ALLOC relocations must use final OutputSection-relative addresses
# in exactly the same view as symbols and shared relocations.  A two-object
# scripted section proves that source st_value alone is not sufficient.
set -Eeuo pipefail

: "${MINILD:?MINILD must point to minic-ld}"
gnu_ld=${GNU_LD:-ld.bfd}
for tool in as "$gnu_ld" objcopy readelf cmp python3; do
    command -v "$tool" >/dev/null || {
        echo "MINILD_STATIC_NONALLOC_OUTPUT_GNU=INCONCLUSIVE missing_tool=$tool" >&2
        exit 8
    }
done

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-nonalloc-output.XXXXXX")
trap 'rm -rf -- "$work"' EXIT

printf '.text\n.globl _start\n_start: ret\n.section .debug_owned,"",@progbits\n.quad 111\n' |
    as -o "$work/first.o" -
printf '.section .debug_owned,"",@progbits\n.globl debug_global\ndebug_global: .quad 222\n.section .debug_ref,"",@progbits\n.quad debug_global\n.long debug_global - .\n' |
    as -o "$work/second.o" -

cat > "$work/link.lds" <<'LDS'
SECTIONS {
  . = 0x10000;
  .text : { *(.text) }
  .debug_owned 0 : { *(.debug_owned) }
  .debug_ref 0 : { *(.debug_ref) }
}
LDS

for mode in default scripted; do
    flags=()
    if [[ $mode == scripted ]]; then
        flags=(-T "$work/link.lds")
    fi
    "$gnu_ld" -m elf_x86_64 -static "${flags[@]}" \
        -o "$work/$mode.gnu.elf" "$work/first.o" "$work/second.o"
    "$MINILD" -m elf_x86_64 -static "${flags[@]}" \
        -o "$work/$mode.mini.elf" "$work/first.o" "$work/second.o"
    for flavor in gnu mini; do
        objcopy --dump-section ".debug_owned=$work/$mode.$flavor.owned" \
            --dump-section ".debug_ref=$work/$mode.$flavor.ref" \
            "$work/$mode.$flavor.elf"
    done
    cmp "$work/$mode.gnu.owned" "$work/$mode.mini.owned"
    cmp "$work/$mode.gnu.ref" "$work/$mode.mini.ref"
    python3 - "$mode" "$work/$mode.gnu.ref" "$work/$mode.mini.ref" \
        "$work/$mode.gnu.elf" "$work/$mode.mini.elf" <<'PY'
from pathlib import Path
import struct
import subprocess
import sys

mode, gnu_ref, mini_ref, gnu_elf, mini_elf = sys.argv[1:]
for filename in (gnu_ref, mini_ref):
    payload = Path(filename).read_bytes()
    if len(payload) != 12:
        raise SystemExit(f'{mode}: wrong debug relocation source size')
    pointer, pc_relative = struct.unpack('<Qi', payload)
    if (pointer, pc_relative) != (8, 0):
        raise SystemExit(f'{mode}: wrong nonalloc final S/P ({pointer}, {pc_relative})')
for filename in (gnu_elf, mini_elf):
    output = subprocess.check_output(['readelf', '-Ws', filename], text=True)
    symbols = [line.split() for line in output.splitlines()
               if line.split() and line.split()[-1] == 'debug_global']
    if len(symbols) != 1 or int(symbols[0][1], 16) != 8:
        raise SystemExit(f'{mode}: .symtab uses different nonalloc offset')
PY
done

# The source is ALLOC here; treating a non-ALLOC target as an absolute VMA
# would leak an unmapped address into runtime data.  Preserve fail-closed.
printf '.text\n.globl _start\n_start: ret\n.data\n.quad debug_global\n.section .debug_owned,"",@progbits\n.globl debug_global\ndebug_global: .quad 4\n' |
    as -o "$work/invalid-runtime-ref.o" -
if "$MINILD" -m elf_x86_64 -static -o "$work/invalid.elf" \
    "$work/invalid-runtime-ref.o" >"$work/invalid.log" 2>&1; then
    echo 'MINILD_STATIC_NONALLOC_OUTPUT_GNU=FAIL allocated_reference_accepted' >&2
    exit 1
fi
grep -Fq 'unsupported-static-symbol:debug_global' "$work/invalid.log"
test ! -e "$work/invalid.elf"

echo 'MINILD_STATIC_NONALLOC_OUTPUT_GNU=PASS GNU_BFD=2 modes=default_scripted global_symbols=2 cross_fragment=8 pcrel=0 allocated_to_nonalloc=rejected'
