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
grep -Eq 'R_AARCH64_ADR_PREL_PG_HI21.*sym' "$work/relocations"
grep -Eq 'R_AARCH64_ABS64.*sym' "$work/relocations"
if grep -Eq 'R_AARCH64_.*(x9|q20|v3)' "$work/relocations"; then
    echo 'AArch64 register alias leaked into an MC symbolic expression' >&2
    exit 1
fi
grep -Eq '[[:space:]]sym$' "$work/symbols"
echo 'MINIAS_A64_TYPED_ALIAS=PASS positional_registers=4 local_branch=1 symbolic_relocs=2'
