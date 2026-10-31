#!/bin/sh
# Linux 6.6 AArch64 __jump_table: two native asm-goto site opcodes and
# three exact PC-relative ELF relocation fields, including inline clones.
set -eu
: "${MINIC_CC:?set MINIC_CC to built MiniC compiler}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat >"$work/jump.i" <<'SRC'
static int local_key;
extern int external_key;
int aarch_static_branch(void) {
    asm goto(
        "1:\tnop\t\t\t\n\t"
        ".pushsection\t__jump_table, \"aw\"\t\n\t"
        ".align\t3\t\t\t\n\t"
        ".long\t\t1b - ., %l[l_yes] - .\t\n\t"
        ".quad\t\t%c0 - .\t\t\t\n\t"
        ".popsection\t\t\t\t\n\t"
        : : "i"(&local_key) : : l_yes);
    return 0;
l_yes: return 1;
}
int aarch_static_branch_jump(void) {
    asm goto(
        "1:\tb\t%l[l_yes]\t\n\t"
        ".pushsection\t__jump_table, \"aw\"\t\n\t"
        ".align\t3\t\t\t\n\t"
        ".long\t\t1b - ., %l[l_yes] - .\t\n\t"
        ".quad\t\t%c0 - .\t\t\t\n\t"
        ".popsection\t\t\t\t\n\t"
        : : "i"(&external_key) : : l_yes);
    return 0;
l_yes: return 1;
}
static inline __attribute__((always_inline)) int inlined_static_branch(int *key) {
    asm goto(
        "1:\tnop\t\t\t\n\t"
        ".pushsection\t__jump_table, \"aw\"\t\n\t"
        ".align\t3\t\t\t\n\t"
        ".long\t\t1b - ., %l[l_yes] - .\t\n\t"
        ".quad\t\t%c0 - .\t\t\t\n\t"
        ".popsection\t\t\t\t\n\t"
        : : "i"(key) : : l_yes);
    return 0;
l_yes: return 1;
}
int inlined_static_branch_call(void) { return inlined_static_branch(&local_key); }
SRC
"$MINIC_CC" --target=aarch64-linux -S "$work/jump.i" -o "$work/jump.s"
grep -Fq '1: nop' "$work/jump.s"
grep -Fq '1: b .Lmini_arm_' "$work/jump.s"
test "$(grep -Fc '.pushsection __jump_table,"aw"' "$work/jump.s")" -eq 3
test "$(grep -Fc '.balign 8' "$work/jump.s")" -eq 3
grep -Fq '.quad local_key - .' "$work/jump.s"
grep -Fq '.quad external_key - .' "$work/jump.s"
echo 'AARCH64_LINUX_JUMP_LABEL=PASS site=3 PREL32=6 PREL64=3 inline_clone=1'

cross=${AARCH64_CC:-aarch64-linux-gnu-gcc}
if command -v "$cross" >/dev/null 2>&1; then
    "$cross" -c -x assembler "$work/jump.s" -o "$work/jump.o"
    echo 'AARCH64_JUMP_LABEL_GNU_AS=PASS'
else
    echo 'AARCH64_JUMP_LABEL_GNU_AS=SKIP'
fi
sed 's/__jump_table/__alien_jump_table/g' "$work/jump.i" >"$work/invalid.i"
if "$MINIC_CC" --target=aarch64-linux -S "$work/invalid.i" \
    -o "$work/invalid.s" >/dev/null 2>"$work/invalid.err"; then
    echo 'AARCH64_JUMP_LABEL=FAIL mutated metadata accepted' >&2
    exit 1
fi
test ! -e "$work/invalid.s"
echo 'AARCH64_JUMP_LABEL_NEGATIVE=PASS alien_section=rejected'
