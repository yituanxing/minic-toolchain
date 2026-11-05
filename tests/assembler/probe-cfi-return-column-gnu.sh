#!/usr/bin/env bash
set -Eeuo pipefail

: "${MINIAS:?Set MINIAS to the exact-head assembler binary}"
command -v as >/dev/null
command -v readelf >/dev/null

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# GNU .cfi_return_column changes the procedure's CIE return column, not the
# target psABI's initial rule that saves the actual architectural return PC.
# The second function changes its column *after* advancing the PC and later
# overrides it again; both events still affect the CIE, not its FDE program.
# Functions one/four and two/three should share their respective CIEs.
for mode in .eh_frame .debug_frame; do
  cat >"$work/cfi.s" <<ASM
.cfi_sections $mode
.text
.globl cfi_default
cfi_default:
.cfi_startproc
nop
.cfi_endproc
.globl cfi_changed_late
cfi_changed_late:
.cfi_startproc
nop
.cfi_return_column 4
nop
.cfi_return_column 0
.cfi_endproc
.globl cfi_changed_early
cfi_changed_early:
.cfi_startproc
.cfi_return_column 0
nop
.cfi_endproc
.globl cfi_restored_default
cfi_restored_default:
.cfi_startproc
.cfi_return_column 0
.cfi_return_column 16
nop
.cfi_endproc
ASM
  as --64 -o "$work/gnu.o" "$work/cfi.s"
  "$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/cfi.s"
  readelf --debug-dump=frames "$work/gnu.o" >"$work/gnu.frames"
  readelf --debug-dump=frames "$work/mini.o" >"$work/mini.frames"

  python3 - "$mode" "$work/gnu.frames" "$work/mini.frames" <<'PY'
import pathlib
import re
import sys

mode, gnu_path, mini_path = sys.argv[1:]

def parse(path):
    cies, fdes = [], []
    current = None
    for line in pathlib.Path(path).read_text().splitlines():
        cie = re.search(r'^\s*([0-9a-f]{8})\s+[0-9a-f]+\s+[0-9a-f]+\s+CIE\b', line)
        fde = re.search(r'^\s*([0-9a-f]{8})\s+[0-9a-f]+\s+[0-9a-f]+\s+FDE\s+cie=([0-9a-f]{8})\b', line)
        if cie:
            current = dict(addr=cie.group(1), return_column=None, rules=[])
            cies.append(current)
        elif fde:
            current = dict(cie=fde.group(2), rules=[])
            fdes.append(current)
        elif current is not None:
            column = re.search(r'Return address column:\s*(\d+)', line)
            if column:
                current['return_column'] = int(column.group(1))
            if 'DW_CFA_' in line:
                rule = line.split('DW_CFA_', 1)[1].strip()
                # GAS may emit empty DW_CFA_advance_loc at a mid-function
                # .cfi_return_column.  It only changes the CIE (globally
                # for this FDE); the location-only FDE op has no unwind
                # state effect.  Compare actual register/CFA rules exactly.
                if rule != 'nop' and not rule.startswith('advance_loc'):
                    current['rules'].append(rule)
    addresses = {entry['addr']: i for i, entry in enumerate(cies)}
    assert len(cies) == 2 and len(fdes) == 4, (path, cies, fdes)
    assert all(fde['cie'] in addresses for fde in fdes), (path, fdes)
    view = {'columns': [cie['return_column'] for cie in cies],
            'cie_rules': [cie['rules'] for cie in cies],
            'fde_cies': [addresses[fde['cie']] for fde in fdes],
            'fde_rules': [fde['rules'] for fde in fdes]}
    return view

gnu, mini = parse(gnu_path), parse(mini_path)
assert gnu['columns'] == [16, 0], (mode, gnu)
assert gnu['fde_cies'] == [0, 1, 1, 0], (mode, gnu)
assert all(any('offset: r16' in rule and 'cfa-8' in rule
               for rule in rules) for rules in gnu['cie_rules']), (mode, gnu)
if gnu != mini:
    raise SystemExit(f'{mode} GNU CIE/FDE mismatch:\nGNU={gnu}\nMiniAS={mini}')
