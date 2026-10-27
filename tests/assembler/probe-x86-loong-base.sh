#!/usr/bin/env bash
# Real GNU GAS -> same .s -> MiniAS. An error collector is never a success.
set -Eeuo pipefail
# Show exact failing command in strict GNU object oracle.
trap 'echo "MINIAS_GNU_ORACLE_FAIL line=$LINENO command=$BASH_COMMAND" >&2' ERR
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
    /* GCC inline asm condition code comments are no instructions. */
    /* A real multi-line GAS comment:
       next line remains part of the comment. */
    endbr64 /* register-state commentary */
    cs
    nop
    rep
    nop
    repz
    nop
    repne
    nop
    data16
    nop
    rex.W
    nop
    call *%rax
    callq *%r11
    jmp *%r10
    jmpq *%r13
    # Group-5 indirect memory transfers must not masquerade as PLT32.
    call *%gs:40
    call *ext_global(%rip)
    # GNU AT&T absolute symbols use SIB base=101 and R_X86_64_32S,
    # not RIP-relative PC32 and not a signed32 immediate.
    call *handler_ptr
    jmp *exit_ptr
    jmp *8(%r12,%r13,4)
    jmp *%fs:4(%rbx)
    push %r12
    pop %r12
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
    movl %gs:pcpu_hot+12(%rip), %eax
    movq %gs:this_cpu_off(%rip), %rbx
    movq %fs:my_tls_qword(%rip), %r10
    movl %gs:pcpu_hot+8(%rip), %r9d
    # Segment is only a prefix: preserve base/index/scale/disp ModR/M.
    movq %gs:8(%rdi), %rax
    movq abs64_slot, %rax
    movl %eax, abs32_slot
    cmpb $1, flag_abs
    movq 1234, %rax
    movl %fs:16(%r12,%r13,4), %r9d
    movb %al, %gs:1(%rbp)
    cmpb $1, %gs:16(%rax)
    movw %fs:24(,%r8,4), %r10w
    movl %eax, (%rdi)
    movq (%rsp), %rax
    movq %rax, 128(%rsp)
    movl 0(%rbp), %eax
    addq %r8,%r9
    # Carry-dependent 8/16-bit operations share typed ModR/M.
    sbbb %sil,%dil
    sbbb %cl,8(%r12)
    sbbb 8(%r12),%r9b
    sbbw %cx,4(%r13)
    sbbb $7,%r8b
    sbbb $1,8(%r12)
    subq %rax,%rcx
    cmpq %rbp,%rsp
    testq %r8,%r9
    testl %ecx,%eax
    movq %rax, 32(%r14)
    cmpq %rax, 8(%r12)
    cmpq 8(%r12), %rax
    cmpl %r9d, -8(%rbp)
    btq %rax, __cpu_online_mask(%rip)
    btsq %r9, __cpu_online_mask(%rip)
    btrl %r10d, 8(%rsp)
    btcq %r11, 16(%r12)
    xaddl %eax, (%rdi)
    xaddq %r13, 8(%rsp)
    # All x86 width variants use the existing 0x86/0x87 typed memory encoder.
    xchgl %eax, 24(%rbx)
    xchgl %eax, ext_global(%rip)
    xchgl 32(%r12), %r10d
    xchgq %r13, 8(%rsp)
    xchgw %dx, 6(%rbp)
    xchgb %al, 4(%rdi)
    inb %dx, %al
    inw %dx, %ax
    inl %dx, %eax
    outb %al, %dx
    outw %ax, %dx
    outl %eax, %dx
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
    notl %eax
    notq %r9
    negl %r10d
    negq %r11
    incl %eax
    incq %r8
    decl %r9d
    decq %r10
    pushf
    popf
    cmpb $0, __already_done.0(%rip)
    testb $1, (%rax)
    addb $2, 8(%rsp)
    movq 0(,%rdx,8), %rax
    leaq 0(,%rdx,8), %rax
    cmove %edx, %eax
    cmovne %r9d, %r10d
    cmovgq %r8, %r9
    js .Lshort_x86
    jns .Lshort_x86
    shlq %cl, %rax
    sarq %cl, %r9
    # Shared width descriptor: shifts and rotates 8/16/32/64.
    shrb $4, %al
    shrb %cl, %sil
    shrw %ax
    shlw %r9w
    rolw $8, %ax
    rolw $8, %dx
    rorw %cl, %r12w
    rolb %dl
    shrl $1, %r12d
    testb $1, %ah
    testb $2, %ch
    testb $255, %dh
    cli
    sti
    cld
    std
    stac
    clac
    cpuid
    rdtsc
    rdtscp
    rdmsr
    wrmsr
    swapgs
    wbinvd
    invd
    mull %eax
    imull %edx, %eax
    # Immediate signed IMUL: 6B imm8 vs 69 imm32, 16/32/64-bit forms.
    imulq $1000, %rax, %rax
    imulq $-9, %r8, %r9
    imulq $274877907, %rdx, %rdx
    imull $255, %ecx, %edx
    imull $7, %r10d
    imulw $7, %cx, %dx
    imulw $1000, %r9w, %r10w
    movzbl %ah, %eax
    movzbl %ch, %ecx
    movzbl %dh, %edx
    movzbl %bh, %ebx
    imulq %r8, %r9
    imulw %cx, %dx
    imulq 8(%r12), %r9
    imull (%rax), %r10d
    imulw 4(%r13), %r8w
    mulq %r14
    imulq %rax
    divl %edx
    divq %rcx
    idivq %r11
    idivl %ebx
    notq (%rax)
    mulq 8(%r12)
    divq ext_global(%rip)
    # Unsuffixed operations infer width from the register (GNU AT&T).
    add %gs:this_cpu_off(%rip), %rbx
    add %gs:this_cpu_off(%rip), %r13
    sub (%r12), %eax
    or %r8w, (%r9)
    cmp %rax, ext_global(%rip)
    bt %rax, %rdx
    cmpxchg %ecx, (%rdi)
    # 0F 40+cc/BC/BD share the existing typed memory+REX/PC32 path.
    cmove overflowuid(%rip), %edx
    cmovgq ext_global(%rip), %r8
    cmovl 8(%rsp,%r12,4), %r9d
    cmovew -8(%rbp), %r11w
    bsf ext_global(%rip), %rax
    bsfl 8(%rdi,%r8,2), %eax
    bsrq 8(%r12), %r13
    bsfw (%rsp), %dx
    # TEST uses F7 /0 or A9 accumulator, not Group-1 immediate.
    testl $65536, %eax
    testq $-1, %r8
    testq $65536, %rax
    testw $65535, %ax
    testw $1, %r9w
    btq %rax, %rdx
    btsq %r8, %r9
    btrl %ecx, %edx
    btcq %r10, %r11
    xaddl %ecx, %edx
    xaddq %r12, %r13
    cmpxchgl %ecx, (%rdi)
    cmpxchgq %r9, 8(%r10)
    bsf %rax,%rax
    bsfq %r8, %r9
    bsrl %ecx, %edx
    bsr %r11, %r12
    bswap %edx
    bswapl %r10d
    bswapq %r13
    # Real GNU x86-64 per-CPU indexed absolute disp32 uses 32S, not PC32.
    movq __per_cpu_offset(,%rax,8),%rax
    addq __per_cpu_offset(,%rdx,8),%rax
    pushq -8(%r10)
    popq -8(%r10)
    orb $1,%dh
    andb $2,%dh
    adcl $0,%eax
    adcq %r9,%r10
    adcq (%r12),%r11
    cltd
    sfence
    mfence
    lfence
    .value 0x1234
    movzbl %al, %eax
    movzbl %r8b, %r10d
    movzbl 8(%rsp,%r10,2), %r9d
    movzbl ext_global(%rip), %r8d
    movzbq %r12b, %r13
    movzwl %ax, %eax
    movzwl (%rsp,%r12,4), %r8d
    movzwq %r14w, %r15
    movzwq 8(%r12), %r11
    movslq 16(%r13,%r14,4), %r9
    movslq ext_global(%rip), %rax
    movb %al, 4(%rsi)
    movb 4(%rsi), %cl
    movb %r9b, 16(%r12,%r13,4)
    cmpb %spl, (%rax)
    testb %sil, (%rdi)
    addb (%r13), %r8b
    andb %r10b, (%r14)
    movw %cx, -8(%rbp)
    movw (%rax), %r10w
    cmpw %r9w, 2(%r10)
    testw (%r12), %r11w
    subw %si, (%r15)
    testw %ax, %ax
    testw %r9w, %r10w
    movw %r10w, %r11w
    andw %cx, %dx
    cmpw %di, %si
    cmpb $1, %al
    cmpb $2, %dl
    cmpb $255, %r9b
    xorb $-1, %sil
    andw $-4096, %ax
    andw $-1, %r9w
    cmpw $255, %ax
    cmpw $1, %ax
    addw $500, %r9w
    addq $17, %r12
    cmpw $1, (%r12)
    cmpw $255, (%r13)
    andw $-4096, 6(%r14)
    testw $0x1234, (%rax)
    movw $0x5678, 8(%r12)
    addw $1, 2(%rax)
    cmpb $1, 3(%r15)
    sbbl %eax, %r10d
    sbbq %r8, %r9
    sbbl $-1, %eax
    shrl %eax
    shrq %r8
    sarq %r9
    shll %r10d
    salq %rdx
    shrl $1, %ecx
    shrq $3, %r8
    salq $4, %rax
    sarl $2, %r9d
    testb $1, %al
    testb $255, %r8b
    pushq $0
    pushq $4096
    movb $0, 7(%rsp)
    # Forward/backward native rel8 and out-of-range rel32 are GNU-oracled.
