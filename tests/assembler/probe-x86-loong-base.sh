#!/usr/bin/env bash
# Real GNU GAS -> same .s -> MiniAS. An error collector is never a success.
set -Eeuo pipefail
: "$MINIAS"
: "$LOONGAS"
: "$LOONGCOPY"
: "$LOONGREADELF"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat > "$work/x86.s" <<'ASM_X'
.text
.globl entry_x
.type entry_x,@function
entry_x:
    nop
    endbr64
    pushq %rbp
    movq %rsp,%rbp
    movl $42,%eax
    xorl %ecx,%ecx
    addq $16, %rsp
    subq $8, %rsp
    addl $4096, %r8d
    subl $-129, %r9d
    movl (%rdi), %eax
    movq %rax, -8(%rbp)
    movq -8(%rbp), %r9
    movl %r9d, 8(%r12)
    movl 8(%r12), %r10d
    movl %eax, (%rdi)
    movq (%rsp), %rax
    movq %rax, 128(%rsp)
    movl 0(%rbp), %eax
    addq %r8,%r9
    subq %rax,%rcx
    cmpq %rbp,%rsp
    testq %r8,%r9
    testl %ecx,%eax
    movq %rax, 32(%r14)
    cmpq %rax, 8(%r12)
    cmpq 8(%r12), %rax
    cmpl %r9d, -8(%rbp)
    testq %r8, 16(%rsp)
    testl 8(%r12), %eax
    movq %r8, 16(%r10,%r11,8)
    movl -128(%rbp,%rdx,2), %r8d
    movq 1024(%r12,%r13,4), %r9
    cmpq %r9, (%r14,%rbx,1)
    testq 8(%rsp,%r11,4), %r9
    xorl (%r12,%r8,2), %r10d
    movq (%r9,%r12,8), %rax
    addq %r9, 16(%r12)
    addl 8(%r12), %eax
    subq (%rdi), %r9
    xorl %r9d, 8(%r12)
    andq -16(%rbp), %rax
    orq %r8, 16(%rsp)
    popq %rbp
    ret
.size entry_x, .-entry_x
ASM_X
"$MINIAS" --target=x86_64 -march=x86-64 -mabi=sysv -o "$work/x86-mini.o" "$work/x86.s"
as --64 -o "$work/x86-gnu.o" "$work/x86.s"
objcopy -j .text -O binary "$work/x86-gnu.o" "$work/x86-gnu.bin"
objcopy -j .text -O binary "$work/x86-mini.o" "$work/x86-mini.bin"
cmp "$work/x86-mini.bin" "$work/x86-gnu.bin"
readelf -h "$work/x86-mini.o" | grep -q 'Advanced Micro Devices X86-64'

cat > "$work/la.s" <<'ASM_LA'
.text
.globl entry_la
.type entry_la, @function
entry_la:
    nop
    add.d $a0, $a0, $a1
    addi.d $sp, $sp, -16
    st.d $ra, $sp, 8
    ld.d $ra, $sp, 8
    stptr.d $ra, $sp, -32768
    ldptr.d $a0, $sp, 32764
    stptr.w $a0, $sp, -4
    ldptr.w $a1, $sp, 12
    st.b $a0, $sp, 7
    ld.bu $a1, $sp, 7
    st.h $a2, $sp, 14
    ld.hu $a3, $sp, 14
    ld.wu $a4, $sp, 8
    sub.d $a0, $a0, $a1
    and $a0, $a0, $a1
    or $a0, $a0, $a1
    xor $a0, $a0, $a1
    move $a0, $a1
    jr $r12
    ret
.size entry_la, .-entry_la
ASM_LA
"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d -o "$work/la-mini.o" "$work/la.s"
"$LOONGAS" -mabi=lp64d -o "$work/la-gnu.o" "$work/la.s"
"$LOONGCOPY" -j .text -O binary "$work/la-gnu.o" "$work/la-gnu.bin"
"$LOONGCOPY" -j .text -O binary "$work/la-mini.o" "$work/la-mini.bin"
cmp "$work/la-gnu.bin" "$work/la-mini.bin"
"$LOONGREADELF" -h "$work/la-mini.o" > "$work/la-header.txt"
grep -q 'LoongArch' "$work/la-header.txt"
grep -q '0x43' "$work/la-header.txt"

for bad in 'movq %rax, (%eax)' \
           'movl 4294967296(%rax), %eax' \
           'movq %rax, (%rax,%rsp,2)' \
           'movq %rax, (%rax,%eax,4)' \
           'movq %rax, (%rax,%rcx,3)' \
           'movq %rax, (%rax,%rcx,16)' \
           'movq %rax, (%rax,%rcx,2,7)'; do
    printf '.text\n%s\n' "$bad" >"$work/invalid-mem.s"
    if "$MINIAS" --target=x86_64 -o "$work/bad-mem.o" \
         "$work/invalid-mem.s" >/dev/null 2>"$work/invalid.err"; then
        echo "MiniAS accepted unsupported x86 memory form: $bad" >&2
        exit 1
    fi
done
for bad in 'ldptr.d $a0, $sp, 3' \
           'stptr.w $a0, $sp, 32768' \
           'ldptr.d $a1, $sp, -32772'; do
    printf '.text\n%s\n' "$bad" >"$work/invalid-la-ptr.s"
    if "$MINIAS" --target=loongarch64 -o "$work/bad-ptr.o" \
         "$work/invalid-la-ptr.s" >/dev/null 2>"$work/invalid.err"; then
        echo "MiniAS accepted invalid LoongArch pointer displacement: $bad" >&2
        exit 1
    fi
done
for target in x86_64 loongarch64; do
    if [ "$target" = x86_64 ]; then invalid='movq %rax, %eax'
    else invalid='addi.d $a0, $a0, 4096'; fi
    printf '.text\n%s\n' "$invalid" > "$work/invalid.s"
    if "$MINIAS" --target="$target" -o "$work/invalid.o" "$work/invalid.s" \
           >/dev/null 2>"$work/invalid.err"; then
        echo "invalid instruction accepted target=$target" >&2
        exit 1
    fi
    rm -f "$work/invalid.o"
done
printf '.text\nbl _unknown\n' > "$work/unimplemented.s"
if "$MINIAS" --target=loongarch64 -o "$work/invalid.o" "$work/unimplemented.s" \
        >/dev/null 2>"$work/invalid.err"; then
    echo 'Unimplemented branch/fixup silently accepted' >&2
    exit 1
fi
echo 'MINIAS_TWO_TARGET_ORACLE=PASS x86_GNU_bytes=equal loong_GNU_bytes=equal ELF_machine=correct lp64d_v1=0x43 CET_endbr64=1 ALU_imm8_imm32=1 x86_ModRM_SIB_disp=1 x86_test_cmp_alu_memory=1 x86_scaled_index_SIB=1 LA_jr=1 LA_scaled_ptr14=1 invalid=reject'