print(f'MINIAS_CFI_RETURN_COLUMN=PASS section={mode} CIE=2 FDE=4 GNU_unwind_rules=exact')
PY
done

# LoongArch64 is the real Linux 6.6.143 sigreturn.o trigger: GNU's compiler
# emits `.cfi_return_column 0` for the signal trampoline, whereas ordinary
# LoongArch procedures retain the psABI return column 1.  When the Loong GNU
# cross assembler is unavailable, verify these emitted CIE values directly
# with host readelf (without claiming a Loong GNU-object oracle).
for mode in .eh_frame .debug_frame; do
  cat >"$work/loong.s" <<ASM
.cfi_sections $mode
.text
.globl loong_normal
loong_normal:
.cfi_startproc
nop
.cfi_endproc
.globl loong_sigreturn
loong_sigreturn:
.cfi_startproc
.cfi_return_column 0
nop
.cfi_endproc
.globl loong_after
loong_after:
.cfi_startproc
nop
.cfi_endproc
ASM
  "$MINIAS" --target=loongarch64 -o "$work/loong.o" "$work/loong.s"
  readelf --debug-dump=frames "$work/loong.o" >"$work/loong.frames"
  python3 - "$mode" "$work/loong.frames" <<'PY'
import pathlib
import re
import sys

mode, path = sys.argv[1:]
cies, fdes, current = [], [], None
for line in pathlib.Path(path).read_text().splitlines():
    cie = re.search(r'^\s*([0-9a-f]{8})\s+[0-9a-f]+\s+[0-9a-f]+\s+CIE\b', line)
    fde = re.search(r'^\s*([0-9a-f]{8})\s+[0-9a-f]+\s+[0-9a-f]+\s+FDE\s+cie=([0-9a-f]{8})\b', line)
    if cie:
        current = {'addr': cie.group(1), 'column': None}
        cies.append(current)
    elif fde:
        fdes.append(fde.group(2))
        current = None
    elif current is not None:
        ret = re.search(r'Return address column:\s*(\d+)', line)
        if ret:
            current['column'] = int(ret.group(1))
assert [c['column'] for c in cies] == [1, 0], (mode, cies)
addresses = {cie['addr']: i for i, cie in enumerate(cies)}
assert [addresses[ref] for ref in fdes] == [0, 1, 0], (mode, cies, fdes)
print(f'MINIAS_CFI_RETURN_COLUMN_LOONG=PASS section={mode} columns=1,0 FDE=3')
PY
done

# CIE v1 stores the return-address column as exactly one byte. GNU GAS
# rejects 256, but its 128..255 encodings must also remain byte-exact.
for column in 127 128 200 255; do
  cat >"$work/v1.s" <<ASM
.cfi_sections .debug_frame
.text
v1_column:
.cfi_startproc
.cfi_return_column $column
nop
.cfi_endproc
ASM
  as --64 -o "$work/v1.gnu.o" "$work/v1.s"
  "$MINIAS" --target=x86_64 -o "$work/v1.mini.o" "$work/v1.s"
  objcopy --dump-section ".debug_frame=$work/v1.gnu.bytes" "$work/v1.gnu.o"
  objcopy --dump-section ".debug_frame=$work/v1.mini.bytes" "$work/v1.mini.o"
  cmp "$work/v1.gnu.bytes" "$work/v1.mini.bytes"
done
echo 'MINIAS_CFI_CIE_V1_WIDTH=PASS x86_columns=127,128,200,255 GNU_debug_frame_bytes=exact'

# RISC-V uses DWARF CIE version 3 in BOTH frame sections. For v3 the column
# is a ULEB128 (not a v1 byte); a byte-sized 200 previously made readelf
# misparse the EH augmentation and silently lose the CIE/FDE. Test both
# serializers together, including the ULEB transition and values >255.
for column in 127 128 200 255 256 300; do
  cat >"$work/v3.s" <<ASM