.Lback_x86:
    jne .Lshort_x86
    nop
.Lshort_x86:
    jnb .Lback_x86
    jmp .Lafter_x86
    .space 200, 0x90
.Lafter_x86:
    jnb .Lback_x86
    # This .org would be backwards if an in-range JMP were forced rel32.
.Lorg_x86:
    jmp .Lend_org_x86
    .org .Lorg_x86+12
.Lend_org_x86:
    nop
    popq %rbp
    ret
.size entry_x, .-entry_x
.globl unresolved_x86
unresolved_x86:
    call ext_target
    callq ext_target
    jmp ext_target
    je ext_target
    jne ext_target
    ja ext_target
    jnb ext_target
    jl ext_target
    movl %eax, 8(%rbx,%r8,2)
    movl 8(%rbx,%r9,4), %eax
    .pushsection __minias_bug_table,"aw"
2:
    .long external_function - .
    .long ext_global - .
    .word 25
    .word 0
    .org 2b+12
    .align 16
    .popsection
    movq ext_global(%rip), %rax
    movl %r8d, ext_global+8(%rip)
    cmpq ext_global(%rip), %r9
    movq %gs:40, %rax
    movq %gs:pcpu_hot, %rax
    movq %rax, %gs:pcpu_hot
    movl %fs:12, %eax
    leaq 16(%rdi,%rsi,4), %rax
    leaq ext_global(%rip), %rdx
    movq $0, %rax
    movq $-5, %r8
    movq $0, (%rsp)
    movq $-1, 8(%r12)
    movl $0, 4(%rsp)
    addq $1, (%rax)
    cmpq $0, (%rbx)
    subq $4096, 8(%rbp)
    andl $15, (%r8)
    orq $2, (%r9)
    xorq $255, (%rax)
    testq $2, 8(%rdi)
    movq $ext_global, %rsi
    # Same typed ABS32S fixup as movq $sym,%r64 with a ModR/M memory operand.
    movq $abs_mem_immediate, 8(%rsp)
    movq $abs_mem_immediate+8, (%r12)
    movq $4294967296, %rax
    movabsq $0x1122334455667788, %r8
    movabsq $ext_global, %r9
    setne %al
    sete %sil
    setg %r9b
    testb %al, %al
    xorb %r8b, %r9b
    cmpb %dil, %spl
    movslq %edi, %rdi
    movslq %r9d, %r12
    pause
    cltq
    cqto
    rep stosq
    rep movsq
    lock
    addl $1, (%rdi)
