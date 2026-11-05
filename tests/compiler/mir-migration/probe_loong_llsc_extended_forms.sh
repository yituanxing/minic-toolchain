#!/usr/bin/env bash
# Four Linux 6.6 LoongArch typed SOURCE_ASM LL/SC families, 12 sealed forms.
# Native GNU assembler/QEMU admission is a separate cross-toolchain gate.
set -Eeuo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
minic="${1:-${MINIC_CC:-$root/build/debug/bin/minic-cc}}"
test -x "$minic"
mkdir -p "$root/.mir-tmp"
work="$(mktemp -d "$root/.mir-tmp/loong-extended-XXXXXX")"
trap 'rm -rf -- "$work"' EXIT

fixture="$root/tests/compiler/mir-migration/loong_llsc_extended_forms.c"
cc -std=gnu11 -Wall -Wextra -Werror -fsyntax-only "$fixture"
cc -E -P -x c "$fixture" -o "$work/original.i"
"$minic" --target=loongarch64-linux -S "$work/original.i" -o "$work/typed.s"
test -s "$work/typed.s"

check() {
    if ! grep -Eq "$1" "$work/typed.s"; then
        echo "LOONG_EXTENDED_LLSC=FAIL missing machine semantic: $1" >&2
        exit 1
    fi
}

# SOURCE_ASM retained every Core-origin sealed occurrence. No selected
# instruction may replace the source Linux exception-table template.
test "$(grep -Fc '# MINIC_SOURCE_INLINE_ASM_BEGIN' "$work/typed.s")" -eq 12
test "$(grep -Fc '.pushsection __ex_table, "a"' "$work/typed.s")" -eq 12

# ID7: +r err=7 must be *read* from the lvalue before GNU ASM; the old
# backend erroneously set this register to zero and lost the input. The two
# exception fixups name that same physical $t6 through Linux GPR tables.
check '^[[:space:]]*ld[.]w[[:space:]]+\$t6,[[:space:]]*\$t2,[[:space:]]*0$'
check '^[[:space:]]*([0-9]+:[[:space:]]*)?ll[.]w[[:space:]]+\$t1,[[:space:]]*\$t4,[[:space:]]*0'
check '^[[:space:]]*bne[[:space:]]+\$t1,[[:space:]]*\$t2,[[:space:]]*3f'
check '^[[:space:]]*move[[:space:]]+\$t0,[[:space:]]*\$t8'
check '^[[:space:]]*([0-9]+:[[:space:]]*)?sc[.]w[[:space:]]+\$t0,[[:space:]]*\$t5,[[:space:]]*0'
grep -Fq '.L__gpr_num_$t6' "$work/typed.s"

# ID18: original LL/SC `beq`/retry + no-store barrier, 32/64-bit widths.
check '^[[:space:]]*([0-9]+:[[:space:]]*)?ll[.]w[[:space:]]+\$t0,[[:space:]]*\$t2,[[:space:]]*0'
check '^[[:space:]]*add[.]w[[:space:]]+\$t1,[[:space:]]*\$t0,[[:space:]]*\$t4'
check '^[[:space:]]*sc[.]w[[:space:]]+\$t1,[[:space:]]*\$t2,[[:space:]]*0'
check '^[[:space:]]*([0-9]+:[[:space:]]*)?ll[.]d[[:space:]]+\$t0,[[:space:]]*\$t2,[[:space:]]*0'
check '^[[:space:]]*add[.]d[[:space:]]+\$t1,[[:space:]]*\$t0,[[:space:]]*\$t4'
check '^[[:space:]]*sc[.]d[[:space:]]+\$t1,[[:space:]]*\$t2,[[:space:]]*0'

# ID19: five closed operation variants, matching zero-input physical tie,
# two distinct memory addresses, two fault entries per source instance.
check '^[[:space:]]*move[[:space:]]+\$t0,[[:space:]]*\$t2'
for op in 'add[.]w' 'or' 'and' 'xor'; do
    check "^[[:space:]]*$op[[:space:]]+\\\$t0,[[:space:]]*\\\$t1,[[:space:]]*\\\$t2"
done
test "$(grep -Fc '  st.w $t6, $t7, 0' "$work/typed.s")" -eq 6

# ID23: two widths and both signed ADDI immediate and register SUB paths.
check '^[[:space:]]*addi[.]w[[:space:]]+\$t1,[[:space:]]*\$t2,[[:space:]]*-1$'
check '^[[:space:]]*sub[.]w[[:space:]]+\$t1,[[:space:]]*\$t2,[[:space:]]*\$t5$'
check '^[[:space:]]*addi[.]d[[:space:]]+\$t1,[[:space:]]*\$t2,[[:space:]]*-1$'
check '^[[:space:]]*sub[.]d[[:space:]]+\$t1,[[:space:]]*\$t2,[[:space:]]*\$t5$'
check '^[[:space:]]*st[.]d[[:space:]]+\$t2,[[:space:]]*\$t7,[[:space:]]*0$'

# Mutating a protected GNU instruction/label invalidates its exact target
# descriptor; the compiler must reject without leaving a partial .s.
while IFS='|' read -r tag original mutation; do
    test -n "$tag" || continue
    sed "s/$original/$mutation/" "$work/original.i" >"$work/$tag.i"
    if cmp -s "$work/original.i" "$work/$tag.i"; then
        echo "LOONG_EXTENDED_LLSC=FAIL fixture mutation missed $tag" >&2
        exit 1
    fi
    if "$minic" --target=loongarch64-linux -S "$work/$tag.i" \
        -o "$work/$tag.s" >"$work/$tag.log" 2>&1; then
        echo "LOONG_EXTENDED_LLSC=FAIL accepted unsafe $tag" >&2
        exit 1
    fi
    test ! -e "$work/$tag.s"
done <<'CASES'
futex-cmpxchg|bne %1, %z4, 3f|bne %1, %z4, 4f
add-unless|sc.w %\[rc\], %\[c\]|sc.w %[rc], %[c]; nop
futex-rmw|add.w \$t0, %1, %z5|sub.w $t0, %1, %z5
sub-positive|addi.d %0, %1, %3|addi.d %0, %1, %2
CASES

echo 'LOONG_EXTENDED_LLSC=PASS forms=12 families=4 extable_entries=12 signed_imm=2 readwrite_ingress=1 negatives=4 GNU_QEMU=NOT_RUN'
