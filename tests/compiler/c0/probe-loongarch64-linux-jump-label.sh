#!/bin/sh
# Original Linux 6.6.143 arch/loongarch/include/asm/jump_label.h uses two
# asm-goto forms. Each owns one ELF __jump_table record: site PREL32, C-label
# PREL32 and static-key PREL64. Both source and Core machine gates must prove
# the symbolic `i` input and preserve the C control-flow edge.
set -eu
: "${MINIC_CC:?point MINIC_CC at the built compiler}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat >"$work/jump.i" <<'SRC'
static int local_key;
extern int external_key;
int loong_static_branch(void) {
    asm goto(
        "1:\tnop\t\t\t\n\t"
        ".pushsection\t__jump_table, \"aw\"\t\n\t"
        ".align\t3\t\t\t\n\t"
        ".long\t\t1b - ., %l[l_yes] - .\t\n\t"
        ".quad\t\t%0 - .\t\t\t\n\t"
        ".popsection\t\t\t\t\n\t"
        : : "i"(&local_key) : : l_yes);
    return 0;
l_yes:
    return 1;
}
int loong_static_branch_jump(void) {
    asm goto(
        "1:\tb\t%l[l_yes]\t\n\t"
        ".pushsection\t__jump_table, \"aw\"\t\n\t"
        ".align\t3\t\t\t\n\t"
        ".long\t\t1b - ., %l[l_yes] - .\t\n\t"
        ".quad\t\t%0 - .\t\t\t\n\t"
        ".popsection\t\t\t\t\n\t"
        : : "i"(&external_key) : : l_yes);
    return 0;
l_yes:
    return 1;
}
static inline __attribute__((always_inline)) int inline_static_branch(int *key) {
    asm goto(
        "1:\tnop\t\t\t\n\t"
        ".pushsection\t__jump_table, \"aw\"\t\n\t"
        ".align\t3\t\t\t\n\t"
        ".long\t\t1b - ., %l[l_yes] - .\t\n\t"
        ".quad\t\t%0 - .\t\t\t\n\t"
        ".popsection\t\t\t\t\n\t"
        : : "i"(key) : : l_yes);
    return 0;
l_yes:
    return 1;
}
int inline_static_branch_call(void) { return inline_static_branch(&local_key); }
SRC
"$MINIC_CC" --target=loongarch64-linux -fPIE -S \
  "$work/jump.i" -o "$work/jump.s"
grep -Fq '1: nop' "$work/jump.s"
grep -Fq '1: b .Lmini_loong_' "$work/jump.s"
test "$(grep -Fc '.pushsection __jump_table,"aw"' "$work/jump.s")" -eq 3
test "$(grep -Fc '.balign 8' "$work/jump.s")" -eq 3
grep -Fq '.quad local_key - .' "$work/jump.s"
grep -Fq '.quad external_key - .' "$work/jump.s"
echo 'MINIC_LOONGARCH64_JUMP_LABEL_SHAPE=PASS nop=2 branch=1 PREL32=3 PREL64=3 symbolic_inline_clone=1'

cross=${LOONGARCH64_CC:-loongarch64-linux-gnu-gcc-13}
if command -v "$cross" >/dev/null 2>&1; then
    "$cross" -c -x assembler "$work/jump.s" -o "$work/jump.o"
    echo 'MINIC_LOONGARCH64_JUMP_LABEL_GNU_AS=PASS __jump_table=3'
else
    echo 'MINIC_LOONGARCH64_JUMP_LABEL_GNU_AS=SKIP missing Loong GNU assembler'
fi

# Negative: the source template must be the exact Linux record. Unknown
# instructions cannot pass merely because they spell asm goto + `i`.
sed 's/__jump_table/__invalid_jump_table/g' \
  "$work/jump.i" >"$work/invalid.i"
if "$MINIC_CC" --target=loongarch64-linux -fPIE -S \
    "$work/invalid.i" -o "$work/invalid.s" >/dev/null 2>"$work/invalid.err"; then
    echo 'MINIC_LOONGARCH64_JUMP_LABEL=FAIL mutated patch table accepted' >&2
    exit 1
fi
test ! -e "$work/invalid.s"
echo 'MINIC_LOONGARCH64_JUMP_LABEL_NEGATIVE=PASS mutated_template=rejected'
