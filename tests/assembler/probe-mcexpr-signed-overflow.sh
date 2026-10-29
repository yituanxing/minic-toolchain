#!/usr/bin/env bash
# Verify that MCExpr's S+A-P arithmetic cannot overflow signed C integers.
# Preserve GNU's 64-bit modular DATA and signed-view RELA arithmetic without
# invoking undefined signed overflow; invalid cross-section division must
# still fail closed.
set -Eeuo pipefail
: "${MINIAS:?}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat >"$work/valid.s" <<'SRC'
.data
.space 16
local_start:
.space 32
# S+A overflows int64_t, yet S+A-P is representable.
.long local_start - . + 9223372036854775807
.quad (local_start+9223372036854775807-.)/4
.quad (forward+9223372036854775807-.)/4
.quad (forward+9223372036854775807-local_start)/4
.space 16
forward:
# A1-A2 cancels even though a different evaluation order might overflow.
local_for_reloc:
.quad external_value+9223372036854775807-(local_for_reloc+9223372036854775807)
# GNU RELA records signed -2 after modular MAX-(-MAX), not an overflow error.
local_for_wrap:
.quad external_value+9223372036854775807-(local_for_wrap+0x8000000000000001)
SRC

"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/valid.s"
as --64 -o "$work/gnu.o" "$work/valid.s"
objcopy -j .data -O binary "$work/mini.o" "$work/mini.bin"
objcopy -j .data -O binary "$work/gnu.o" "$work/gnu.bin"
cmp "$work/mini.bin" "$work/gnu.bin"
python3 - "$work/mini.o" "$work/gnu.o" <<'PY'
import re
import subprocess
import sys

def relocs(path):
    result=[]
    for line in subprocess.check_output(['readelf','-rW',path],text=True).splitlines():
        match=re.search(r'^\s*([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+(R_X86_64_\w+)\s+[0-9a-fA-F]+\s+(\S+)\s+([+-])\s+(\S+)',line)
        if match:
            result.append((int(match[1],16),match[2],match[3],match[4],match[5]))
    return result

mini,gnu=map(relocs,sys.argv[1:])
if mini != gnu or len(mini)!=2 or not any(x[3]=='-' and x[4]=='2' for x in mini):
    raise SystemExit(f'checked MCExpr relocation mismatch: {mini} != {gnu}')
PY

# Division between unrelated ELF sections has no absolute GNU DATA value.
# Do not manufacture a numeric section offset or leave a partial object.
cat >"$work/overflow.s" <<'SRC'
.section .left,"a"
left:
.quad (left-right)/4
.section .right,"a"
right:
SRC
if "$MINIAS" --target=x86_64 -o "$work/overflow.o" "$work/overflow.s" >"$work/output" 2>&1; then
    echo 'MCExpr accepted a cross-section divided symbol difference' >&2
    exit 1
fi
test ! -e "$work/overflow.o"
grep -q 'unsupported-divided-symbol-pair' "$work/output"
if as --64 -o "$work/gnu-overflow.o" "$work/overflow.s" >"$work/gnu-error" 2>&1; then
    echo 'GNU GAS unexpectedly accepted a cross-section divided difference' >&2
    exit 1
fi
echo 'MINIAS_MCEXPR_SIGNED_OVERFLOW=PASS GNU_bytes=exact GNU_reloc=exact modular_WRAP=exact fail_closed=1'
