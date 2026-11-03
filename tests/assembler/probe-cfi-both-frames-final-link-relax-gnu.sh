#!/usr/bin/env bash
# CFI correctness is a linked-PC property.  A relaxed CALL shortens the code
# range between two CFI anchors; BOTH .eh_frame and .debug_frame must follow
# the final instruction addresses, not retain assembler-time row addresses.
set -Eeuo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
: "${MINIAS:=$repo/build/release/bin/minic-as}"
test -x "$MINIAS"

for tool in readelf python3; do
  command -v "$tool" >/dev/null
done

work=$(mktemp -d)
if [[ ${MINIAS_CFI_KEEP_WORK:-0} == 1 ]]; then
  echo "MINIAS_CFI_FINAL_LINK_WORK=$work"
else
  trap 'rm -rf -- "$work"' EXIT
fi

cat_rv_source() {
  # Use printf rather than reusing compiler output: identical handwritten CFI
  # ensures the GNU and MiniAS linkers see the same source event stream.
  printf '%s\n' \
    '.cfi_sections .eh_frame, .debug_frame' \
    '.option push' \
    '.option relax' \
    '.text' \
    '.globl _start' \
    '.type _start,@function' \
    '_start:' \
    '  .cfi_startproc simple' \
    '  .cfi_def_cfa 2, 0' \
    '  call target' \
    'after_call:' \
    '  .cfi_def_cfa_offset 16' \
    '  addi x0,x0,0' \
    'target:' \
    '  .cfi_def_cfa_offset 24' \
    '  ret' \
    '  .cfi_endproc' \
    '.size _start,.-_start' \
    '.option pop'
}

cat_rv_source >"$work/rv.s"

check_object_fixups() {
  local reloc_file=$1 prefix=$2 positive=$3
  readelf -Wr "$reloc_file" >"$reloc_file.relocations"
  python3 - "$reloc_file.relocations" "$prefix" "$positive" <<'PY'
from pathlib import Path
import re
import sys

text = Path(sys.argv[1]).read_text()
prefix, positive = sys.argv[2:]
for section in ('.rela.eh_frame', '.rela.debug_frame'):
    m = re.search(r"Relocation section '" + re.escape(section) +
                  r"'.*?(?=Relocation section|\Z)", text, re.S)
    assert m, (sys.argv[1], section, text)
    types = re.findall(r'\bR_' + re.escape(prefix) + r'_\w+\b', m.group())
    assert types.count('R_' + prefix + '_' + positive) >= 1, (section, types)
    assert types.count('R_' + prefix + '_SUB6') >= 1, (section, types)
print(f'MINIAS_CFI_FINAL_LINK=PASS object={sys.argv[1]} '
      'sections=2 code_advance_6bit_pairs=2')
PY
}

check_linked_frames() {
  local elf=$1 producer=$2 mode=$3
  readelf -Ws "$elf" >"$elf.symbols"
  readelf --debug-dump=frames "$elf" >"$elf.frames"
  python3 - "$elf.symbols" "$elf.frames" "$producer" "$mode" <<'PY'
from pathlib import Path
import re
import sys

symbols, frames, producer, mode = sys.argv[1:]
values = {}
start_size = None
for line in Path(symbols).read_text().splitlines():
    fields = line.split()
    if len(fields) >= 8 and fields[-1] in ('_start', 'after_call', 'target'):
        values[fields[-1]] = int(fields[1], 16)
        if fields[-1] == '_start':
            start_size = int(fields[2], 10)
assert len(values) == 3, (producer, mode, values)
expected_call = 4 if mode == 'relax' else 8
assert values['after_call'] - values['_start'] == expected_call, \
    (producer, mode, 'CALL was not appropriately shortened', values)
# .size is an ELF symbol-table size, not fixed emitted data.  The linker must
# shrink it when a RELAX relocation deletes instructions inside the function.
assert start_size == values['target'] + 4 - values['_start'], \
    (producer, mode, 'linked st_size does not cover final function',
     start_size, values)

text = Path(frames).read_text()
# Ignore CIE initial CFA settings; FDE rows have their own linked PC and must
# map the two *post-instruction* CFI state transitions to exactly the same
# labels the linker chose after deleting bytes.
blocks = {}
current = None
for line in text.splitlines():
    match = re.search(r'Contents of the (\.(?:eh|debug)_frame) section', line)
    if match:
        current = match.group(1)
        blocks[current] = []
        continue
    if current and 'DW_CFA_advance_loc:' in line:
        address = re.search(r'\bto\s+(?:0x)?([0-9a-fA-F]+)\b', line)
        if address:
            blocks[current].append(int(address.group(1), 16))

for section in ('.eh_frame', '.debug_frame'):
    got = blocks.get(section, [])
    assert len(got) >= 2, (producer, mode, section, blocks, text)
    assert got[:2] == [values['after_call'], values['target']], \
        (producer, mode, section, 'linked CFI row PCs wrong', got, values)
print(f'MINIAS_CFI_FINAL_LINK=PASS producer={producer} mode={mode} '
      'linked_eh_frame=exact linked_debug_frame=exact '
      f'linked_st_size={start_size} call_bytes={expected_call}')
PY
}

rv_as=${RVAS:-riscv64-linux-gnu-as}
rv_ld=${RVLD:-riscv64-linux-gnu-ld}
if ! command -v "$rv_as" >/dev/null || ! command -v "$rv_ld" >/dev/null; then
  if [[ ${REQUIRE_ALL_GNU:-0} == 1 ]]; then
    echo 'MINIAS_CFI_FINAL_LINK=BLOCKED missing=RISC-V GNU as/ld' >&2
    exit 1
  fi
  echo 'MINIAS_CFI_FINAL_LINK=SKIP RISC-V cross GNU as/ld unavailable'
  exit 0
fi

for bits in 32 64; do
  if ((bits == 32)); then
    march=rv32i2p1 mabi=ilp32 emulation=elf32lriscv
  else
    march=rv64i2p1 mabi=lp64 emulation=elf64lriscv
  fi
  "$MINIAS" --target=riscv "-march=$march" "-mabi=$mabi" \
    -o "$work/mini-$bits.o" "$work/rv.s"
  "$rv_as" "-march=$march" "-mabi=$mabi" -mrelax \
    -o "$work/gnu-$bits.o" "$work/rv.s"
  check_object_fixups "$work/mini-$bits.o" RISCV SET6
  check_object_fixups "$work/gnu-$bits.o" RISCV SET6
  for producer in mini gnu; do
    for mode in relax no-relax; do
      "$rv_ld" -m "$emulation" "--$mode" -e _start \
        -o "$work/$producer-$bits-$mode.elf" "$work/$producer-$bits.o"
      check_linked_frames "$work/$producer-$bits-$mode.elf" \
        "$producer-rv$bits" "$mode"
    done
  done
done

echo 'MINIAS_CFI_FINAL_LINK=PASS profiles=rv32,rv64 modes=relax,no-relax frames=eh,debug final_CFI_PCs=exact'
