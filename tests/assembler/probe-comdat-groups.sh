#!/usr/bin/env bash
# Cross-target, real ELF COMDAT/SHT_GROUP contract. A group may contain
# multiple sections and the relocation sections belonging to them.
set -Eeuo pipefail
: "${MINIAS:?MiniAS required}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/comdat.s" <<'SRC'
.section .text.shared,"axG",@progbits,shared,comdat
.globl shared
.type shared,@function
shared:
    call external
    ret
.size shared,.-shared
.section .rodata.shared,"aG",@progbits,shared,comdat
.long shared
.text
.globl plain
plain:
    ret
SRC
"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/comdat.s"
as --64 -o "$work/gnu.o" "$work/comdat.s"
readelf --section-groups "$work/mini.o" >"$work/mini.group"
readelf --section-groups "$work/gnu.o" >"$work/gnu.group"
python3 - "$work/mini.group" "$work/gnu.group" <<'PY'
import re
import sys
def members(path):
    s = open(path,encoding='utf-8').read()
    if not re.search(r"COMDAT group section.*\[shared\]",s):
        raise SystemExit(f"missing COMDAT signature: {path}: {s}")
    a = set(re.findall(r"\[\s*\d+\]\s+(\.\S+)",s))
    return {x for x in a if x in
            {'.text.shared','.rodata.shared','.rela.text.shared',
             '.rela.rodata.shared'}}
a,b=map(members,sys.argv[1:])
expected={'.text.shared','.rodata.shared','.rela.text.shared',
          '.rela.rodata.shared'}
if a != expected or b != expected:
    raise SystemExit(f"GROUP membership mismatch: MiniAS {a}, GNU {b}")
print('MINIAS_COMDAT_GROUP=PASS signature=shared members=4')
PY
readelf -rW "$work/mini.o" | grep -q 'R_X86_64_PLT32'
readelf -rW "$work/mini.o" | grep -q 'R_X86_64_32'
cp "$work/mini.o" "$work/mini2.o"
ld -r "$work/mini.o" "$work/mini2.o" -o "$work/merged.o"
test "$(readelf -Ws "$work/merged.o" | grep -Ec ' FUNC .* shared$')" -eq 1
# Fail-closed: repeated section name cannot silently change its group.
cat >"$work/bad.s" <<'SRC'
.section .text.same,"axG",@progbits,first,comdat
nop
.section .text.same,"axG",@progbits,second,comdat
nop
SRC
if "$MINIAS" --target=x86_64 -o "$work/bad.o" "$work/bad.s" >/dev/null 2>&1; then
    echo 'MiniAS accepted inconsistent COMDAT membership' >&2
    exit 1
fi
test ! -e "$work/bad.o"
echo 'MINIAS_COMDAT_GROUP_LINK=PASS ld_multiple_objects=deduplicated'
