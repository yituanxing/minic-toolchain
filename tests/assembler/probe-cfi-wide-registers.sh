#!/usr/bin/env bash
set -Eeuo pipefail

: "${MINIAS:?Set MINIAS to the exact-head assembler binary}"
command -v as >/dev/null
command -v readelf >/dev/null

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# DW_CFA register operands are ULEB128, including x86 register numbers
# beyond 255.  The legacy CFI frontend incorrectly imposed the byte-sized
# DWARF v1 CIE return-register limit on *every* directive operand.
cat >"$work/wide.s" <<'ASM'
.text
.cfi_sections .debug_frame
.globl cfi_wide_registers
cfi_wide_registers:
.cfi_startproc
.cfi_def_cfa 301,16
.cfi_offset 302,-8
.cfi_val_offset 303,-16
.cfi_register 304,305
.cfi_def_cfa_register 306
.cfi_undefined 307
.cfi_same_value 308
.cfi_restore 309
nop
.cfi_endproc
ASM

"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/wide.s"
as --64 -o "$work/gnu.o" "$work/wide.s"
readelf --debug-dump=frames "$work/mini.o" >"$work/mini.txt"
readelf --debug-dump=frames "$work/gnu.o" >"$work/gnu.txt"
python3 - "$work/mini.txt" "$work/gnu.txt" <<'PY'
import re
import sys

def events(path):
    content = open(path, encoding='utf-8').read()
    if content.count(' FDE ') != 1:
        raise SystemExit(f'{path}: expected one real .debug_frame FDE')
    # GNU GAS may fold all CFI operations before the first PC advance into
    # its CIE, while MiniAS retains the equivalent operations in the FDE.
    # Compare the register's actual semantic event, not its placement in an
    # otherwise distinct CIE/FDE byte encoding.
    return [re.sub(r'^.*?DW_CFA_', 'DW_CFA_', line.strip())
            for line in content.splitlines()
            if 'DW_CFA_' in line and re.search(r'\br30[1-9]\b', line)]

mini, gnu = (events(path) for path in sys.argv[1:])
if mini != gnu:
    raise SystemExit(f'CFI wide-register GNU semantic events mismatch:\nmini={mini}\ngnu={gnu}')
if not all(str(reg) in '\n'.join(mini) for reg in range(301, 310)):
    raise SystemExit(f'missing ULEB128 register numbers: {mini}')
print('MINIAS_CFI_WIDE_REGISTERS=PASS GNU_frame_events=exact register_range=301..309')
PY

# Parser must still fail closed when the number exceeds its typed unsigned
# register storage; accepting and wrapping would silently target r0.
sed 's/\.cfi_def_cfa 301,16/.cfi_def_cfa 4294967296,16/' \
  "$work/wide.s" >"$work/overflow.s"
if "$MINIAS" --target=x86_64 -o "$work/overflow.o" "$work/overflow.s" \
    >"$work/overflow.stdout" 2>"$work/overflow.stderr"; then
    echo 'CFI register number outside unsigned storage was accepted' >&2
    exit 1
fi
grep -q 'unsupported-dwarf-cfi' "$work/overflow.stderr"