.data
    .ascii "/*preserve literal*/"
.globl common_space_test
common_space_test:
    .quad ext_object64
    .long ext_object32
    .skip 8, 0x5a
    .space 5, 0x33
    .skip (2+3)*2, (0x30+3)
    .zero 3
.local local_probe
local_probe:
    .byte 0x7f
    # GNU x86 SysV: .word is TWO bytes, unlike A64/RISC-V .word.
    .align 16
    .word 0x1234
    .comm common_implicit,3
    .comm common_symbol,24,8
    .comm common_symbol,32,16
    # GNU retains GLOBAL COMMON even if a late .local appears.
    .local common_symbol
    .local local_comm_symbol
    .comm local_comm_symbol,4,4
    .data
    .quad common_symbol
ASM_X
if ! "$MINIAS" --target=x86_64 -march=x86-64 -mabi=sysv -o "$work/x86-mini.o" "$work/x86.s" >"$work/x86.stdout" 2>"$work/x86.stderr"; then
    echo 'MINIAS_X86_PARSE_OR_ENCODE_FAIL' >&2
    cat "$work/x86.stdout" "$work/x86.stderr" >&2
    exit 1
fi
as --64 -o "$work/x86-gnu.o" "$work/x86.s"
objcopy -j .text -O binary "$work/x86-gnu.o" "$work/x86-gnu.bin"
objcopy -j .text -O binary "$work/x86-mini.o" "$work/x86-mini.bin"
cmp "$work/x86-mini.bin" "$work/x86-gnu.bin"
objcopy -j .data -O binary "$work/x86-mini.o" "$work/x86-mini-data.bin"
objcopy -j .data -O binary "$work/x86-gnu.o" "$work/x86-gnu-data.bin"
cmp "$work/x86-mini-data.bin" "$work/x86-gnu-data.bin"
objcopy -j __minias_bug_table -O binary "$work/x86-mini.o" "$work/x86-mini-bug.bin"
objcopy -j __minias_bug_table -O binary "$work/x86-gnu.o" "$work/x86-gnu-bug.bin"
cmp "$work/x86-mini-bug.bin" "$work/x86-gnu-bug.bin"
readelf -h "$work/x86-mini.o" | grep -q 'Advanced Micro Devices X86-64'
readelf -Ws "$work/x86-mini.o" > "$work/x86-mini-symbols.txt"
readelf -Ws "$work/x86-gnu.o" > "$work/x86-gnu-symbols.txt"
for symbols in x86-mini x86-gnu; do
    grep -Eq '0000000000000004 +3 OBJECT +GLOBAL +DEFAULT +COM common_implicit' "$work/$symbols-symbols.txt"
    grep -Eq '0000000000000010 +24 OBJECT +GLOBAL +DEFAULT +COM common_symbol' "$work/$symbols-symbols.txt"
    grep -Eq '0000000000000000 +4 OBJECT +LOCAL +DEFAULT +[0-9]+ local_comm_symbol' "$work/$symbols-symbols.txt"
done
echo 'MINIAS_COMMON_ELF=PASS SHN_COMMON_size_align_repeat_local_BSS'
cat > "$work/common-not-absolute.s" <<'COMMON_NOT_ABS'
.comm symbol_in_common,8,8
.if symbol_in_common
    nop
.endif
COMMON_NOT_ABS
if "$MINIAS" --target=x86_64 -o "$work/common-invalid.o" \
        "$work/common-not-absolute.s" >"$work/common-invalid.out" \
        2>"$work/common-invalid.err"; then
    echo 'ELF SHN_COMMON alignment misused as absolute expression' >&2
    exit 1
fi
if as --64 -o "$work/common-gnu-invalid.o" \
        "$work/common-not-absolute.s" >/dev/null 2>&1; then
    echo 'Unexpected GNU GAS acceptance of COMMON absolute expression' >&2
    exit 1