.cfi_sections .eh_frame,.debug_frame
.text
v3_column:
.cfi_startproc
.cfi_return_column $column
addi x0,x0,0
.cfi_endproc
ASM
  "$MINIAS" --target=riscv -march=rv64i -mabi=lp64 \
    -o "$work/v3.mini.o" "$work/v3.s"
  readelf --debug-dump=frames "$work/v3.mini.o" \
    >"$work/v3.mini.frames" 2>"$work/v3.mini.errors"
  test ! -s "$work/v3.mini.errors"
  if command -v riscv64-linux-gnu-as >/dev/null 2>&1; then
    riscv64-linux-gnu-as -march=rv64i -mabi=lp64 \
      -o "$work/v3.gnu.o" "$work/v3.s"
    readelf --debug-dump=frames "$work/v3.gnu.o" \
      >"$work/v3.gnu.frames" 2>"$work/v3.gnu.errors"
    test ! -s "$work/v3.gnu.errors"
    gnu_frames="$work/v3.gnu.frames"
  else
    gnu_frames=''
  fi
  python3 - "$column" "$work/v3.mini.frames" "$gnu_frames" <<'PY'
import pathlib
import re
import sys

column = int(sys.argv[1])

def read_frames(path):
    if not path:
        return None
    result = {}
    current = None
    for line in pathlib.Path(path).read_text().splitlines():
        section = re.match(r'Contents of the (\.\w+) section:', line)
        if section:
            current = section.group(1)
            result[current] = {'cie': 0, 'fde': 0, 'version': [], 'column': []}
            continue
        if current is None:
            continue
        if re.search(r'\bCIE\b', line):
            result[current]['cie'] += 1
        if re.search(r'\bFDE\b', line):
            result[current]['fde'] += 1
        match = re.search(r'Version:\s*(\d+)', line)
        if match:
            result[current]['version'].append(int(match.group(1)))
        match = re.search(r'Return address column:\s*(\d+)', line)
        if match:
            result[current]['column'].append(int(match.group(1)))
    return result

mini = read_frames(sys.argv[2])
for section in ('.eh_frame', '.debug_frame'):
    expected = {'cie': 1, 'fde': 1, 'version': [3], 'column': [column]}
    assert mini.get(section) == expected, (section, column, mini)
gnu = read_frames(sys.argv[3])
if gnu is not None:
    assert gnu == mini, (column, gnu, mini)
PY
done

# A v3 column can exceed 255 but still must fit our unsigned register ID.
cat >"$work/v3-overflow.s" <<'ASM'
.text
bad:
.cfi_startproc
.cfi_return_column 4294967296
addi x0,x0,0
.cfi_endproc
ASM
if "$MINIAS" --target=riscv -march=rv64i -mabi=lp64 \
  -o "$work/v3-overflow.o" "$work/v3-overflow.s" >/dev/null 2>&1; then
  echo 'MiniAS accepted overflowing DWARF v3 return column' >&2
  exit 1
fi
test ! -e "$work/v3-overflow.o"
echo 'MINIAS_CFI_CIE_V3_WIDTH=PASS rv64_columns=127,128,200,255,256,300 eh_and_debug_FDE=decoded'

# Reject malformed / non-v1-representable columns rather than wrapping or
# accidentally emitting an invalid CIE.  GAS rejects each of these too.
for operand in '' -1 foo 256 300 '1,2'; do
  cat >"$work/reject.s" <<ASM
.text
bad:
.cfi_startproc
.cfi_return_column $operand
nop
.cfi_endproc
ASM
  if as --64 -o "$work/gnu.o" "$work/reject.s" >/dev/null 2>&1; then
    echo "GNU unexpectedly accepted return column '$operand'" >&2
    exit 1
  fi
  if "$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/reject.s" >/dev/null 2>&1; then
    echo "MiniAS unexpectedly accepted return column '$operand'" >&2
    exit 1
  fi
done
printf '.cfi_return_column 0\n' >"$work/reject.s"
if "$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/reject.s" >/dev/null 2>&1; then
  echo 'MiniAS accepted return column outside .cfi_startproc' >&2
  exit 1
fi
echo 'MINIAS_CFI_RETURN_COLUMN_REJECT=PASS invalid=7'
