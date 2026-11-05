#!/usr/bin/env bash
# Linux __tracepoints uses the x86-64 S+A-P 64-bit relocation, not PC32.
set -Eeuo pipefail
: "${MINILD:?MINILD must name the exact linker}"
for cmd in as ld readelf objcopy python3; do
    command -v "$cmd" >/dev/null || exit 8
done
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-x86-pc64.XXXXXXXX")
finish() {
    if [[ ${MINILD_PC64_KEEP:-0} == 1 ]]; then
        echo "MINILD_PC64_ARTIFACTS=$work"
    else
        rm -r -- "$work"
    fi
}
trap finish EXIT

printf '%s\n' \
    '.section .text,"ax",@progbits' \
    '.globl _start' \
    '_start:' \
    ' mov $60,%eax' \
    ' xor %edi,%edi' \
    ' syscall' \
    '.section .rodata,"a",@progbits' \
    '.globl tracepoint_site' \
    'tracepoint_site:' \
    ' .quad _start - .' \
    >"$work/input.s"
as -o "$work/input.o" "$work/input.s"
readelf -Wr "$work/input.o" | grep -Fq R_X86_64_PC64

for linker in ld "$MINILD"; do
    owner=mini
    [[ $linker != ld ]] || owner=gnu
    "$linker" -m elf_x86_64 -static --emit-relocs -o "$work/$owner.elf" \
        "$work/input.o" >"$work/$owner.log" 2>&1
    "$work/$owner.elf"
    readelf -Wr "$work/$owner.elf" >"$work/$owner.rela"
    grep -Fq R_X86_64_PC64 "$work/$owner.rela"
    objcopy --dump-section .rodata="$work/$owner.rodata" \
        "$work/$owner.elf"
done

python3 - "$work/gnu.elf" "$work/mini.elf" \
           "$work/gnu.rodata" "$work/mini.rodata" <<'PY'
import re
import subprocess
import sys

def read_symbol(path, symbol):
    text = subprocess.check_output(['readelf', '-Ws', path], text=True)
    for line in text.splitlines():
        m = re.match(r'^\s*\d+:\s+([0-9a-fA-F]+)\s+\d+\s+\S+\s+\S+\s+\S+\s+\S+\s+(\S+)$', line)
        if m and m.group(2) == symbol:
            return int(m.group(1), 16)
    raise SystemExit('X86_PC64=FAIL missing_symbol:' + symbol)

for elf, data in zip(sys.argv[1:3], sys.argv[3:5]):
    with open(data, 'rb') as handle:
        actual = int.from_bytes(handle.read(8), 'little')
    expected = (read_symbol(elf, '_start') -
                read_symbol(elf, 'tracepoint_site')) & ((1 << 64) - 1)
    if actual != expected:
        raise SystemExit(f'X86_PC64=FAIL value_mismatch:{elf}:{actual:x}:{expected:x}')
print('X86_PC64=PASS GNU_minild_relocation_semantics=2/2 final_emit_relco=2/2')
PY