fi
echo 'MINIAS_COMMON_NONABS=PASS fail_closed_GNU_GAS_agrees'
readelf -Wr "$work/x86-mini.o" >"$work/x86-rel.txt"
grep -q R_X86_64_PLT32 "$work/x86-rel.txt"
grep -q R_X86_64_PC32 "$work/x86-rel.txt"
readelf -Wr "$work/x86-gnu.o" >"$work/x86-gnu-rel.txt"
grep -q R_X86_64_PLT32 "$work/x86-gnu-rel.txt"
grep -q R_X86_64_PC32 "$work/x86-gnu-rel.txt"
grep -q R_X86_64_32S "$work/x86-rel.txt"
grep -q R_X86_64_32S "$work/x86-gnu-rel.txt"
test "$(grep -c 'R_X86_64_32S.*__per_cpu_offset' "$work/x86-rel.txt")" -eq 2
test "$(grep -c 'R_X86_64_32S.*__per_cpu_offset' "$work/x86-gnu-rel.txt")" -eq 2
test "$(grep -c 'R_X86_64_32S.*abs_mem_immediate' "$work/x86-rel.txt")" -eq 2
test "$(grep -c 'R_X86_64_32S.*abs_mem_immediate' "$work/x86-gnu-rel.txt")" -eq 2
grep -q R_X86_64_64 "$work/x86-rel.txt"
grep -q R_X86_64_64 "$work/x86-gnu-rel.txt"
grep -q R_X86_64_32 "$work/x86-rel.txt"
grep -q R_X86_64_32 "$work/x86-gnu-rel.txt"
# GNU and MiniAS must agree on every relocation type, offset, addend.
# GNU GAS and MiniELF may serialize RELA entries in different orders.
# Compare exact relocation offset, type, symbol, and addend after sorting.
readelf -Wr "$work/x86-mini.o" | awk '$3 ~ /^R_X86_64_/ {print $1,$3,$5,$6,$7}' |
    LC_ALL=C sort >"$work/x86-mini-kinds.txt"
readelf -Wr "$work/x86-gnu.o" | awk '$3 ~ /^R_X86_64_/ {print $1,$3,$5,$6,$7}' |
    LC_ALL=C sort >"$work/x86-gnu-kinds.txt"
diff -u "$work/x86-gnu-kinds.txt" "$work/x86-mini-kinds.txt"

cat > "$work/la.s" <<'ASM_LA'
.text
.globl entry_la
.type entry_la, @function
entry_la:
.Lgcc_anchor_start:
    nop
