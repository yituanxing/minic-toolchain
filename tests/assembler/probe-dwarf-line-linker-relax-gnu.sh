#!/usr/bin/env bash
# GNU GAS 2.42's source-owned DWARF v4 line-program contract on targets whose
# linker can delete instructions.  This probe deliberately checks .debug_line
# separately from CFI: its address deltas use DW_LNS_fixed_advance_pc and
# ADD16/SUB16, not the CFI short-opcode relocation family.
set -Eeuo pipefail

: "${MINIAS:?Set MINIAS to the exact-head minic-as executable}"
python=${PYTHON:-python3}
for tool in "$python" readelf; do command -v "$tool" >/dev/null; done
rv_as=${RISCV_GNU_AS:-riscv64-linux-gnu-as}
loong_as=${LOONG_GNU_AS:-loongarch64-linux-gnu-as}

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# Four *instruction* line addresses: 0, 8, 12, 12+gap, followed by a
# four-byte instruction.  GAS records .loc at the following instruction,
# not at an intervening .space directive; include a real four-byte
# instruction for line 12 and reserve gap-4 bytes after it.  Thus the four
# consecutive spans (including end_sequence) are 8, 4, gap, 4.  A 50000-byte
# span fits fixed_advance_pc; 50004 forces GAS's ADDR_DELTA_LIMIT=50000
# set_address fallback, not a truncated 16-bit fixup.
make_source() {
  local profile=$1 mode=$2 gap=$3 out=$4
  if [[ $profile == rv* ]]; then
    {
      printf '.file 1 "line-relax.S"\n.text\n.option %s\n' "$mode"
      printf '%s\n' '.globl line_start' 'line_start:' '.loc 1 10 0' \
        '    call external_line_target' '.loc 1 11 0' \
        '    addi a0,a0,0' '.loc 1 12 0'
      printf '%s\n' '    addi a2,a2,0'
      printf '    .space %s\n' "$((gap - 4))"
      printf '%s\n' '.loc 1 13 0' '    addi a1,a1,0'
    } >"$out"
  else
    {
      printf '%s\n' '.file 1 "line-relax.S"' '.text'
      # The GNU 2.42 LoongArch assembler selects relaxation on its CLI;
      # .option norelax support arrived later. MiniAS supports that lexical
      # directive, so keep the GNU fixture otherwise identical.
      if [[ $mode == norelax && $out == *.mini.s ]]; then
        printf '%s\n' '.option norelax'
      fi
      printf '%s\n' '.globl line_start' 'line_start:' '.loc 1 10 0' \
        '    la.local $a0,line_end' '.loc 1 11 0' \
        '    addi.d $a1,$a1,0' '.loc 1 12 0'
      printf '%s\n' '    addi.d $a3,$a3,0'
      printf '    .space %s\n' "$((gap - 4))"
      printf '%s\n' '.loc 1 13 0' 'line_end:' '    addi.d $a2,$a2,0'
    } >"$out"
  fi
}

assemble_mini() {
  local arch=$1 mode=$2 source=$3 object=$4
  case $arch in
    rv32) "$MINIAS" --target=riscv -march=rv32i -mabi=ilp32 \
            -o "$object" "$source" ;;
    rv64) "$MINIAS" --target=riscv -march=rv64i -mabi=lp64 \
            -o "$object" "$source" ;;
    loong) "$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
            -o "$object" "$source" ;;
    *) echo "unknown MiniAS target $arch" >&2; return 1 ;;
  esac
}

assemble_gnu() {
  local arch=$1 mode=$2 source=$3 object=$4
  case $arch in
    rv32) "$rv_as" --gdwarf-4 -march=rv32i -mabi=ilp32 \
            -o "$object" "$source" ;;
    rv64) "$rv_as" --gdwarf-4 -march=rv64i -mabi=lp64 \
            -o "$object" "$source" ;;
    loong) "$loong_as" --gdwarf-4 -mabi=lp64d "-m$mode" \
            -o "$object" "$source" ;;
  esac
}

# pyelftools is the exact semantic oracle in CI: inspect the emitted DWARF
# program, each 16-bit field, and the relocations' resolved symbol+addend
# values. The no-pyelftools host fallback below still checks MiniAS using
# readelf, and explicitly says that its coverage is narrower.
inspect_python=''
if "$python" -c 'import elftools' >/dev/null 2>&1; then
  inspect_python=pyelftools
else
  inspect_python=readelf
fi

