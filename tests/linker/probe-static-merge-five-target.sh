#!/usr/bin/env bash
# SHF_MERGE coordinate projection is shared by all five Linux ELF ABIs.
# MiniAS creates real ET_REL relocation inputs for each target. The generic
# merge data (two identical strings and two suffix references) must yield
# identical pointers, including the RV32 ELFCLASS32/ILP32 case.
set -Eeuo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
minias=${MINIAS:-"$repo/build/release/bin/minic-as"}
minild=${MINILD:-"$repo/build/release/bin/minic-ld"}
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-merge-five.XXXXXX")
trap 'rm -rf -- "$work"' EXIT

profiles=(
    'rv32 riscv rv32i ilp32 elf32lriscv'
    'rv64 riscv rv64i lp64 elf64lriscv'
    'x86 x86_64 x86-64 sysv elf_x86_64'
    'aarch64 aarch64 armv8-a lp64 aarch64linux'
    'loong loongarch64 loongarch64 lp64d elf64loongarch'
)
for profile in "${profiles[@]}"; do
    read -r name target march abi emulation <<< "$profile"
    for part in a b; do
        fixture="merge-cross-$part.S"
        [[ $name == rv32 ]] && fixture="merge-cross-rv32-$part.S"
        "$minias" "--target=$target" "-march=$march" "-mabi=$abi" \
            -o "$work/$name-$part.o" \
            "$repo/tests/linker/fixtures/$fixture"
    done
    "$minild" -m "$emulation" -static -e _start -o "$work/$name.elf" \
        "$work/$name-a.o" "$work/$name-b.o"
    python3 - "$work/$name.elf" "$name" <<'PY'
import re
import struct
import subprocess
import sys
from pathlib import Path

elf = Path(sys.argv[1])
name = sys.argv[2]
raw = elf.read_bytes()
width = 4 if raw[4] == 1 else 8
section_text = subprocess.check_output(
    ["readelf", "-SW", str(elf)], text=True)
m = re.search(r"\[\s*\d+\]\s+\.data\s+\S+\s+([0-9a-fA-F]+)\s+"
              r"([0-9a-fA-F]+)\s+([0-9a-fA-F]+)", section_text)
if not m:
    raise SystemExit(f"{name}: no data section")
data_addr, data_file, data_size = (int(field, 16) for field in m.groups())
symbols = subprocess.check_output(["nm", "-n", str(elf)], text=True)
bindings = {}
for line in symbols.splitlines():
    parts = line.split()
    if len(parts) == 3 and parts[2] in {
        'merge_cross_full_a', 'merge_cross_full_b',
        'merge_cross_tail_a', 'merge_cross_tail_b',
    }:
        bindings[parts[2]] = int(parts[0], 16)
if len(bindings) != 4:
    raise SystemExit(f"{name}: missing merged symbol bindings: {bindings}")

def pointer(symbol):
    offset = bindings[symbol] - data_addr
    if offset < 0 or offset + width > data_size:
        raise SystemExit(f"{name}: pointer outside data: {symbol}")
    fmt = '<I' if width == 4 else '<Q'
    return struct.unpack_from(fmt, raw, data_file + offset)[0]

first = pointer('merge_cross_full_a')
tail = pointer('merge_cross_tail_a')
if first != pointer('merge_cross_full_b') or \
        tail != pointer('merge_cross_tail_b') or tail != first + 7:
    raise SystemExit(f"{name}: merged/suffix pointers wrong: "
                     f"0x{first:x},0x{tail:x}")
PY
done
echo 'MINILD_STATIC_MERGE_FIVE=PASS rv32=1 rv64=1 x86_64=1 aarch64=1 loongarch64=1 reloc_symbol_addend=1'