.Lgcc_anchor_after_nop = .
    li.w $r4, -14
    bytepick.w $r24, $r26, $r24, 2
    bytepick.d $r20, $r15, $r20, 1
    iocsrwr.w $r12, $r13
    iocsrrd.w $r12, $r13
    iocsrrd.d $r13, $r12
    iocsrrd.b $r14, $r13
    iocsrrd.h $r12, $r15
    iocsrwr.d $r15, $r12
    iocsrwr.b $r16, $r13
    iocsrwr.h $r17, $r14
    movgr2fr.d $f30, $r13
    movgr2fr.w $f19, $r15
    rotr.w $r13, $r13, $r18
    rotr.d $r14, $r15, $r16
    cacop 13, $r12, 0
    cacop 31, $r15, -2048
    tlbrd
    tlbwr
    tlbsrch
    tlbfill
    li.w $r12, 16384
    li.w $r28, (1 << 14) / 8
    li.w $r13, 0x1020
    li.d $r15, 16384
    li.d $r2, ~16383
    vxor.v $vr4, $vr4, $vr8
    vxor.v $vr0, $vr0, $vr4
    vori.b $vr4, $vr0, 0
    vandi.b $vr1, $vr2, 255
    fst.d $f0, $r13, 16 - 16
    fld.d $f1, $r13, 8
    fst.s $f2, $r15, -2048
    vsrli.b $vr8, $vr4, 4
    vslli.w $vr8, $vr4, 31
    vsrai.d $vr8, $vr4, 63
    xvxor.v $xr0, $xr0, $xr2
    xvand.v $xr3, $xr4, $xr5
    movcf2gr $r13, $fcc0
    movcf2gr $r15, $fcc7
    vslti.b $vr12, $vr4, 0
    vslti.d $vr12, $vr4, -16
    vshuf.b $vr4, $vr22, $vr22, $vr4
    vbitsel.v $vr0, $vr1, $vr2, $vr3
    xvshuf.b $xr0, $xr2, $xr3, $xr4
    xvbitsel.v $xr4, $xr1, $xr2, $xr0
    movgr2cf $fcc0, $r14
    movgr2cf $fcc7, $r15
    xvori.b $xr2, $xr0, 0
    xvxori.b $xr8, $xr9, 255
    la.pcrel $r12, example_percpu
    la.pcrel $r13, sample_kernel_sp
    movfcsr2gr $r13, $fcsr0
    movfcsr2gr $r14, $fcsr2
    invtlb 1, $zero, $zero
    invtlb 31, $r12, $r13
    vld $vr22, $r27, 0
    vld $vr0, $r5, 0
    vst $vr22, $r27, -2048
    xvld $xr12, $r14, 2047
    xvst $xr31, $r3, 16
    movgr2fcsr $fcsr0, $r12
    movfr2gr.d $r5, $f30
    add.d $a0, $a0, $a1
    addu16i.d $a0, $a1, 0
    addu16i.d $a2, $sp, 32767
    addu16i.d $a3, $tp, -32768
    addu16i.d $r31, $r0, -1
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
    slti $a0, $a1, -2048
    sltui $a1, $a0, 2047
    andi $a0, $a1, 4095
    ori $a0, $a1, 1024
    xori $a0, $a1, 255
    slli.w $a0, $a1, 31
    slli.d $a1, $a0, 63
    srli.w $a0, $a1, 17
    srli.d $a1, $a0, 42
    srai.w $a0, $a1, 31
    srai.d $a0, $a1, 63
    rotri.w $a0, $a1, 13
    rotri.d $a0, $a1, 35
    lu12i.w $r12, -4096>>12
    lu12i.w $r13, 4096>>12
    lu32i.d $r13, 0x800000000000>>32
    addi.d $r12, $r13, 16>>2
    alsl.w $a0, $a1, $a2, 1
    alsl.wu $a1, $a0, $a2, 4
    alsl.d $a0, $a1, $a2, 2
    lu12i.w $a0, -524288
    lu32i.d $a0, 524287
    pcalau12i $a1, 1
    pcaddu12i $a1, -2
    maskeqz $a0, $a1, $a2
    masknez $a0, $a1, $a2
    sll.d $a0, $a1, $a2
    sra.w $a1, $a0, $a2
    mul.d $a0, $a1, $a2
    sub.d $a0, $a0, $a1
    ldx.d $a0, $a1, $a2
    ldx.w $a1, $a0, $a2
    ldx.bu $a2, $a1, $a0
    ldx.hu $a2, $a0, $a1
    stx.d $a1, $a2, $a0
    stx.b $a0, $a1, $a2
    stx.w $a2, $a1, $a0
    amadd_db.w $a0, $a1, $a2
    amadd_db.w $a0, $a1, $a2, 0
    amadd.w $a0, $a1, $a2, 0
    amadd.d $a2, $a1, $a0, 0
    amand.d $a0, $a2, $a1, 0
    amor.d $a0, $a1, $a2, 0
    amxor_db.d $a0, $a1, $a2, 0
    ammin.du $a2, $a0, $a1, 0
    amadd_db.d $a2, $a0, $a1
    amand_db.d $a1, $a2, $a0
    amor_db.d $a0, $a2, $a1
    amxor_db.d $a0, $a1, $a2
    amswap_db.w $a1, $a2, $a0
    revb.2h $a0, $a1
    revb.4h $a1, $a0
    clz.d $a0, $a1
    ctz.d $a1, $a0
    bitrev.d $a0, $a1
    div.du $a0, $a1, $a2
    xvreplve0.q $xr22, $xr22
    xvreplve0.w $xr3, $xr4
    xvreplve0.b $xr5, $xr6
    vinsgr2vr.d $vr0, $r13, 1
    vinsgr2vr.w $vr1, $r14, 3
    vinsgr2vr.h $vr2, $r15, 7
    vinsgr2vr.b $vr3, $r16, 15
    xvslti.b $xr6, $xr2, 0
    xvslli.b $xr8, $xr2, 1
    xvslli.h $xr3, $xr4, 15
    xvslli.w $xr5, $xr6, 31
    xvslli.d $xr9, $xr7, 63
    xvsrli.b $xr2, $xr3, 7
    xvsrli.h $xr3, $xr4, 14
    xvsrli.w $xr5, $xr6, 30
    xvsrli.d $xr7, $xr8, 62
    xvslti.h $xr3, $xr31, -16
    xvslti.w $xr4, $xr5, 15
    xvslti.d $xr8, $xr7, -1
    xvpickve2gr.d $r17, $xr31, 0
    xvpickve2gr.d $a0, $xr3, 3
    xvpickve2gr.w $a1, $xr4, 7
    xvpickve2gr.wu $a3, $xr6, 7
    xvpickve2gr.du $a4, $xr7, 3
    vpickve2gr.d $r13, $vr0, 1
    vpickve2gr.b $a0, $vr31, 15
    vpickve2gr.wu $a1, $vr2, 3
    vpickve2gr.hu $a2, $vr3, 7
    div.wu $a0, $a1, $a2
    mulh.du $a0, $a1, $a2
    ll.w $a0, $sp, -4
    sc.w $a1, $sp, 4
    ll.d $a2, $sp, -8
    sc.d $a1, $sp, 8
    csrrd $a0, 0x100
    csrwr $a1, 0x20
    csrxchg $a2, $a1, 0x123
    and $a0, $a0, $a1
    or $a0, $a0, $a1
    xor $a0, $a0, $a1
    move $a0, $a1
    jr $r12
    jirl $a0, $a1, 4
    jirl $zero, $ra, -131072
    jirl $ra, $a0, 131068
    break 1
    break 32767
    syscall 0
    dbar 18
    ibar 16
    bstrpick.d $a0, $a1, 31, 0
    bstrpick.w $a1, $a0, 7, 0
    bstrins.d $a2, $a3, 63, 1
    bstrins.w $a3, $a2, 30, 2
    bltz $r13, .Lla_extra
    bgtz $r13, .Lla_extra
    bgez $r13, .Lla_extra
    blez $r13, .Lla_extra
    preld 0, $r23, 0
    preld 8, $r12, -2048
    preld 31, $r13, 2047
    preldx 0, $r12, $r13
    rdtime.d $r13, $r12
    rdtimel.w $r12, $r13
    rdtimeh.w $r12, $r13
    cpucfg $r12, $r13
    idle 0
    idle 32767
.Lla_extra:
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

