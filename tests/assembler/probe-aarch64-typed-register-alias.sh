#!/usr/bin/env bash
# AArch64 .req is a register-position alias, not a replacement across every
# identifier in an instruction's source text. The same spelling may be a
# local branch target or a symbol in an ADRP / literal-pool expression.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat >"$work/alias.s" <<'SRC'
.text
sym .req x9
gpr_start:
    add sym, sym, #1
    ldr x0, [sym]
    br sym
    b sym
    adrp x0, sym
    ldr x2, =sym
sym:
    ret
.unreq sym
.ltorg

vec .req v3
qpair .req q20
wide .req d7
base .req x3
    mov vec.16b, v4.16b
    stp qpair, q21, [x2]
    ldr wide, [x2]
    ld1 {vec.16b}, [base]
.unreq vec
.unreq qpair
.unreq wide
.unreq base
SRC

"$MINIAS" --target=aarch64 -o "$work/alias.o" "$work/alias.s"
readelf -Wr "$work/alias.o" >"$work/relocations"
readelf -Ws "$work/alias.o" >"$work/symbols"

test "$(awk '/R_AARCH64_/ {count++} END {print count + 0}' \
    "$work/relocations")" -eq 2
# GNU GAS canonicalizes relocations to an ordinary defined local NOTYPE
# label as STT_SECTION + label's offset.  The label must still be parsed as
# a symbol, never as the currently bound x9 register alias. Check the full
# ELF relocation *target address*, rather than requiring the obsolete
# textual local name as the relocation's r_info symbol.
python3 - "$work/relocations" "$work/symbols" <<'PY'
from pathlib import Path
import re
import sys

relocation_lines = Path(sys.argv[1]).read_text().splitlines()
symbol_lines = Path(sys.argv[2]).read_text().splitlines()
symbols = [line.split() for line in symbol_lines
           if re.match(r'^\s*\d+:', line)]
local = [parts for parts in symbols
         if len(parts) >= 8 and parts[7] == 'sym' and
         parts[3:5] == ['NOTYPE', 'LOCAL'] and parts[6].isdigit()]
if len(local) != 1:
    raise SystemExit(f'missing real local symbol named sym: {local}')
symbol_value = int(local[0][1], 16)

expected = {'R_AARCH64_ADR_PREL_PG_HI21': 0x10,
            'R_AARCH64_ABS64': 0x20}
for relocation, offset in expected.items():
    matches = [line.split() for line in relocation_lines
               if relocation in line.split()]
    if len(matches) != 1:
        raise SystemExit(f'expected one {relocation}, saw {matches}')
    fields = matches[0]
    if (len(fields) < 7 or int(fields[0], 16) != offset or
            fields[4:6] != ['.text', '+'] or
            int(fields[6], 16) != symbol_value):
        raise SystemExit(f'{relocation} must target .text + sym offset '
                         f'{symbol_value:#x}: {fields}')
PY
if grep -Eq 'R_AARCH64_.*(x9|q20|v3)' "$work/relocations"; then
    echo 'AArch64 register alias leaked into an MC symbolic expression' >&2
    exit 1
fi
grep -Eq '[[:space:]]sym$' "$work/symbols"
echo 'MINIAS_A64_TYPED_ALIAS=PASS positional_registers=4 local_branch=1 symbolic_relocs=2'
