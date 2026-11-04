#!/usr/bin/env bash
# Verify Linux first-link audit on native ELF32 and ELF64 outputs, including
# mutations that preserve the four formerly checked ELF identity fields.
set -Eeuo pipefail
for tool in as ld python3; do
    command -v "$tool" >/dev/null || {
        echo "MINILD_FINAL_ELF_AUDIT_TEST=INCONCLUSIVE missing=$tool" >&2
        exit 8
    }
done
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-final-audit.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
cat >"$work/source.S" <<'ASM'
.section .text,"ax",@progbits
.globl _start
_start:
    nop
    ret
.section .rodata,"a",@progbits
    .ascii "test"
.section .data,"aw",@progbits
    .long 0x12345678
.section .bss,"aw",@nobits
    .space 32
ASM
for bits in 32 64; do
    if [[ $bits == 32 ]]; then
        as --32 -o "$work/object.o" "$work/source.S"
        ld -m elf_i386 -static -o "$work/reference.elf" "$work/object.o"
    else
        as --64 -o "$work/object.o" "$work/source.S"
        ld -m elf_x86_64 -static -o "$work/reference.elf" "$work/object.o"
    fi
    cp "$work/reference.elf" "$work/candidate.elf"
    python3 tools/ci/minild-final-elf-audit.py \
        "$work/reference.elf" "$work/candidate.elf"
    python3 - "$work/reference.elf" "$work" "$bits" <<'PY'
from pathlib import Path
import struct
import sys

original = bytearray(Path(sys.argv[1]).read_bytes())
out = Path(sys.argv[2])
is64 = sys.argv[3] == "64"

# Same ELF class/machine/type but wrong entry point.
wrong_entry = original.copy()
struct.pack_into("<Q" if is64 else "<I", wrong_entry, 24, 0)
(out / "entry.elf").write_bytes(wrong_entry)

# A PT_LOAD whose file extent lies beyond EOF must be rejected.
bad_load = original.copy()
phoff = struct.unpack_from("<Q" if is64 else "<I", original,
                           32 if is64 else 28)[0]
struct.pack_into("<Q" if is64 else "<I", bad_load,
                 phoff + (32 if is64 else 16), len(original) + 1)
(out / "load.elf").write_bytes(bad_load)

# Break SHF_ALLOC section placement without changing the ELF header.
bad_section = original.copy()
shoff = struct.unpack_from("<Q" if is64 else "<I", original,
                           40 if is64 else 32)[0]
shentsize = struct.unpack_from("<H", original,
                               58 if is64 else 46)[0]
count = struct.unpack_from("<H", original, 60 if is64 else 48)[0]
for index in range(1, count):
    offset = shoff + index * shentsize
    flags = struct.unpack_from("<Q" if is64 else "<I", original,
                               offset + 8)[0]
    if flags & 2:
        struct.pack_into("<Q" if is64 else "<I", bad_section,
                         offset + (16 if is64 else 12), 0)
        break
else:
    raise SystemExit("no SHF_ALLOC fixture section")
(out / "section.elf").write_bytes(bad_section)
PY
    for invalid in entry load section; do
        if python3 tools/ci/minild-final-elf-audit.py \
            "$work/reference.elf" "$work/$invalid.elf" \
            >"$work/$invalid.stdout" 2>"$work/$invalid.stderr"; then
            echo "MINILD_FINAL_ELF_AUDIT_TEST=FAIL admitted=$bits:$invalid" >&2
            exit 1
        fi
        grep -q 'MINILD_FINAL_ELF_AUDIT=FAIL' \
            "$work/$invalid.stderr" || {
            echo "MINILD_FINAL_ELF_AUDIT_TEST=FAIL diagnostics=$bits:$invalid" >&2
            exit 1
        }
    done
    echo "MINILD_FINAL_ELF_AUDIT_TEST=PASS elf=$bits positive=1 negatives=3"
done
