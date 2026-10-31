#!/usr/bin/env bash
# AAELF GOT-entry relocations use a zero addend: accepting local +1/-1
# produces a relocation that cannot preserve the intended GOT-entry identity.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
command -v readelf >/dev/null

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat >"$work/zero.s" <<'ASM'
.text
.globl got_zero
got_zero:
    adrp x0,:got:external+0
    ldr x0,[x0,:got_lo12:external-0]
    ret
ASM
"$MINIAS" --target=aarch64 -o "$work/zero.o" "$work/zero.s"
readelf -rW "$work/zero.o" >"$work/zero.reloc"
grep -q 'R_AARCH64_ADR_GOT_PAGE.*external + 0' "$work/zero.reloc"
grep -q 'R_AARCH64_LD64_GOT_LO12_NC.*external + 0' "$work/zero.reloc"

for operand in \
    'adrp x0,:got:external+1' \
    'adrp x0,:got:external-1' \
    'ldr x0,[x0,:got_lo12:external+1]' \
    'ldr x0,[x0,:got_lo12:external-1]'; do
    {
        printf '.text\n.globl invalid_got\ninvalid_got:\n'
        printf '%s\n' "$operand"
    } >"$work/invalid.s"
    if "$MINIAS" --target=aarch64 -o "$work/invalid.o" \
        "$work/invalid.s" >"$work/invalid.out" 2>"$work/invalid.err"; then
        echo "AArch64 accepted invalid GOT addend: $operand" >&2
        exit 1
    fi
    grep -q 'aarch64-got-relocation-nonzero-addend' "$work/invalid.err"
done
echo 'MINIAS_AARCH64_GOT_ADDEND=PASS zero_accepted nonzero_rejected=4'
