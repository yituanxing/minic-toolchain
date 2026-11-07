#!/usr/bin/env bash
# Original Linux ES/DS/SS and LKGS exception-table contracts, not executed
# in user mode. The third __ex_table word is a physical register-index ABI.
set -Eeuo pipefail
mini=${1:?MiniC compiler}
test -x "$mini"
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat > "$d/mini.c" <<'SRC'
#define EXT_REG_CORE \
" .pushsection \"__ex_table\",\"a\"\n" \
" .balign 4\n" \
" .long (1b) - .\n" \
" .long (1b) - .\n" \
".macro extable_type_reg type:req reg:req\n" \
".set .Lfound, 0\n" \
".set .Lregnr, 0\n" \
".irp rs,rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8,r9,r10,r11,r12,r13,r14,r15\n" \
".ifc \\reg, %%\\rs\n" \
".set .Lfound, .Lfound+1\n" \
".long \\type + (.Lregnr << 8)\n" \
".endif\n" \
".set .Lregnr, .Lregnr+1\n" \
".endr\n" \
".set .Lregnr, 0\n" \
".irp rs,eax,ecx,edx,ebx,esp,ebp,esi,edi,r8d,r9d,r10d,r11d,r12d,r13d,r14d,r15d\n" \
".ifc \\reg, %%\\rs\n" \
".set .Lfound, .Lfound+1\n" \
".long \\type + (.Lregnr << 8)\n" \
".endif\n" \
".set .Lregnr, .Lregnr+1\n" \
".endr\n" \
".if (.Lfound != 1)\n" \
".error \"extable_type_reg: bad register argument\"\n" \
".endif\n" \
".endm\n" \
"extable_type_reg reg=%k0, type=(17 | ((0) << 16)) \n" \
".purgem extable_type_reg\n" \
" .popsection\n"
#define SEGMENT(reg) "\t\t\t\t\t\t\n1:\tmovl %k0,%%" reg "\t\t\n" EXT_REG_CORE
#define LKGS "1:  .byte 0xf2,0x0f,0x00,0xf7 ; " EXT_REG_CORE
void mini_es(unsigned short selector) {
    asm volatile(SEGMENT("es") : "+r"(selector) :: "memory");
}
void mini_ds(unsigned short selector) {
    asm volatile(SEGMENT("ds") : "+r"(selector) :: "memory");
}
void mini_ss(unsigned short selector) {
    asm volatile(SEGMENT("ss") : "+r"(selector) :: "memory");
}
void mini_lkgs(unsigned short selector) {
    asm volatile(LKGS : "+D"(selector));
}
SRC
# LKGS Source text's named operand is only used inside the .macro body.
# The kernel's Core normalized `%k[sel]` form is equivalent to `%k0` here.
sed 's/mini_/gnu_/g' "$d/mini.c" > "$d/gnu.c"
gcc -E -P -x c "$d/mini.c" -o "$d/mini.i"
"$mini" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
gcc -c -x assembler "$d/mini.s" -o "$d/mini.o"
gcc -c -O0 "$d/gnu.c" -o "$d/gnu.o"
objdump -dr "$d/mini.o" > "$d/mini.dump"
objdump -s -j __ex_table "$d/mini.o" > "$d/mini.extable"
objdump -s -j __ex_table "$d/gnu.o" > "$d/gnu.extable"
for seg in es ds ss; do
    grep -q "mov.*%$seg" "$d/mini.dump"
done
grep -q 'lkgs' "$d/mini.dump"
# Three fixed R10D +r records (0xa11) and one fixed EDI LKGS record (0x711).
test "$(grep -o '110a0000' "$d/mini.extable" | wc -l)" -eq 3
grep -q '11070000' "$d/mini.extable"
test "$(grep -o '11070000' "$d/gnu.extable" | wc -l)" -eq 1

sed 's/+r"(selector)/+m"(selector)/g' "$d/mini.i" > "$d/bad-constraint.i"
sed 's/\.long (1b) - \./.quad (1b) - ./g' \
    "$d/mini.i" > "$d/bad-extable.i"
sed 's/0xf2,0x0f,0x00,0xf7/0xf3,0x0f,0x00,0xf7/' \
    "$d/mini.i" > "$d/bad-lkgs.i"
sed 's/+D"(selector)/+r"(selector)/' \
    "$d/mini.i" > "$d/bad-di.i"
for case in constraint extable lkgs di; do
    if "$mini" --target=x86_64-linux -S "$d/bad-$case.i" \
        -o "$d/bad-$case.s" >/dev/null 2>&1; then
        echo "X86_SEGMENT_EXTABLE_FAIL=accepted_bad_$case" >&2
        exit 1
    fi
    test ! -e "$d/bad-$case.s"
done
echo 'MINIC_X86_SEGMENT_EXTABLE=PASS GNU_AS=4 type17=4 negative=4'
