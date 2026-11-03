#!/usr/bin/env bash
# Host-runnable MiniELF -> MiniLD source-section ownership gate.
# The input has three unrelated SHF_ALLOC+SHF_EXECINSTR sections named .text,
# no STT_SECTION symbols, distinct alignment, and an unreachable undefined
# R_RISCV_JAL edge. The strong definition follows a weak one in input order.
# Requires no RISC-V GNU assembler, RISC-V GNU linker, QEMU, or Linux cache.
set -Eeuo pipefail

: "${MINILD:?MINILD must point to minic-ld}"
: "${INPUT_SECTION_GC_FIXTURE:?fixture generator binary is required}"
for tool in python3 readelf; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "MINILD_GC_INPUT=INCONCLUSIVE missing_tool=$tool" >&2
        exit 8
    }
done

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-input-section-gc.XXXXXX")
finish() {
    if [[ "${MINILD_GC_INPUT_KEEP:-0}" == 1 ]]; then
        echo "MINILD_GC_INPUT_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap finish EXIT

"$INPUT_SECTION_GC_FIXTURE" "$work/strong.o" "$work/weak.o"

python3 - "$work/strong.o" "$work/weak.o" <<'PY'
import re
import subprocess
import sys

for path, expected_count, expected_align in (
    (sys.argv[1], 3, [4, 16, 64]),
    (sys.argv[2], 1, [8]),
):
    sections = subprocess.check_output(["readelf", "-W", "-S", path], text=True)
    alignments = []
    for line in sections.splitlines():
        # ELF64 readelf -W: [Nr] Name Type Address Off Size ES Flg Lk Inf Al
        match = re.match(r"^\s*\[\s*\d+\]\s+\.text\s+PROGBITS\s+", line)
        if match:
            alignments.append(int(line.split()[-1]))
    if len(alignments) != expected_count or sorted(alignments) != expected_align:
        raise SystemExit(
            f"{path}: expected {expected_count} .text section headers with"
            f" alignment {expected_align}, found {alignments}"
        )
    symbols = subprocess.check_output(["readelf", "-W", "-s", path], text=True)
    if re.search(r"\sSECTION\s+", symbols):
        raise SystemExit(f"{path}: producer emitted forbidden STT_SECTION")

relocations = subprocess.check_output(
    ["readelf", "-W", "-r", sys.argv[1]], text=True
)
if len(re.findall(r"R_RISCV_JAL\s+", relocations)) != 2:
    raise SystemExit("strong.o: expected two R_RISCV_JAL input relocations")
for name in ("live", "missing_dead"):
    if not re.search(r"R_RISCV_JAL\s+\S+\s+" + name + r"\b", relocations):
        raise SystemExit(f"strong.o: missing relocation against {name}")
print("MINILD_GC_INPUT_SOURCE=PASS input_sections=4 same_name=3 source_section_symbols=0 undefined_dead_reloc=1")
PY

if ! "$MINILD" -melf64lriscv -static --gc-sections -e _start \
    -o "$work/output.elf" "$work/weak.o" "$work/strong.o" \
    >"$work/link.stdout" 2>"$work/link.stderr"; then
    echo 'MINILD_GC_INPUT=FAIL stage=link' >&2
    sed -n '1,30p' "$work/link.stderr" >&2
    exit 1
fi

python3 - "$work/output.elf" <<'PY'
import re
import struct
import subprocess
import sys
from pathlib import Path

path = Path(sys.argv[1])
data = path.read_bytes()
if data[:6] != b"\x7fELF\x02\x01":
    raise SystemExit("final output is not ELF64 little-endian")
if struct.unpack_from("<HH", data, 16) != (2, 243):
    raise SystemExit("final output is not RISC-V ET_EXEC")

symbols = subprocess.check_output(["readelf", "-W", "-s", str(path)], text=True)
def defined(name):
    result = []
    for line in symbols.splitlines():
        columns = line.split()
        if len(columns) >= 8 and columns[-1] == name and columns[6] != "UND":
            result.append(columns)
    return result

for name in ("_start", "live"):
    entries = defined(name)
    if len(entries) != 1 or entries[0][4] != "GLOBAL":
        raise SystemExit(f"{path}: expected one strong {name}, found {entries}")
for name in ("dead", "missing_dead"):
    if defined(name):
        raise SystemExit(f"{path}: unreachable symbol {name} survived")

offset = struct.unpack_from("<Q", data, 40)[0]
entry_size = struct.unpack_from("<H", data, 58)[0]
count = struct.unpack_from("<H", data, 60)[0]
names_index = struct.unpack_from("<H", data, 62)[0]
if entry_size != 64 or names_index >= count:
    raise SystemExit("invalid output section table")
headers = [struct.unpack_from("<IIQQQQIIQQ", data, offset + 64 * i)
           for i in range(count)]
names_header = headers[names_index]
names = data[names_header[4]:names_header[4] + names_header[5]]
def name_at(index):
    end = names.find(b"\x00", index)
    return names[index:end].decode("ascii")

text_bytes = b"".join(
    data[sh[4]:sh[4] + sh[5]] for sh in headers
    # The linker may temporarily retain unique output section names for
    # independently collected same-name source sections.
    if name_at(sh[0]).startswith(".text") and sh[1] == 1
)
if b"\x13\x05\x70\x00" not in text_bytes:
    raise SystemExit("strong live function bytes absent")
if b"\x13\x05\x60\x01" in text_bytes:
    raise SystemExit("weak live function bytes erroneously survived")
if b"\x13\x05\xc0\x02" in text_bytes:
    raise SystemExit("unreachable dead function bytes survived")
print("MINILD_GC_INPUT=PASS same_named_sections=3 no_STT_SECTION=1 dead_reloc_discarded=1 strong_over_weak=1")
PY
