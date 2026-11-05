#!/usr/bin/env bash
# GNU -n/--nmagic is a real ELF file-layout policy, not an ignorable flag.
# This is a bare static linker-script parity test, not a Linux mmap/QEMU
# user-process test: nmagic deliberately permits p_align=1 LOADs.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
for bin in as ld python3; do
    command -v "$bin" >/dev/null || {
        echo "MINILD_NMAGIC=INCONCLUSIVE missing=$bin" >&2
        exit 8
    }
done
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-nmagic.XXXXXX")
finish() {
    if [[ ${MINILD_NMAGIC_KEEP:-0} == 1 ]]; then
        echo "MINILD_NMAGIC_ARTIFACTS=$work"
    else
        rm -r -- "$work"
    fi
}
trap finish EXIT

cat >"$work/native.s" <<'ASM'
.section .text.entry,"ax",@progbits
.globl _start
_start:
  nop
  ret
.section .rodata.literal,"a",@progbits
  .ascii "nmagic-rodata\0"
.section .data.word,"aw",@progbits
  .quad 0x1122334455667788
ASM
cat >"$work/riscv.s" <<'ASM'
.section .text.entry,"ax",@progbits
.globl _start
_start:
  addi a0,zero,0
  ret
.section .rodata.literal,"a",@progbits
  .ascii "nmagic-rodata\0"
.section .data.word,"aw",@progbits
  .dword 0x1122334455667788
ASM
cat >"$work/native.lds" <<'LDS'
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text.entry) }
  .rodata : { *(.rodata.literal) }
  .data : { *(.data.word) }
}
LDS
cat >"$work/riscv.lds" <<'LDS'
OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text.entry) }
  .rodata : { *(.rodata.literal) }
  .data : { *(.data.word) }
}
LDS
as -o "$work/native.o" "$work/native.s"
"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/riscv.o" "$work/riscv.s"
ld -m elf_x86_64 -n -T "$work/native.lds" -o "$work/gnu.elf" \
    "$work/native.o"
"$MINILD" -melf64lriscv -n -T "$work/riscv.lds" \
    -o "$work/minild.elf" "$work/riscv.o"
"$MINILD" -melf64lriscv --nmagic -T "$work/riscv.lds" \
    -o "$work/minild-long.elf" "$work/riscv.o"
cmp "$work/minild.elf" "$work/minild-long.elf"

if "$MINILD" -melf64lriscv -n -o "$work/no-script.elf" \
    "$work/riscv.o" >"$work/negative.stdout" 2>"$work/negative.stderr"; then
    echo 'MINILD_NMAGIC=FAIL unscripted_accepted' >&2
    exit 1
fi
grep -q -- '--nmagic-requires-static-script' "$work/negative.stderr"
if "$MINILD" -melf64lriscv -N -T "$work/riscv.lds" \
    -o "$work/omagic.elf" "$work/riscv.o" \
    >"$work/omagic.stdout" 2>"$work/omagic.stderr"; then
    echo 'MINILD_NMAGIC=FAIL omagic_silently_accepted' >&2
    exit 1
fi
grep -q 'unsupported-option:-N' "$work/omagic.stderr"

python3 - "$work/gnu.elf" "$work/minild.elf" <<'PY'
from pathlib import Path
import struct
import sys


def read_elf(path):
    raw = Path(path).read_bytes()
    assert raw[:5] == b'\x7fELF\x02', path
    endian = '<' if raw[5] == 1 else '>'
    phoff, shoff = struct.unpack_from(endian + 'QQ', raw, 32)
    phentsize, phnum, shentsize, shnum, shstrndx = struct.unpack_from(
        endian + 'HHHHH', raw, 54)
    ph = [struct.unpack_from(endian + 'IIQQQQQQ', raw,
                             phoff + phentsize * i) for i in range(phnum)]
    sh = [struct.unpack_from(endian + 'IIQQQQIIQQ', raw,
                             shoff + shentsize * i) for i in range(shnum)]
    strings = sh[shstrndx]
    string_table = raw[strings[4]:strings[4] + strings[5]]
    result = {}
    for sec in sh:
        off = sec[0]
        name = string_table[off:string_table.find(b'\0', off)].decode()
        if name in {'.text', '.rodata', '.data'}:
            result[name] = {
                'addr': sec[3], 'offset': sec[4], 'size': sec[5],
                'payload': raw[sec[4]:sec[4] + sec[5]],
            }
    assert len(result) == 3, (path, result)
    return [segment for segment in ph if segment[0] == 1], result


gnu_ph, gnu = read_elf(sys.argv[1])
mini_ph, mini = read_elf(sys.argv[2])
for label, ph, secs in [('GNU', gnu_ph, gnu),
                        ('MiniLD', mini_ph, mini)]:
    # Unlike ordinary user processes, BFD -n deliberately emits a single
    # permission-union LOAD (RWE) across adjacent executable, constant and
    # writable script sections. The firmware/kernel image is not mmap-safe.
    assert len(ph) == 1 and ph[0][1] == 7 and ph[0][7] == 1, (label, ph)
    load = ph[0]
    for name in ('.text', '.rodata', '.data'):
        section = secs[name]
        assert (load[3] <= section['addr'] and
                section['addr'] + section['size'] <= load[3] + load[6] and
                load[2] <= section['offset'] and
                section['offset'] + section['size'] <= load[2] + load[5]), (
                    label, name, load, section)
for name in ('.rodata', '.data'):
    assert mini[name]['payload'] == gnu[name]['payload'], name
for secs in (gnu, mini):
    # '-n' must NOT insert an implicit page boundary between adjacent
    # script output sections whose VMA addresses are in one 4K page.
    assert secs['.text']['offset'] < 4096, secs
    assert secs['.data']['offset'] - secs['.text']['offset'] < 4096, secs
    assert secs['.data']['addr'] - secs['.text']['addr'] < 4096, secs
print('MINILD_NMAGIC=PASS native_GNU_nmagic=true '
      'mini_short_and_long_flags=true RWE_one_LOAD=true '
      'p_align=1 no_page_spacing=true '
      'rodata_and_data=GNU_byte_parity negative_modes=PASS')
print('MINILD_NMAGIC_FILE_BASE gnu=0x%x mini=0x%x' %
      (gnu['.text']['offset'], mini['.text']['offset']))
PY