for bad in 'movq __per_cpu_offset(%rdi),%rax' \
           'adcl %rax,%eax' \
           'inb %eax,%al' \
           'outw %al,%dx' \
           'sbbb %rax,%r8b' \
           'sbbb $256,%r8b' \
           'pushq %eax' \
           'cmove %rax, (%rdi)' \
           'bsfq %rax, (%rdi)' \
           'cmovb %al, %al' \
           'testq $4294967296, %rax' \
           'testw $65536, %ax' \
           'testl %rax, %eax' \
           'add %gs:this_cpu_off(%rip), (%rax)' \
           'mulq %eax' \
           'imull %rax,%eax' \
           'imulq %edx,%rax' \
           'imulq $2147483648,%rax,%rax' \
           'imulq $8,%eax,%rax' \
           'imull $8,%rax,%edx' \
           'movzbl %ah,%r8d' \
           'movzbq %ch,%rax' \
           'divl %rax' \
           'cmpxchgq %eax, (%rdi)' \
           'bsfq %rax, %eax' \
           'bswapw %ax' \
           'shrb $256, %al' \
           'shrw $256, %ax' \
           'shrb %cl, %ax' \
           'rolw %ah' \
           'testb $256, %ah' \
           'movq $abs_mem_immediate, abs_mem_addr(%rip)' \
           'movw $65536, (%rax)' \
           'andw $-32769, (%rax)' \
           'cmpb $256, %al' \
           'andw $65536, %ax' \
           'xorw %rax, %ax' \
           'testw %al, %ax' \
           'movb %eax, (%rax)' \
           'movw %eax, (%rax)' \
           'cmpb %rax, (%rax)' \
           'movzbl %ax, %eax' \
           'movzwl %al, %eax' \
           'movzbq %al, %eax' \
           'movzwl (%rsp), %rax' \
           'movslq %rax, %r8' \
           'movq %rax, (%eax)' \
           'movl 4294967296(%rax), %eax' \
           'movq %rax, (%rax,%rsp,2)' \
           'movq %rax, (%rax,%eax,4)' \
           'movq %rax, (%rax,%rcx,3)' \
           'movq %rax, (%rax,%rcx,16)' \
           'movq %rax, (%rax,%rcx,2,7)' \
           'movq ext_global(%rax), %r9' \
           'movq ext_global(%rip,%rax,2), %r9' \
           'movq %rax, ext_global(%rbp)' \
           'movq %gs:4294967296, %rax' \
           'movq %fs:pcpu_hot(%rax), %rax' \
           'movq $18446744073709551616, %r8' \
           'cmpq $4294967296, (%rax)' \
           'leaq %rax, (%rsp)' \
           'setne %ah' \
           'movslq %rdi, %r8' \
           'testb %ah, %r8b' \
           'movabsq $ext_global, (%rax)' \
           'movzbl %rax, %eax' \
           'shrq $256, %rax' \
           'pushq $4294967296' \
           'testb $256, %al' \
           'movb $256, (%rax)' \
           'cmpb $256, (%rax)' \
           'movq %rax, (,%rsp,8)' \
           'cmove %r8, %eax' \
           'call *%eax' \
           'jmp *%ecx' \
           'call *%gs:(%rax,%rsp,4)' \
           'jmp *%fs:(%r12,%eax,4)' \
           'shrq %dl, %rax' \
           'shrl %rax' \
           'btq %eax, (%rdi)' \
           'xaddl %rax, (%rdi)' \
           'movl %ds:pcpu_hot(%rip), %eax' \
           'movq %gs:bogus(%r8,%r9,5), %rax'; do
    printf '.text\n%s\n' "$bad" >"$work/invalid-mem.s"
    if "$MINIAS" --target=x86_64 -o "$work/bad-mem.o" \
         "$work/invalid-mem.s" >/dev/null 2>"$work/invalid.err"; then
        echo "MiniAS accepted unsupported x86 memory form: $bad" >&2
        exit 1
    fi
done
# Original LoongArch GCC / Linux symbolic code model, GNU object as oracle.
cat >"$work/la-reloc.s" <<'LA_RELOC'
.text
.globl reloc_entry
.type reloc_entry,@function
reloc_entry:
    pcalau12i $a0, %pc_hi20(_external_data)
    addi.d $a0, $a0, %pc_lo12(_external_data)
    ld.d $a1, $a0, %pc_lo12(_external_data)
    b _external_branch
    bl _external_call
    beqz $a0, _external_branch
    bnez $a1, _external_branch
    beq $a0, $a1, _external_branch
    bne $a0, $a1, _external_branch
    blt $a0, $a1, _external_branch
    bge $a0, $a1, _external_branch
    bltu $a0, $a1, _external_branch
    bgeu $a0, $a1, _external_branch
    bgtu $a0, $a1, _external_branch
    bleu $a0, $a1, _external_branch
    bgt $a0, $a1, _external_branch
    ble $a0, $a1, _external_branch
.size reloc_entry, .-reloc_entry
LA_RELOC
"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/rel-mini.o" "$work/la-reloc.s"
"$LOONGAS" -mabi=lp64d -o "$work/rel-gnu.o" "$work/la-reloc.s"
"$LOONGCOPY" -j .text -O binary "$work/rel-mini.o" "$work/rel-mini.bin"
"$LOONGCOPY" -j .text -O binary "$work/rel-gnu.o" "$work/rel-gnu.bin"
if ! cmp -s "$work/rel-mini.bin" "$work/rel-gnu.bin"; then
    echo 'MINIAS_LARCH_BYTES_MISMATCH: exact per-word GNU and MiniAS output'
    echo 'GNU bytes:'; od -An -tx4 -w4 "$work/rel-gnu.bin"
    echo 'MiniAS bytes:'; od -An -tx4 -w4 "$work/rel-mini.bin"
    echo 'GNU relocations:'; "$LOONGREADELF" -Wr "$work/rel-gnu.o"
    echo 'MiniAS relocations:'; "$LOONGREADELF" -Wr "$work/rel-mini.o"
    exit 1
fi
"$LOONGREADELF" -Wr "$work/rel-mini.o" >"$work/rel-mini.txt"
"$LOONGREADELF" -Wr "$work/rel-gnu.o" >"$work/rel-gnu.txt"
for kind in R_LARCH_PCALA_HI20 R_LARCH_PCALA_LO12 R_LARCH_B16 R_LARCH_B21 R_LARCH_B26; do
    grep -q "$kind" "$work/rel-mini.txt"
    grep -q "$kind" "$work/rel-gnu.txt"
done

# Loong psABI uses CFA stack register r3, link register r1 and -8 data
# alignment. Verify actual GNU DWARF debug-frame records, not no-op parsing.
cat >"$work/la-cfi.s" <<'LA_CFI'
.cfi_sections .debug_frame
.text
.globl la_cfi_entry
la_cfi_entry:
    .cfi_startproc
    addi.d $sp, $sp, -16
    .cfi_def_cfa_offset 16
    st.d $ra, $sp, 8
    .cfi_offset 1, -8
    ld.d $ra, $sp, 8
    .cfi_restore 1
    addi.d $sp, $sp, 16
    .cfi_def_cfa_offset 0
    ret
    .cfi_endproc