inspect_object() {
  local object=$1 arch=$2 mode=$3 span=$4
  if [[ $inspect_python == readelf ]]; then
    "$python" - "$object" "$arch" "$mode" "$span" <<'PY'
import re
import subprocess
import sys

path, arch, mode, span = sys.argv[1:]
raw = subprocess.check_output(
    ['readelf', '--debug-dump=rawline', path], text=True)
reloc = subprocess.check_output(['readelf', '-Wr', path], text=True)
fixed = len(re.findall(r'DW_LNS_fixed_advance_pc|Advance PC by fixed size amount', raw))
address = len(re.findall(r'DW_LNE_set_address|Extended opcode 2: set Address', raw))
assert fixed == (3 if span == 'wide' else 4), (path, 'fixed_advance_pc', fixed, raw)
assert address == (2 if span == 'wide' else 1), (path, 'set_address', address, raw)
assert re.search(r'DW_LNE_end_sequence|Extended opcode 1: End of Sequence', raw), (path, raw)
section = re.search(r"Relocation section '\.rela\.debug_line'.*?(?=Relocation section|\Z)",
                    reloc, re.S)
assert section, (path, '.rela.debug_line missing', reloc)
family = 'R_RISCV' if arch.startswith('rv') else 'R_LARCH'
add = re.findall(r'\b'+family+r'_ADD16\b', section.group())
sub = re.findall(r'\b'+family+r'_SUB16\b', section.group())
paired = not (arch == 'loong' and mode == 'norelax')
want = (3 if span == 'wide' else 4) if paired else 0
assert len(add) == len(sub) == want, (path, 'ADD16/SUB16', len(add), len(sub), want)
text = re.search(r"Relocation section '\.rela\.text'.*?(?=Relocation section|\Z)",
                 reloc, re.S)
markers = 0 if not text else len(re.findall(
    r'\bR_RISCV_RELAX\b' if family == 'R_RISCV' else r'\bR_LARCH_RELAX\b',
    text.group()))
assert (markers > 0) == (mode == 'relax'), (path, 'instruction relax markers', markers)
print(f'MINIAS_DWARF_LINE=PASS readelf profile={arch}/{mode}/{span} '
      f'fixed={fixed} addr={address} pairs={want} relax={markers}')
PY
    return
  fi

  "$python" - "$object" "$arch" "$mode" "$span" <<'PY'
import struct
import sys
from collections import defaultdict
from elftools.elf.elffile import ELFFile

path, arch, mode, span = sys.argv[1:]
kind = 'rv' if arch.startswith('rv') else 'loong'
add_kind, sub_kind, relax_kind = (34, 38, 51) if kind == 'rv' else (48, 53, 100)
expected = [8, 4, 4] if span == 'wide' else [8, 4, 50000 if span == 'limit' else 4, 4]
relocate = not (kind == 'loong' and mode == 'norelax')

def uleb(buf, at):
    value = 0
    shift = 0
    while True:
        assert at < len(buf), ('truncated DWARF LEB', path)
        b = buf[at]
        at += 1
        value |= (b & 127) << shift
        if not b & 128:
            return value, at
        shift += 7
        assert shift < 70, ('DWARF LEB overflow', path)

with open(path, 'rb') as source:
    elf = ELFFile(source)
    line = elf.get_section_by_name('.debug_line')
    assert line is not None, (path, 'no DWARF line section')
    data = line.data()
    assert len(data) >= 16 and struct.unpack_from('<H', data, 4)[0] == 4, \
        (path, 'expected DWARF v4', data[:16].hex())
    unit_len = struct.unpack_from('<I', data, 0)[0]
    assert unit_len + 4 == len(data), (path, 'line unit length')
    header_len = struct.unpack_from('<I', data, 6)[0]
    start = 10 + header_len
    assert 16 < start < len(data), (path, 'DWARF header length')
    opcode_base = data[15]
    standard_arity = data[16:16 + opcode_base - 1]
    assert opcode_base == 13 and len(standard_arity) == 12, \
        (path, 'GNU v4 line opcode contract', opcode_base)
    address_size = elf.elfclass // 8

    rela = elf.get_section_by_name('.rela.debug_line')
    assert rela is not None, (path, 'missing line relocation section')
    symtab = elf.get_section(rela['sh_link'])
    by_offset = defaultdict(list)
    for r in rela.iter_relocations():
        by_offset[r['r_offset']].append(r)
    text_rela = elf.get_section_by_name('.rela.text')
    marks = [] if text_rela is None else [r for r in text_rela.iter_relocations()
                                         if r['r_info_type'] == relax_kind]
    assert bool(marks) == (mode == 'relax'), \
        (path, 'source relaxation markers', len(marks), mode)

    def symbol_value(r):
        idx = r['r_info_sym']
        assert idx != 0, (path, 'null label difference symbol', r)
        symbol = symtab.get_symbol(idx)
        assert symbol['st_shndx'] == elf.get_section_index('.text'), \
            (path, 'line difference outside code', symbol.name, symbol['st_shndx'])
        return symbol['st_value'] + r['r_addend']

    fixed, addresses, other_address_ops, end_sequences = [], [], 0, 0
    at = start
    while at < len(data):
        op = data[at]
        at += 1
        if op == 0:
            length, at = uleb(data, at)
            assert length > 0 and at + length <= len(data), (path, 'extended opcode length')
            extension = data[at]
            if extension == 2:
                assert length == address_size + 1, (path, 'set_address width', length)
                field = at + 1
                relocations = by_offset.pop(field, [])
                assert len(relocations) == 1, (path, 'set_address relocation', field)
                r = relocations[0]
                expected_abs = 1 if address_size == 4 else 2
                assert r['r_info_type'] == expected_abs, \
                    (path, 'set_address absolute relocation', r['r_info_type'])
                addresses.append(symbol_value(r))
            elif extension == 1:
                end_sequences += 1
            at += length
        elif op == 9:
            assert at + 2 <= len(data), (path, 'short fixed PC field')
            initial = struct.unpack_from('<H', data, at)[0]
            relocations = by_offset.pop(at, [])
            if relocate:
                assert [r['r_info_type'] for r in relocations] == [add_kind, sub_kind], \
                    (path, 'missing ordered ADD16/SUB16', at, relocations)
                # R_*_ADD/SUB are in-place arithmetic adjustments. The
                # original field plus the two symbol values is the exact
                # post-link PC delta, regardless of section-symbol folding.
                final = (initial + symbol_value(relocations[0]) -
                         symbol_value(relocations[1])) & 0xffff
            else:
                assert not relocations, (path, 'unexpected relaxed norelax line field')
                final = initial
            fixed.append(final)
            at += 2
        elif op == 2:
            other_address_ops += 1
            _, at = uleb(data, at)
        elif op < opcode_base:
            assert op > 0, (path, 'unexpected zero opcode')
            for _ in range(standard_arity[op - 1]):
                _, at = uleb(data, at)
        else:
            pass  # A DWARF special opcode has no operand bytes.

    assert not by_offset, (path, 'unconsumed .debug_line relocs', dict(by_offset))
    assert other_address_ops == 0, (path, 'variable-width DW_LNS_advance_pc', other_address_ops)
    assert end_sequences == 1, (path, 'missing/duplicate end_sequence', end_sequences)
    assert fixed == expected, (path, 'line fixed increments', fixed, expected)
    expected_addresses = [0, 50016] if span == 'wide' else [0]
    assert addresses == expected_addresses, (path, 'set_address fallback', addresses, expected_addresses)
    assert fixed[-1] == 4, (path, 'missing relocatable end_sequence advance')
    print(f'MINIAS_DWARF_LINE=PASS pyelftools profile={arch}/{mode}/{span} '
          f'fixed={fixed} set_address={addresses} pairs={len(fixed) if relocate else 0} '
          f'relax_markers={len(marks)}')
PY
}