LA_CFI
"$MINIAS" --target=loongarch64 -o "$work/la-cfi-mini.o" "$work/la-cfi.s"
"$LOONGAS" -mabi=lp64d -o "$work/la-cfi-gnu.o" "$work/la-cfi.s"
"$LOONGCOPY" -j .text -O binary "$work/la-cfi-mini.o" "$work/la-cfi-mini.bin"
"$LOONGCOPY" -j .text -O binary "$work/la-cfi-gnu.o" "$work/la-cfi-gnu.bin"
cmp "$work/la-cfi-mini.bin" "$work/la-cfi-gnu.bin"
"$LOONGREADELF" --debug-dump=frames "$work/la-cfi-mini.o" >"$work/la-cfi-mini.frame"
"$LOONGREADELF" --debug-dump=frames "$work/la-cfi-gnu.o" >"$work/la-cfi-gnu.frame"
for marker in CIE FDE 'DW_CFA_def_cfa_offset: 16' 'DW_CFA_offset: r1'; do
    grep -q "$marker" "$work/la-cfi-mini.frame"
    grep -q "$marker" "$work/la-cfi-gnu.frame"
done
"$LOONGREADELF" -Wr "$work/la-cfi-mini.o" | grep -q R_LARCH_64
echo 'MINIAS_LARCH_DWARF_CFI=PASS cfa_stack_r3=1 link_r1=1 real_FDE=1'

# Absolute symbol arithmetic, GNU '=' conditional alias and comparisons.
cat >"$work/la-if.s" <<'LA_IF'
.text
.equ docfi, 0
.if docfi == 0
    nop
.endif
.if (7 << 1) > 10
    ret
.else
    break 1
.endif
.if docfi != 0
    break 2
.else
    nop
.endif
LA_IF
"$MINIAS" --target=loongarch64 -o "$work/la-if-mini.o" "$work/la-if.s"
"$LOONGAS" -mabi=lp64d -o "$work/la-if-gnu.o" "$work/la-if.s"
"$LOONGCOPY" -j .text -O binary "$work/la-if-mini.o" "$work/la-if-mini.bin"
"$LOONGCOPY" -j .text -O binary "$work/la-if-gnu.o" "$work/la-if-gnu.bin"
cmp "$work/la-if-mini.bin" "$work/la-if-gnu.bin"
echo 'MINIAS_COMMON_ABSOLUTE_IF=PASS symbol_equals_compare_shift'

echo 'MINIAS_LARCH_RELOC_ORACLE=PASS text_bytes=equal required_ELF_reloc_families=5'
# Same-section forward/backward LoongArch branches must use the
# original RV-proven symbol-layout resolver rather than producing a redundant
# external ELF RELA or guessing a GNU relaxation.
cat >"$work/la-local.s" <<'LA_LOCAL'
.text
.globl local_entry
.type local_entry,@function
local_entry:
.Lback:
    nop
    beqz $a0, .Lforward
    b .Lforward
    beq $a0, $a1, .Lback
    bgtu $a0, $a1, .Lforward
.Lforward:
    bnez $a1, .Lback
    ret
.size local_entry, .-local_entry
LA_LOCAL
"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/la-local-mini.o" "$work/la-local.s"
"$LOONGAS" -mabi=lp64d -o "$work/la-local-gnu.o" "$work/la-local.s"
"$LOONGCOPY" -j .text -O binary "$work/la-local-mini.o" "$work/la-local-mini.bin"
"$LOONGCOPY" -j .text -O binary "$work/la-local-gnu.o" "$work/la-local-gnu.bin"
cmp "$work/la-local-mini.bin" "$work/la-local-gnu.bin"
# The GNU assembler may retain R_LARCH_MARK_LA/relax bookkeeping; the
# machine-level code and defined local labels must still be identical.
echo 'MINIAS_LARCH_LOCAL_BRANCH_ORACLE=PASS same_section_forward_backward_bytes=equal'

# The exact same original Linux GNU source uses cross-section symbol
# difference in data tables. Support ADD32/SUB32 and ADD64/SUB64 in the
# RV-proven common SymbolExpr -> Fixup -> ELF writer (Loong type only).
cat >"$work/la-diff.s" <<'LA_DIFF'
.data
.globl la_difference
la_difference:
    .long external_item - .
    .quad external_item - .
LA_DIFF
"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
   -o "$work/la-diff-mini.o" "$work/la-diff.s"
"$LOONGAS" -mabi=lp64d -o "$work/la-diff-gnu.o" "$work/la-diff.s"
"$LOONGREADELF" -Wr "$work/la-diff-mini.o" > "$work/la-diff-mini-rel.txt"
"$LOONGREADELF" -Wr "$work/la-diff-gnu.o" > "$work/la-diff-gnu-rel.txt"
for reloc in R_LARCH_ADD32 R_LARCH_SUB32 R_LARCH_ADD64 R_LARCH_SUB64; do
    grep -q "$reloc" "$work/la-diff-mini-rel.txt"
    grep -q "$reloc" "$work/la-diff-gnu-rel.txt"
done
echo 'MINIAS_LARCH_SYMBOL_DIFF=PASS ELF_ADD_SUB_32_64'

for bad in 'li.w $r4, -2049' \
           'cacop 32, $r12, 0' \
           'cacop 0, $r12, -2049' \
           'iocsrrd.d $r13, $f30' \
           'movgr2fr.d $f32, $r13' \
           'rotr.w $r1, $r2, 6' \
           'tlbrd $r1' \
           'vshuf.b $vr0, $vr1, $vr2, $xr3' \
           'xvshuf.b $xr0, $xr1, $xr2, $vr3' \
           'movgr2cf $fcc8, $r14' \
           'xvori.b $xr0, $xr1, 256' \
           'vsrli.b $vr1, $vr2, 8' \
           'xvxor.v $xr32, $xr0, $xr1' \
           'movcf2gr $r1, $fcc8' \
           'vslti.b $vr1, $vr2, 16' \
           'vori.b $vr1, $vr2, 256' \
           'vxor.v $vr1, $vr2, $r3' \
           'fst.d $f32, $r1, 0' \
           'fst.d $f0, $r1, -2049' \
           'la.pcrel $f1, invalid_sym' \
           'la.pcrel $r12' \
           'invtlb 32, $r0, $r0' \
           'vld $vr32, $r5, 0' \
           'vst $vr0, $r5, 2048' \
           'xvld $xr0, $f1, 0' \
           'movfcsr2gr $r1, $fcsr4' \
           'bytepick.w $r1,$r2,$r3,4' \
           'bytepick.d $r1,$r2,$r3,8' \
           'movgr2fcsr $fcsr4,$r12' \
           'movfr2gr.d $r5,$f32' \
           'ldptr.d $a0, $sp, 3' \
           'stptr.w $a0, $sp, 32768' \
           'ldptr.d $a1, $sp, -32772' \
           'andi $a0, $a1, -1' \
           'ori $a0, $a1, 4096' \
           'slli.w $a0, $a1, 32' \
           'addu16i.d $a0, $a1, 32768' \
           'addu16i.d $a0, $a1, -32769' \
           'xvslti.b $xr0, $xr1, 16' \
           'xvpickve2gr.d $r1, $xr0, 4' \
           'xvpickve2gr.w $r1, $xr0, 8' \
           'xvpickve2gr.b $r1, $xr0, 1' \
           'xvpickve2gr.hu $r1, $xr0, 1' \
           'xvslli.b $xr0, $xr1, 8' \
           'xvreplve0.q $xr0, $vr1' \
           'vinsgr2vr.d $vr0, $r2, 2' \
           'vinsgr2vr.h $vr0, $r2, 8' \
           'xvsrli.d $xr0, $xr1, 64' \
           'xvslti.d $xr0, $xr1, -17' \
           'vpickve2gr.d $r1, $vr0, 2' \
           'vpickve2gr.wu $a0, $vr2, 4' \
           'srli.d $a0, $a1, 64' \
           'lu12i.w $a0, 524288' \
           'pcalau12i $a0, -524289' \
           'jirl $a0, $a1, 3' \
           'jirl $a0, $a1, 131072' \
           'jirl $a0, $a1, -131076' \
           'break -1' \
           'dbar 32768' \
           'bstrpick.w $a0, $a1, 32, 1' \
           'bstrpick.d $a0, $a1, 2, 3' \
           'alsl.d $a0, $a1, $a2, 0' \
           'alsl.w $a0, $a1, $a2, 5' \
           'csrrd $a0, 16384' \
           'csrwr $a0, -1' \
           'csrxchg $a0, $zero, 1' \
           'csrxchg $a0, $ra, 1' \
           'amadd_db.w $a0, $a1, $a2, 1'; do
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
# Unsupported GOT/TLS relocation modifiers must NOT accidentally be treated
# as numeric PCALA relocations.
printf '.text\npcalau12i $a0, %%got_pc_hi20(unknown)\n' > "$work/unimplemented.s"
if "$MINIAS" --target=loongarch64 -o "$work/invalid.o" "$work/unimplemented.s" \
        >/dev/null 2>"$work/invalid.err"; then
    echo 'Unimplemented GOT/fixup silently accepted' >&2
    exit 1
fi
# GNU .fill DATA fragments: evaluate complete byte stream independently
# on x86-64 and LA64 with real GNU object producers.
cat > "$work/gnu-fill.s" <<'GNU_FILL'
.section .data
    .byte 0x56
    .fill 3,2,0x1234
    .fill 2,4,0x55667788
    .fill 2,8,0x01020304
    .fill 0,1,0
    .byte 0x78
GNU_FILL
"$MINIAS" --target=loongarch64 -o "$work/la-fill-mini.o" "$work/gnu-fill.s"
"$LOONGAS" -mabi=lp64d -o "$work/la-fill-gnu.o" "$work/gnu-fill.s"
"$LOONGCOPY" -j .data -O binary "$work/la-fill-mini.o" "$work/la-fill-mini.bin"
"$LOONGCOPY" -j .data -O binary "$work/la-fill-gnu.o" "$work/la-fill-gnu.bin"
cmp "$work/la-fill-mini.bin" "$work/la-fill-gnu.bin"
echo 'MINIAS_COMMON_FILL=PASS GNU_LA64_data_bytes=equal'
echo 'MINIAS_X86_SEGMENTED_INDIRECT=PASS GNU_AS_text_and_relocations segment_base_index_abs_rip=true indirect_memory_call_jmp=true absolute_symbol32S=true'
echo 'MINIAS_TWO_TARGET_ORACLE=PASS x86_GNU_bytes=equal loong_GNU_bytes=equal ELF_machine=correct lp64d_v1=0x43 CET_endbr64=1 ALU_imm8_imm32=1 x86_ModRM_SIB_disp=1 x86_test_cmp_alu_memory=1 x86_scaled_index_SIB=1 LA_jr=1 LA_scaled_ptr14=1 LA_imm12_shift_upper20=1 invalid=reject'