gnu_skips=0
gnu_cases=0
local_cases=0
for arch in rv32 rv64 loong; do
  gas=$rv_as
  if [[ $arch == loong ]]; then gas=$loong_as; fi
  oracle=1
  if ! command -v "$gas" >/dev/null 2>&1; then
    oracle=0
    if [[ ${REQUIRE_ALL_GNU:-0} == 1 ]]; then
      echo "MINIAS_DWARF_LINE=BLOCKED GNU assembler missing: $gas" >&2
      exit 1
    fi
    ((gnu_skips += 1))
    echo "MINIAS_DWARF_LINE=SKIP GNU=$gas target=$arch (MiniAS semantic checks continue)"
  fi
  for mode in relax norelax; do
    for span in short limit wide; do
      gap=4
      case $span in
        short) gap=4 ;;
        limit) gap=50000 ;;
        wide) gap=50004 ;;
      esac
      stem="$arch-$mode-$span"
      make_source "$arch" "$mode" "$gap" "$work/$stem.mini.s"
      assemble_mini "$arch" "$mode" "$work/$stem.mini.s" "$work/$stem.mini.o"
      inspect_object "$work/$stem.mini.o" "$arch" "$mode" "$span"
      ((local_cases += 1))
      if ((oracle)); then
        make_source "$arch" "$mode" "$gap" "$work/$stem.gnu.s"
        assemble_gnu "$arch" "$mode" "$work/$stem.gnu.s" "$work/$stem.gnu.o"
        inspect_object "$work/$stem.gnu.o" "$arch" "$mode" "$span"
        ((gnu_cases += 1))
      fi
    done
  done
done

echo "MINIAS_DWARF_LINE_LINKER_RELAX=PASS MiniAS=$local_cases GNU=$gnu_cases GNU_arch_skips=$gnu_skips inspector=$inspect_python"
