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
    # Common GNU string-conditional machinery, including .else nesting.
    .ifc "gnu", "gnu"
       nop
    .else
       int3
    .endif
    .ifnc alpha,beta
       nop
    .else
       int3
    .endif
    .ifc foo,bar
       int3
    .else
       nop
    .endif
    .ifc x,x
       .ifnc red,blue
          nop
       .else
          int3
       .endif
    .endif
    nop
    /* Strict GNU quoted conditional strings (including internal commas). */
    .ifeqs "A,B","A,B"
       nop
    .else
       int3
    .endif
    .ifnes "left","right"
       nop
    .endif
    .ifeqs "left","right"
       int3
    .else
       nop
    .endif
    /* GNU numeric conditionals are Common, not target syntax. */
    .set __minias_condition_value, 2
    .ifeq 0
       nop
    .else
       int3
    .endif
    .ifne (__minias_condition_value - 1)
       nop
    .endif
    .ifge 0
       nop
    .endif
    .ifgt 7 - 6
       nop
    .endif
    .ifle -1
       nop
    .endif
    .iflt -1
       nop
    .endif
    .ifnotdef __minias_future_label
       nop
    .else
       int3
    .endif
    .if 0
       .ifeq unresolved_unreachable_label
          int3
       .endif
    .else
       nop
    .endif
    .ifgt -7
       int3
    .else
       nop
    .endif
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
    # GNU bit-imm with ModR/M register, SIB memory, RIP-relative relocation.
    # GNU 69/6B IMUL with typed memory source, signed imm8/16/32.
    imull $1000, 636(%rbx), %r15d
    imull $125, 16(%rax), %eax
    imull $1640531527, 4(%r9), %eax
    imull $1000, pm_test_delay(%rip), %ebx
    imulq $1000, 8(%rsp), %r13
    imulq $100, ext_global(%rip), %r12
    imulw $200, 2(%rax), %r10w
    rep movsb
    rep movsl
    rep stosl
    repz cmpsb
    repne scasb
    movq $abs_mem_immediate, abs_mem_addr(%rip)
    # Real GCC imm-to-memory symbolic and dual-relocation instructions.
    cmpq $apic_physflat, apic(%rip)
    cmpq $truncate_bdev_range, 72(%rdi)
    cmpq $shash_no_setkey, -48(%r14)
    movq $ext_global, x86_pmu(%rip)
    movq $pt_attr_groups, pt_pmu+40(%rip)
    movq $pci_mmcfg, raw_pci_ext_ops(%rip)
    addq $ext_global, 16(%rbx)
    subq $ext_global+4, 48(%rbp)
    andq $ext_global, ext_global(%rip)
    cmpq $ext_global, ext_global+8(%rip)
    # ELF ABS32/ABS32S typed symbol immediates and GNU accumulator.
    cmpq $ip6_output, %rax
    cmpq $init_net, %rbx
    cmpq $ip6_mtu, %r9
    addq $ext_global, %rax
    subq $ext_global+4, %r12
    andl $ext_global, %eax
    xorl $ext_global, %r10d
    pushq $ext_global
    cmpq $1000, %rax
    addl $1000, %eax
    btq $43, %rax
    btsq $0, (%rbx)
    btrl $127, 8(%r12)
    btcl $1, ext_global(%rip)
    btsq $15, __cpu_online_mask(%rip)
    # Mandatory F3 register and memory POPCNT must equal GNU byte stream.
    # GNU byte memory and full unsigned 32-bit immediate oracle.
    movb %dh, 17(%rcx)
    movb 9(%rbx), %ah
    movb %ah, -2(%rdi)
    movb hex_asc(%rdx), %bh
    andb %ah, 7(%rbp)
    xorb (%rax), %ch
    testb %bh, 3(%rcx)
    andb $-129, 57(%rbx)
    andb $-129, %al
    andl $4294967295, %ebp
    andl $4294967295, %eax
    andl $4294963200, %eax
    andl $4286906367, %edx
    # GNU AT&T unsuffixed opcode aliases infer width from typed registers.
    cmp $453, %rdx
    cmp $7, %ecx
    cmp %al, %ah
    mov %rax, %rbx
    mov %eax, 16(%rbx)
    mov 8(%rdi), %r9
    add $9, %r11
    sub %ecx, %eax
    and $255, %r8d
    xor %r10, %r11
    test %edx, %ecx
    push %rax
    pop %r8
    sal $3, %rax
    shl %cl, %r10
    ror $3, %r8
    # Linux kernel CR/DR/segment register transfer and PIO forms.
    mov %cr2,%r13
    mov %cr3,%rax
    mov %rax,%cr3
    mov %rax,%db7
    movq %cr0,%r15
    mov %r10,%cr8
    mov %cr8,%r9
    mov %ss,%eax
    mov %ds,%ax
    mov %rax,%ss
    mov %eax,%fs
    mov %ds,%bx
    mov %fs,%r8
    inb $0x3f, %al
    outb %al, $0x80
    outw %ax, $0x81
    inl $0x70, %eax
    pop (%rsp)
    pop 32(%rsp)
    push 8(%rbx)
    # GNU Group-2 memory shifts reuse ISA ModR/M with ELF relocations.
    sall $3, 8(%rax)
    salq $5, ext_global(%rip)
    salw $7, 12(%r10)
    salb $2, (%rbx)
    shll %cl, (%rax)
    shrq %cl, ext_global+16(%rip)
    sarl $1, -16(%rbp)
    salq (%r11)
    rorl $4, 24(%rsp)
    sarw %cl, (%rdx)
    # GNU x86 strict .code64 + descriptor-table ModR/M/system ISA.
    .code64
    lgdt 32(%rdi)
    lidt -16(%rbx)
    sgdt 8(%r8)
    sidt ext_global(%rip)
    invlpg (%rax)
    verw 16(%rsi)
    verr 20(%rdx)
    lldt (%rbp)
    ltr (%r9)
    rdpmc
    fninit
    stosw
    movsb
    movsl
    movsq
    outsw
    insw
    # GNU strict byte immediate B0+rb / legacy high versus REX.
    movb $1, %al
    movb $-1, %dl
    movb $-128, %ah
    movb $42, %sil
    movb $72, %r9b
    xsetbv
    # GNU Linux preprocessor arithmetic in memory displacements and
    # MOVL $immediate expressions: reuse Common evaluator across x86 ISA.
    addb %al, 850(%rbx)
    addb %r15b, 8(%rbp)
    addb %dl, 41(%r14)
    movq 0*8(%rsi), %r11
    movq %rbx, 0*8(%rsp)
    movq 3*8(%rdi), %r10
    movl $4096/8, %ecx
    movl $~0xfa1e0ff3, %eax
    movl $(5 << 8) | 7, %edx
    # GNU Linux >48-character symbol displacements and SHLD/SHRD group.
    movq __tracepoint_x86_fpu_xstate_check_failed+72(%rip), %rbx
    movq __tracepoint_intel_frontbuffer_invalidate+72(%rip), %r8
    movq ext_global+48(%rdi,%r8,4), %r11
    shrdq %rdx, %rax
    shrdq $32, %rdx, %rax
    shrdl %eax, %edx
    shrdw $7, %cx, %dx
    shldq %r8, %r9
    shldq $63, %r14, %r15
    shldl $17, %esi, %edi
    shldw %ax, %dx
    cwtl
    stosl
    stosq
    prefetchw (%r12)
    prefetchwt1 ext_global(%rip)
    # GNU 0F18 /0..3 and 0FAE /6..7 memory-cache hint family.
    prefetchnta (%rax)
    prefetcht0 boot_cpu_data(%rip)
    prefetcht1 8(%rbx,%rcx,4)
    prefetcht2 -4(%r8)
    clflush 16(%rdi)
    clflushopt 32(%r9)
    clwb ext_global(%rip)
    # High 8-bit legacy register IDs, including MOV and ALU; no REX.
    movb %al, %dh
    movb %cl, %ah
    movb %dh, %cl
    movb %ah, %bh
    addb %dh, %al
    orb %ch, %bl
    subb %ah, %dl
    xorb %bh, %ah
    # Real GCC/GNU-accepted whitespace in target memory addressing.
    testb $1, boot_cpu_data+114 (% rip)
    testb $8, boot_cpu_data+77 (% rip)
    movq ext_global+8 (% rip), %r9
    movq %rax, ext_global + 16 (% rip)
    cmpq $7, ext_global+24 (% rip)
    # Signed byte/word extension shares MCInst+0F machinery with MOVZX.
    movsbl %al, %eax
    movsbl %ah, %edx
    movsbl %r8b, %r9d
    movsbq %al, %r10
    movswl %ax, %ecx
    movswq %ax, %rax
    movswq %r8w, %r15
    movsbl 8(%rax), %r9d
    movswl ext_global(%rip), %ecx
    movsbq -1(%r12), %r13
    movswq 12(%r8,%r9,2), %r10
    popcntq %rax, %r8
    popcntl %eax, %ecx
    popcntq 8(%rsp), %r9
    popcntl ext_global(%rip), %r10d
    btq %rax, __cpu_online_mask(%rip)
    btsq %r9, __cpu_online_mask(%rip)
    btrl %r10d, 8(%rsp)
    btcq %r11, 16(%r12)
    xaddl %eax, (%rdi)
    xaddq %r13, 8(%rsp)
    # All x86 width variants use the existing 0x86/0x87 typed memory encoder.
    xchgl %eax, 24(%rbx)
    xchgl %eax, ext_global(%rip)
    # GNU base+symbol disp32S; no RIP-relative alias or dummy addend.
    movq ext_global(%rax), %r9
    movq %rax, ext_global(%rbp)
    movq %fs:pcpu_hot(%rax), %rax
    movq __per_cpu_offset(%rdi), %rax
    movq kmalloc_caches+48(%rax), %rdi
    movq pcpu_hot+32(%r12), %r10
    addq %r8, glob_index+8(%r13,%r10,4)
    movl %gs:this_cpu_off+12(%rbx), %eax
    setne 4(%rsp)
    sete ext_global(%rip)
    setg %fs:8(%r13)
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
    notb %al
    notb %ah
    notb %r8b
    notw %ax
    negb %al
    negb %r9b
    negb 4(%rbx)
    negw %cx
    negw 8(%r12)
    incb %r10b
    incw %r11w
    decb %bl
    decw %bx
    mulb %cl
    mulw %dx
    divb %al
    divw %cx
    idivb %r10b
    idivw %r11w
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
# GNU byte-oracle gained one real base+symbol relocation.
test "$(grep -c 'R_X86_64_32S.*__per_cpu_offset' "$work/x86-rel.txt")" -eq 3
test "$(grep -c 'R_X86_64_32S.*__per_cpu_offset' "$work/x86-gnu-rel.txt")" -eq 3
test "$(grep -c 'R_X86_64_32S.*abs_mem_immediate' "$work/x86-rel.txt")" -eq 3
test "$(grep -c 'R_X86_64_32S.*abs_mem_immediate' "$work/x86-gnu-rel.txt")" -eq 3
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

# GNU mutable .set counters used by Linux x86 extable_type_reg
# must capture the value at each .long, not at the final symbol value.
cat >"$work/x86-mutable-set.s" <<'X86_MUTABLE_SET'
.text
.set .Lregnr, 0
.long 5 + (.Lregnr << 8)
.set .Lregnr, .Lregnr+1
.long 5 + (.Lregnr << 8)
.set .Lregnr, .Lregnr+1
.long 5 + (.Lregnr << 8)
.set .Lregnr, 0
.long 9 + (.Lregnr << 8)
.set .Lregnr, .Lregnr+1
.long 9 + (.Lregnr << 8)
.long (17 | ((-14) << 16)) + (.Lregnr << 8)
.set .Lregnr, .Lregnr+1
.long (17 | ((-14) << 16)) + (.Lregnr << 8)
X86_MUTABLE_SET
"$MINIAS" --target=x86_64 -o "$work/x86-set-mini.o" "$work/x86-mutable-set.s"
as --64 -o "$work/x86-set-gnu.o" "$work/x86-mutable-set.s"
objcopy -j .text -O binary "$work/x86-set-mini.o" "$work/x86-set-mini.bin"
objcopy -j .text -O binary "$work/x86-set-gnu.o" "$work/x86-set-gnu.bin"
cmp "$work/x86-set-mini.bin" "$work/x86-set-gnu.bin"
echo 'MINIAS_COMMON_MUTABLE_SET=PASS GNU_EARLY_DATA_SNAPSHOT'
# GNU .purgem removes a named macro so the same name can be defined
# again (Linux extable_type_reg frequently defines/purges it per file).
cat >"$work/x86-purgem.s" <<'X86_PURGEM'
.text
.macro extable_type_reg
    nop
.endm
extable_type_reg
.purgem extable_type_reg
.macro extable_type_reg
    int3
.endm
extable_type_reg
.purgem extable_type_reg
.purgem undefined_extable_macro
X86_PURGEM
"$MINIAS" --target=x86_64 -o "$work/x86-purgem-mini.o" "$work/x86-purgem.s"
as --64 -o "$work/x86-purgem-gnu.o" "$work/x86-purgem.s"
objcopy -j .text -O binary "$work/x86-purgem-mini.o" "$work/x86-purgem-mini.bin"
objcopy -j .text -O binary "$work/x86-purgem-gnu.o" "$work/x86-purgem-gnu.bin"
cmp "$work/x86-purgem-mini.bin" "$work/x86-purgem-gnu.bin"
echo 'MINIAS_COMMON_PURGEM=PASS GNU_MACRO_RELEASE_AND_REDEFINE'
# Same-section symbol differences are absolute even for externally
# visible GLOBAL/WEAK bindings; no x86 ELF64 ADD/SUB relocation exists.
cat >"$work/x86-defined-diff.s" <<'X86_DEFINED_DIFF'
.text
.globl defined_delta_start
defined_delta_start:
    nop
.weak defined_delta_weak
defined_delta_weak:
    nop
.globl defined_delta_end
defined_delta_end:
    nop
.data
    .quad defined_delta_end-defined_delta_start
    .quad defined_delta_weak-defined_delta_start
    .quad defined_delta_start-defined_delta_end
    .long defined_delta_end-defined_delta_weak
    .byte defined_delta_end-defined_delta_start
X86_DEFINED_DIFF
"$MINIAS" --target=x86_64 -o "$work/x86-defined-mini.o" "$work/x86-defined-diff.s"
as --64 -o "$work/x86-defined-gnu.o" "$work/x86-defined-diff.s"
objcopy -j .data -O binary "$work/x86-defined-mini.o" "$work/x86-defined-mini.bin"
objcopy -j .data -O binary "$work/x86-defined-gnu.o" "$work/x86-defined-gnu.bin"
cmp "$work/x86-defined-mini.bin" "$work/x86-defined-gnu.bin"
readelf -Wr "$work/x86-defined-mini.o" | grep -q 'There are no relocations'
echo 'MINIAS_COMMON_SAME_SECTION_BINDINGS=PASS GNU_BYTE_EQUAL GLOBAL_WEAK_LOCAL'
# GNU x86 ELF PC32/PC64 symbol-minus-local-anchor and symbol-minus-dot.
# Use independent readelf relocation types and byte comparison.
cat >"$work/x86-pcrel-diff.s" <<'X86_PCREL_DIFF'
.data
.globl global_pcrel_base
global_pcrel_base:
.Lpc_anchor:
    .quad external_pcrel_target - .Lpc_anchor
    .quad external_pcrel_target - . + 8
.Lpc_second:
    .long external_pcrel_target - .Lpc_second
    .long external_pcrel_target - .
    .quad external_pcrel_target - .Lpc_second + 11
.globl global_pcrel_anchor
global_pcrel_anchor:
    .quad external_pcrel_target - global_pcrel_anchor
.weak weak_pcrel_anchor
weak_pcrel_anchor:
    .quad external_pcrel_target - weak_pcrel_anchor
    .quad external_pcrel_target - (.Lpc_second+11)
X86_PCREL_DIFF
"$MINIAS" --target=x86_64 -o "$work/x86-pcrel-mini.o" "$work/x86-pcrel-diff.s"
as --64 -o "$work/x86-pcrel-gnu.o" "$work/x86-pcrel-diff.s"
objcopy -j .data -O binary "$work/x86-pcrel-mini.o" "$work/x86-pcrel-mini.bin"
objcopy -j .data -O binary "$work/x86-pcrel-gnu.o" "$work/x86-pcrel-gnu.bin"
cmp "$work/x86-pcrel-mini.bin" "$work/x86-pcrel-gnu.bin"
readelf -Wr "$work/x86-pcrel-mini.o" >"$work/x86-pcrel-mini.rel"
readelf -Wr "$work/x86-pcrel-gnu.o" >"$work/x86-pcrel-gnu.rel"
for flavor in mini gnu; do
    test "$(grep -c 'R_X86_64_PC64.*external_pcrel_target' "$work/x86-pcrel-$flavor.rel")" -eq 6
    test "$(grep -c 'R_X86_64_PC32.*external_pcrel_target' "$work/x86-pcrel-$flavor.rel")" -eq 2
done
for flavor in mini gnu; do
    readelf -Wr "$work/x86-pcrel-$flavor.o" |
        awk '$3 ~ /^R_X86_64_PC(32|64)$/ {print $1,$3,$5,$6,$7}' |
        LC_ALL=C sort >"$work/x86-pcrel-$flavor.normalized"
done
diff -u "$work/x86-pcrel-gnu.normalized" "$work/x86-pcrel-mini.normalized"
echo 'MINIAS_COMMON_X86_PCREL64=PASS GNU_DATA_RELOC_TYPE_ADDEND_AND_BINDING'
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
    # GNU GAS full sign-extension-aware 64-bit LI.D pseudo.
    li.d $r12, (((0x8000) << 48) | (1 << 0))
    li.d $r12, ((0x9000) << 48)
    li.d $r14, 0x123456789abcdef0
    li.d $r11, 0x7fffffffffffffff
    li.d $r10, 0x8000000000000000
    li.d $r9, 0xffff000000000000
    ori $r12, $r12, 0b1100
    lddir $r12, $r12, 3
    lddir $r15, $r17, 255
    ldpte $r12, 0
    ldpte $r17, 1
    ldpte $r12, 255
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
    xvinsgr2vr.d $xr31, $r17, 0
    xvinsgr2vr.d $xr8, $a1, 3
    xvinsgr2vr.w $xr9, $a2, 7
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
    xvpermi.q $xr0, $xr31, 0x2
    xvpermi.w $xr1, $xr2, 0xff
    xvpermi.d $xr3, $xr4, 0
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

for bad in            'popcntq %eax,%rax' \
           'popcntl %rax,%eax' \
           'popcntq %rax,(%rsi)' \
           'btsq $256,%rax' \
           'btq $-1,%rax' \
           'btrq $256,(%rax)' \
           'movb %ah,%r8b' \
           'movb %r9b,%ch' \
           'addb %spl,%ah' \
           'subb %ah,%sil' \
           'movq 1 bad(%rip),%rax' \
           'movsbl %rax,%eax' \
           'movsbq %ah,%rax' \
           'movswq %eax,%r9' \
           'movsbl %spl,%ah' \
           'imull $100, %rax, %eax' \
           'imulq $100, 8(%rax), %r8d' \
           'imull $100, 8(%rax), %rax' \
           'imulw $100, 8(%rax), %r8d' \
           'rep movsq %rax' \
           'movb %dh, (%r8)' \
           'movb %ah, 1(%r12)' \
           'movb (%r9), %bh' \
           'addb %ah, (%r8)' \
           'mov $5,(%rax)' \
           'cmp %rax,%eax' \
           'cmp $7,invalid_mem(%rip)' \
           'push %al' \
           'pop %eax' \
           'mov %ah,%r8b' \
           'prefetcht0 %rax' \
           'prefetcht1 $1' \
           'clflush %r8' \
           'clflushopt $4' \
           'clwb %rcx' \
           'shrdq %eax,%rdx' \
           'shldq $4,%eax,%rdx' \
           'shrdw $256,%cx,%dx' \
           'shrdq $3,%r12' \
           'prefetchw %rax' \
           'prefetchwt1 $12' \
           'mov %cr3,%eax' \
           'mov %eax,%cr3' \
           'mov %eax,%cs' \
           'mov %cr17,%rax' \
           'mov %db8,%rax' \
           'inb $256,%al' \
           'outb %al,$256' \
           'outw %eax,$15' \
           'sall $256,(%rax)' \
           'salq $1,%eax' \
           'sarl %eax,(%rbx)' \
           'shrw %r8b,(%rcx)' \
           'lgdt %rax' \
           'lidt $4' \
           'sidt %r8' \
           'invlpg %rax' \
           '.code32' \
           '.code64 bad' \
           'rdpmc %rax' \
           'fninit %rax' \
           'movq 0/0(%rax), %rdx' \
           'addb %rax, 8(%rbx)' \
           'movl $0x100000000, %eax' \
           'movb $256,%al' \
           'movb $1,%rax' \
           'xsetbv %eax' \
           'adcl %rax,%eax' \
           'negb %eax' \
           'negw %eax' \
           'divw %rax' \
           'imulb %r8w' \
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
           'movq ext_global(%rip,%rax,2), %r9' \
           'movq %gs:4294967296, %rax' \
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
           'xvpermi.q $xr0, $xr1, 256' \
           'xvpermi.d $xr0, $vr1, 2' \
           'xvpickve2gr.d $r1, $xr0, 4' \
           'xvpickve2gr.w $r1, $xr0, 8' \
           'xvpickve2gr.b $r1, $xr0, 1' \
           'xvpickve2gr.hu $r1, $xr0, 1' \
           'xvslli.b $xr0, $xr1, 8' \
           'xvreplve0.q $xr0, $vr1' \
           'vinsgr2vr.d $vr0, $r2, 2' \
           'xvinsgr2vr.d $xr0, $r2, 4' \
           'xvinsgr2vr.w $xr0, $r2, 8' \
           'xvinsgr2vr.d $vr0, $r2, 1' \
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
           'lddir $r12, $r12, 256' \
           'lddir $r12, $r12, -1' \
           'ldpte $r12, 256' \
           'ldpte $r12, -1' \
           'ori $r12, $r12, 0b1000000000000' \
           'li.d $r12, 0b2' \
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
    # Real GAS expression precedence and GNU truth=-1, not C truth=1.
.Lbool_begin:
    .byte 0x0b, 0x16
.Lbool_after:
    .byte 0x21
.Lbool_end:
    .fill -(((.Lbool_after-.Lbool_begin)-(.Lbool_end-.Lbool_after))>0),1,0x5a
    .fill -((3>=2)==-1),1,0x6c
    # Binary GAS constants must be lexed identically on x86 and Loong.
    .byte 0b1100
    .fill 0b10,1,0b10101010
    # Real Linux ALTERNATIVE: forward subsection labels and stable size.
    .subsection 0
.Loldstart:
    .byte 1,2,3,4
.Loldend:
    .fill - (((.Lnewend-.Lnewstart)-(.Loldend-.Loldstart)) > 0) * ((.Lnewend-.Lnewstart)-(.Loldend-.Loldstart)) / 4,4,0x03400000
    .skip .Loldend-.Loldstart,0x7a
    .subsection 1
.Lnewstart:
    .byte 0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88
.Lnewend:
    .subsection 0
    .byte 0x99
GNU_FILL
"$MINIAS" --target=loongarch64 -o "$work/la-fill-mini.o" "$work/gnu-fill.s"
"$LOONGAS" -mabi=lp64d -o "$work/la-fill-gnu.o" "$work/gnu-fill.s"
"$LOONGCOPY" -j .data -O binary "$work/la-fill-mini.o" "$work/la-fill-mini.bin"
"$LOONGCOPY" -j .data -O binary "$work/la-fill-gnu.o" "$work/la-fill-gnu.bin"
cmp "$work/la-fill-mini.bin" "$work/la-fill-gnu.bin"
"$MINIAS" --target=x86_64 -o "$work/x86-fill-mini.o" "$work/gnu-fill.s"
as --64 -o "$work/x86-fill-gnu.o" "$work/gnu-fill.s"
objcopy -j .data -O binary "$work/x86-fill-mini.o" "$work/x86-fill-mini.bin"
objcopy -j .data -O binary "$work/x86-fill-gnu.o" "$work/x86-fill-gnu.bin"
cmp "$work/x86-fill-mini.bin" "$work/x86-fill-gnu.bin"
# Undefined forward labels must remain errors, never fabricated as zero.
cat > "$work/invalid-forward-fill.s" <<'BAD_FILL'
.data
.fill .Lnever_defined-.Lstart,4,0
.Lstart:
.byte 1
BAD_FILL
for target in x86_64 loongarch64; do
    if "$MINIAS" --target="$target" -o "$work/invalid-fill.o" \
       "$work/invalid-forward-fill.s" >/dev/null 2>"$work/invalid-forward-fill.err"; then
        echo "unresolved deferred GAS expression incorrectly accepted target=$target" >&2
        exit 1
    fi
done
# GNU GAS named arguments are not literal macro values.  Test ordered,
# reordered and mixed positional/keyword forms on both backends.
cat >"$work/gnu-named-macro.s" <<'GAS_MACRO'
.data
.macro gm_one reg=1 docfi=0
  .if \docfi
    .byte 0xaa
  .else
    .byte 0xbb
  .endif
  .byte \reg
.endm
gm_one docfi=1,reg=7
gm_one reg=8,docfi=0
gm_one 9,docfi=1
gm_one
GAS_MACRO
"$MINIAS" --target=x86_64 -o "$work/x86-macro-mini.o" "$work/gnu-named-macro.s"
as --64 -o "$work/x86-macro-gnu.o" "$work/gnu-named-macro.s"
objcopy -j .data -O binary "$work/x86-macro-mini.o" "$work/x86-macro-mini.bin"
objcopy -j .data -O binary "$work/x86-macro-gnu.o" "$work/x86-macro-gnu.bin"
cmp "$work/x86-macro-mini.bin" "$work/x86-macro-gnu.bin"
"$MINIAS" --target=loongarch64 -mabi=lp64d -o "$work/la-macro-mini.o" "$work/gnu-named-macro.s"
"$LOONGAS" -mabi=lp64d -o "$work/la-macro-gnu.o" "$work/gnu-named-macro.s"
"$LOONGCOPY" -j .data -O binary "$work/la-macro-mini.o" "$work/la-macro-mini.bin"
"$LOONGCOPY" -j .data -O binary "$work/la-macro-gnu.o" "$work/la-macro-gnu.bin"
cmp "$work/la-macro-mini.bin" "$work/la-macro-gnu.bin"
for bad in 'gm_one docfi=1,docfi=0' 'gm_one unknown=1'; do
    printf '.data\n.macro gm_one reg=0 docfi=0\n.byte \\docfi\n.endm\n%s\n' "$bad" >"$work/gnu-macro-invalid.s"
    if "$MINIAS" --target=loongarch64 -o "$work/macro-invalid.o" \
        "$work/gnu-macro-invalid.s" >/dev/null 2>"$work/macro-invalid.err"; then
        echo "illegal GNU GAS keyword argument accepted: $bad" >&2
        exit 1
    fi
done
echo 'MINIAS_COMMON_MACRO_NAMED=PASS x86_LA_GNU_data_bytes=equal keyword_and_positional=true'
echo 'MINIAS_COMMON_DEFERRED=PASS x86_LA64_GNU_bytes=equal forward_subsection_fill_skip=true unresolved=reject'
echo 'MINIAS_COMMON_FILL=PASS GNU_LA64_data_bytes=equal'
# GNU-preprocessed C unsigned suffixes and full-uint64 .quad expressions:
# compare data bytes produced by the same source for BOTH target ELF writers.
cat > "$work/gnu-u64.s" <<'GNU_U64'
.data
    .quad (((0x9000000000200000)) & ((1ULL << 48) - 1))
    .quad ((0x8000000000000000ULL >> 48) | (255UL << 8))
    .quad (0xffffffffffffffffULL & 0x1122334455667788ULL)
GNU_U64
"$MINIAS" --target=x86_64 -o "$work/x86-u64-mini.o" "$work/gnu-u64.s"
as --64 -o "$work/x86-u64-gnu.o" "$work/gnu-u64.s"
objcopy -j .data -O binary "$work/x86-u64-mini.o" "$work/x86-u64-mini.bin"
objcopy -j .data -O binary "$work/x86-u64-gnu.o" "$work/x86-u64-gnu.bin"
cmp "$work/x86-u64-mini.bin" "$work/x86-u64-gnu.bin"
"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d -o "$work/la-u64-mini.o" "$work/gnu-u64.s"
"$LOONGAS" -mabi=lp64d -o "$work/la-u64-gnu.o" "$work/gnu-u64.s"
"$LOONGCOPY" -j .data -O binary "$work/la-u64-mini.o" "$work/la-u64-mini.bin"
"$LOONGCOPY" -j .data -O binary "$work/la-u64-gnu.o" "$work/la-u64-gnu.bin"
cmp "$work/la-u64-mini.bin" "$work/la-u64-gnu.bin"

# Independent GNU byte and relocation oracle for typed x86 GPR instructions.
cat >"$work/x86-extra-isa.s" <<'X86_EXTRA_ISA'
.text
    rdrand %eax
    rdrand %r8
    rdrand %ax
    rdseed %r11d
    rdseed %r9
    rdseed %cx
    movntil %eax,(%rdi)
    movntiq %r8,16(%rbp)
    movnti %r9d,ext_global(%rip)
    movsbw %al,%dx
    movsbw %ah,%cx
    movsbw (%rax),%dx
    movzbw %al,%bx
    movzbw (%rdi),%cx
X86_EXTRA_ISA
"$MINIAS" --target=x86_64 -o "$work/x86-extra-mini.o" "$work/x86-extra-isa.s"
as --64 -o "$work/x86-extra-gnu.o" "$work/x86-extra-isa.s"
objcopy -j .text -O binary "$work/x86-extra-mini.o" "$work/x86-extra-mini.bin"
objcopy -j .text -O binary "$work/x86-extra-gnu.o" "$work/x86-extra-gnu.bin"
cmp "$work/x86-extra-mini.bin" "$work/x86-extra-gnu.bin"
readelf -Wr "$work/x86-extra-mini.o" > "$work/x86-extra-mini-rel.txt"
readelf -Wr "$work/x86-extra-gnu.o" > "$work/x86-extra-gnu-rel.txt"
grep -q 'R_X86_64_PC32.*ext_global - 4' "$work/x86-extra-mini-rel.txt"
grep -q 'R_X86_64_PC32.*ext_global - 4' "$work/x86-extra-gnu-rel.txt"
for bad in 'rdrand %al' 'rdseedq %eax' 'rdrandq %r8' 'movntiq %eax,(%rdi)' \
           'movnti %rax,%rbx' 'movsbw %al,%rax' 'movzbw %ah,%r8w'; do
    printf '.text\n%s\n' "$bad" > "$work/x86-extra-invalid.s"
    if "$MINIAS" --target=x86_64 -o "$work/x86-extra-invalid.o" \
        "$work/x86-extra-invalid.s" >/dev/null 2>"$work/x86-extra-invalid.err"; then
        echo "invalid x86 instruction accepted: $bad" >&2
        exit 1
    fi
done
echo 'MINIAS_X86_EXTRA_ISA=PASS GNU_bytes_and_PC32 RDRAND_RDSEED_MOVNTI_MOVSX_MOVZX'

# GNU x86 register width inference and system MMU/FSBASE descriptor ISA.
cat > "$work/x86-sys-typed.s" <<'X86_SYS_TYPED'
.text
  dec %rax
  inc %rdi
  not %r10d
  div %ecx
  idiv %r9
  bts %eax,%edx
  xadd %cx,%dx
  cmpxchg %ax,%bx
  xaddw %ax,(%rbx)
  cmpxchgb %al,4(%rsp)
  btsw %ax,(%rdi)
  clc
  stc
  cmc
  movsw
  ltr %ax
  str %r9w
  lldt %cx
  verr %ax
  fxsave (%rax)
  fxsaveq 64(%rax)
  fxrstor (%rax)
  fxrstorq saved_fx(%rip)
  rdfsbase %rax
  rdgsbase %eax
  wrfsbase %r9
  wrgsbase %r10d
X86_SYS_TYPED
"$MINIAS" --target=x86_64 -o "$work/x86-sys-mini.o" "$work/x86-sys-typed.s"
as --64 -o "$work/x86-sys-gnu.o" "$work/x86-sys-typed.s"
objcopy -j .text -O binary "$work/x86-sys-mini.o" "$work/x86-sys-mini.bin"
objcopy -j .text -O binary "$work/x86-sys-gnu.o" "$work/x86-sys-gnu.bin"
cmp "$work/x86-sys-mini.bin" "$work/x86-sys-gnu.bin"
readelf -Wr "$work/x86-sys-mini.o" > "$work/x86-sys-mini-rel.txt"
readelf -Wr "$work/x86-sys-gnu.o" > "$work/x86-sys-gnu-rel.txt"
grep -q 'R_X86_64_PC32.*saved_fx - 4' "$work/x86-sys-mini-rel.txt"
grep -q 'R_X86_64_PC32.*saved_fx - 4' "$work/x86-sys-gnu-rel.txt"
for bad in 'dec %ax,%dx' 'rdfsbase %ax' 'wrgsbase %r9w' \
           'fxsaveq %rax' 'ltr %xmm0' 'movsw %eax' \
           'cmpxchgb %rax,(%rdi)' 'xaddw %rax,(%rdi)'; do
    printf '.text\n%s\n' "$bad" > "$work/x86-sys-invalid.s"
    if "$MINIAS" --target=x86_64 -o "$work/x86-sys-invalid.o" \
       "$work/x86-sys-invalid.s" >/dev/null 2>"$work/x86-sys-invalid.err"; then
       echo "GNU-invalid x86 system ISA accepted: $bad" >&2
       exit 1
    fi
done
echo 'MINIAS_X86_GNU_SYSTEM_UNARY=PASS AS_TEXT_BYTES_RELOC FSGSBASE_FXSAVE_DESCRIPTOR_WIDTH'

# Common numeric-local reference rewriting must include GNU AT&T
# $1f/$1b immediates, without rewriting digits within foo$1f symbols.
# Same source, original GAS plus full text bytes and 32S reloc parity.
cat > "$work/x86-local-immediate.s" <<'LOCAL_IMMEDIATE'
.text
  pushq $1f
  pushq $(3*8)
  pushq $(5*8+3)
  movq $1f,%rdi
  shrd $32,%rdx,%rax
  xgetbv
  vmcall
1:
  pushq $1b
  ret
LOCAL_IMMEDIATE
"$MINIAS" --target=x86_64 -o "$work/x86-local-mini.o" "$work/x86-local-immediate.s"
as --64 -o "$work/x86-local-gnu.o" "$work/x86-local-immediate.s"
objcopy -j .text -O binary "$work/x86-local-mini.o" "$work/x86-local-mini.bin"
objcopy -j .text -O binary "$work/x86-local-gnu.o" "$work/x86-local-gnu.bin"
cmp "$work/x86-local-mini.bin" "$work/x86-local-gnu.bin"
readelf -Wr "$work/x86-local-mini.o" > "$work/x86-local-mini-rel.txt"
readelf -Wr "$work/x86-local-gnu.o" > "$work/x86-local-gnu-rel.txt"
test "$(grep -c R_X86_64_32S "$work/x86-local-mini-rel.txt")" -eq 3
test "$(grep -c R_X86_64_32S "$work/x86-local-gnu-rel.txt")" -eq 3
for bad in 'pushq $9b' 'xgetbv %rax' 'shrd $32,%eax,%rax'; do
    printf '.text\n%s\n' "$bad" > "$work/x86-local-invalid.s"
    if "$MINIAS" --target=x86_64 -o "$work/x86-local-invalid.o" \
        "$work/x86-local-invalid.s" >/dev/null 2>"$work/x86-local-invalid.err"; then
        echo "invalid immediate or ISA accepted: $bad" >&2; exit 1
    fi
done
echo 'MINIAS_X86_NUMERIC_IMM=PASS GNU_32S_RELOCS local_forward_backward pushq_arithmetic shrd_xgetbv_vmcall'

# GNU GAS single-param arithmetic macro binding without a comma.
# The same Common binder must accept spaces inside an expression,
# but reject multiple separate positional arguments.
cat > "$work/gnu-one-param-expr.s" <<'GNU_ONE_MACRO'
.data
.macro one value
.rept \value
.byte 0x5a
.endr
.endm
one (32 - 16) / 4
one 2 + 1
GNU_ONE_MACRO
"$MINIAS" --target=x86_64 -o "$work/x86-one-mini.o" "$work/gnu-one-param-expr.s"
as --64 -o "$work/x86-one-gnu.o" "$work/gnu-one-param-expr.s"
objcopy -j .data -O binary "$work/x86-one-mini.o" "$work/x86-one-mini.bin"
objcopy -j .data -O binary "$work/x86-one-gnu.o" "$work/x86-one-gnu.bin"
cmp "$work/x86-one-mini.bin" "$work/x86-one-gnu.bin"
cat > "$work/gnu-one-invalid.s" <<'GNU_ONE_INVALID'
.data
.macro one value
.byte \value
.endm
one 1 2
GNU_ONE_INVALID
if "$MINIAS" --target=x86_64 -o "$work/x86-one-invalid.o" \
  "$work/gnu-one-invalid.s" >/dev/null 2>"$work/x86-one-invalid.err"; then
    echo 'GNU-invalid two positional arguments accepted for one-param macro' >&2
    exit 1
fi
echo 'MINIAS_COMMON_ONE_MACRO_EXPR=PASS GNU_GAS_BYTES arithmetic_whitespace_and_strict_arity'

# Kernel architectural control and MXCSR: real GNU GAS text bytes and
# PC32 relocation parity, including strict invalid operand rejection.
cat > "$work/x86-control.s" <<'X86_CONTROL'
.text
    iretq
    iretw
    lretq
    lret
    vmmcall
    ldmxcsr 4(%rax)
    stmxcsr ext_mxcsr(%rip)
X86_CONTROL
"$MINIAS" --target=x86_64 -o "$work/x86-control-mini.o" "$work/x86-control.s"
as --64 -o "$work/x86-control-gnu.o" "$work/x86-control.s"
objcopy -j .text -O binary "$work/x86-control-mini.o" "$work/x86-control-mini.bin"
objcopy -j .text -O binary "$work/x86-control-gnu.o" "$work/x86-control-gnu.bin"
cmp "$work/x86-control-mini.bin" "$work/x86-control-gnu.bin"
readelf -Wr "$work/x86-control-mini.o" > "$work/x86-control-mini-rel.txt"
readelf -Wr "$work/x86-control-gnu.o" > "$work/x86-control-gnu-rel.txt"
grep -q 'R_X86_64_PC32.*ext_mxcsr - 4' "$work/x86-control-mini-rel.txt"
grep -q 'R_X86_64_PC32.*ext_mxcsr - 4' "$work/x86-control-gnu-rel.txt"
for bad in 'iretq %rax' 'vmmcall %ecx' 'ldmxcsr %eax' 'stmxcsr %rdx'; do
    printf '.text\n%s\n' "$bad" > "$work/x86-control-invalid.s"
    if "$MINIAS" --target=x86_64 -o "$work/x86-control-invalid.o" \
      "$work/x86-control-invalid.s" >/dev/null 2>"$work/x86-control-invalid.err"; then
      echo "invalid kernel x86 opcode accepted: $bad" >&2
      exit 1
    fi
done
echo 'MINIAS_X86_CONTROL_ORACLE=PASS GNU_BYTES_RELOC IRET_LRET_VMMCALL_MXCSR'

# Real GNU numeric-local backward symbol difference in Common .rept
# (AArch64 Linux NOPS macro uses precisely this expression family).
cat > "$work/gnu-local-rept.s" <<'GNU_LOCAL_REPT'
.text
661:
.byte 1,2,3,4
662:
.rept (662b - 661b) / 4
.byte 0x90
.endr
GNU_LOCAL_REPT
"$MINIAS" --target=x86_64 -o "$work/local-rept-mini.o" "$work/gnu-local-rept.s"
as --64 -o "$work/local-rept-gnu.o" "$work/gnu-local-rept.s"
objcopy -j .text -O binary "$work/local-rept-mini.o" "$work/local-rept-mini.bin"
objcopy -j .text -O binary "$work/local-rept-gnu.o" "$work/local-rept-gnu.bin"
cmp "$work/local-rept-mini.bin" "$work/local-rept-gnu.bin"
echo 'MINIAS_COMMON_NUMERIC_REPT=PASS GNU_GAS_661b_662b_STABLE_BYTES'

# Native GNU GAS BMI2 shift VEX bytes: 32/64-bit and all high-register
# R/X/B/vvvv combinations. Illegal width/arity must fail closed.
cat > "$work/x86-bmi2.s" <<'BMI2_GNU'
.text
shlx %rdx, %rax, %rsi
shlx %r10, %r8, %r9
shlxl %edx, %eax, %esi
shrx %r9, %r12, %r13
sarxl %ecx, %edx, %ebx
shrx %ecx, %edx, %eax
BMI2_GNU
"$MINIAS" --target=x86_64 -o "$work/bmi2-mini.o" "$work/x86-bmi2.s"
as --64 -o "$work/bmi2-gnu.o" "$work/x86-bmi2.s"
objcopy -j .text -O binary "$work/bmi2-mini.o" "$work/bmi2-mini.bin"
objcopy -j .text -O binary "$work/bmi2-gnu.o" "$work/bmi2-gnu.bin"
cmp "$work/bmi2-mini.bin" "$work/bmi2-gnu.bin"
for bad in 'shlx %edx,%rax,%rsi' 'sarx %ax,%rax,%rsi' \
           'shrx %r9,%r12' 'shlxl %r10,%r8,%r9'; do
    printf '.text\n%s\n' "$bad" > "$work/bmi2-invalid.s"
    if "$MINIAS" --target=x86_64 -o "$work/bmi2-invalid.o" \
        "$work/bmi2-invalid.s" >/dev/null 2>"$work/bmi2-invalid.err"; then
        echo "invalid x86 BMI2 operand accepted: $bad" >&2
        exit 1
    fi
done
echo 'MINIAS_X86_BMI2_GNU=PASS VEX_3REG_32_64_ALL_EXTENDED strict_invalid'

# F3 0F BC/BD typed bit-scan counts, GNU register and memory bytes,
# REX.W/high registers, RIP-relative relocation and strict invalid width.
cat > "$work/x86-cnt.s" <<'CNT_GNU'
.text
lzcntl %eax,%ecx
lzcntq %r12,%r9
tzcntl %esi,%r10d
tzcntq 16(%rsp),%r8
lzcntl ext_lz(%rip),%eax
CNT_GNU
"$MINIAS" --target=x86_64 -o "$work/cnt-mini.o" "$work/x86-cnt.s"
as --64 -o "$work/cnt-gnu.o" "$work/x86-cnt.s"
objcopy -j .text -O binary "$work/cnt-mini.o" "$work/cnt-mini.bin"
objcopy -j .text -O binary "$work/cnt-gnu.o" "$work/cnt-gnu.bin"
cmp "$work/cnt-mini.bin" "$work/cnt-gnu.bin"
readelf -Wr "$work/cnt-mini.o" > "$work/cnt-mini-rel.txt"
readelf -Wr "$work/cnt-gnu.o" > "$work/cnt-gnu-rel.txt"
grep -q 'R_X86_64_PC32.*ext_lz - 4' "$work/cnt-mini-rel.txt"
grep -q 'R_X86_64_PC32.*ext_lz - 4' "$work/cnt-gnu-rel.txt"
for bad in 'lzcntl %rax,%eax' 'tzcntq %eax,%rdi' 'lzcntq %eax,(%rsp)'; do
    printf '.text\n%s\n' "$bad" > "$work/cnt-invalid.s"
    if "$MINIAS" --target=x86_64 -o "$work/cnt-invalid.o" \
       "$work/cnt-invalid.s" >/dev/null 2>"$work/cnt-invalid.err"; then
       echo "GNU-invalid LZCNT/TZCNT accepted: $bad" >&2
       exit 1
    fi
done
echo 'MINIAS_X86_CNT=PASS GNU_BYTES_PCREL LZCNT_TZCNT_32_64'

# GNU code-relative encoding tables with forward same-section S1-S2 / 4.
# This arithmetic belongs in Common data evaluation, not any Target ISA.
cat > "$work/gnu-pair-div.s" <<'PAIR_DIV'
.section .data
.byte (.Lafter-.Lstart)/4
.2byte (.Lafter-.Lstart)/4
.long (.Lafter-.Lstart)/4
.quad (.Lafter-.Lstart)/4
.Lstart:
.byte 0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x11,0x22
.Lafter:
.byte (.Lafter-.Lstart)/4
PAIR_DIV
for target in x86_64 loongarch64; do
    "$MINIAS" --target="$target" -o "$work/$target-pair-mini.o" "$work/gnu-pair-div.s"
    if [ "$target" = x86_64 ]; then
       as --64 -o "$work/$target-pair-gnu.o" "$work/gnu-pair-div.s"
       objcopy -j .data -O binary "$work/$target-pair-mini.o" "$work/$target-pair-mini.bin"
       objcopy -j .data -O binary "$work/$target-pair-gnu.o" "$work/$target-pair-gnu.bin"
    else
       "$LOONGAS" -mabi=lp64d -o "$work/$target-pair-gnu.o" "$work/gnu-pair-div.s"
       "$LOONGCOPY" -j .data -O binary "$work/$target-pair-mini.o" "$work/$target-pair-mini.bin"
       "$LOONGCOPY" -j .data -O binary "$work/$target-pair-gnu.o" "$work/$target-pair-gnu.bin"
    fi
    cmp "$work/$target-pair-mini.bin" "$work/$target-pair-gnu.bin"
done
echo 'MINIAS_COMMON_PAIR_DIV=PASS GNU_forward_symbol_difference_div4_8_16_32_64'

# Boolean grammar must match GNU GAS: logical ||/&&/! produce 1 or 0,
# unlike integer comparisons (GNU encodes their true value as -1).
cat > "$work/common-bool.s" <<'COMMON_BOOL'
.data
.long 0||3, 1&&3, 3||4, 0&&1, !0, !9, 1==1
.if ((1==2) || (3==3))
.byte 0x77
.else
.byte 0x55
.endif
.if (!0 && (2!=3))
.byte 0x44
.else
.byte 0x55
.endif
COMMON_BOOL
"$MINIAS" --target=x86_64 -o "$work/common-bool-mini.o" "$work/common-bool.s"
as --64 -o "$work/common-bool-gnu.o" "$work/common-bool.s"
objcopy -j .data -O binary "$work/common-bool-mini.o" "$work/common-bool-mini.bin"
objcopy -j .data -O binary "$work/common-bool-gnu.o" "$work/common-bool-gnu.bin"
cmp "$work/common-bool-mini.bin" "$work/common-bool-gnu.bin"
echo 'MINIAS_COMMON_LOGICAL=PASS GNU_GAS_boolean_precedence_AND_OR_NOT_true_is_1'

# GNU AT&T REP family shares exact target string opcode descriptors;
# verify canonical prefix ordering 66/F3 and F3/REX.W on each form.
cat > "$work/x86-rep-matrix.s" <<'REP_MATRIX'
.text
  rep movsb
  rep movsw
  rep movsl
  rep movsq
  rep stosb
  rep stosw
  rep stosq
  repe cmpsb
  repe cmpsw
  repe cmpsq
  repne scasb
  repne scasw
  repne scasq
  repnz cmpsw
  rep outsl
  rep insb
  rep lodsq
REP_MATRIX
"$MINIAS" --target=x86_64 -o "$work/rep-mini.o" "$work/x86-rep-matrix.s"
as --64 -o "$work/rep-gnu.o" "$work/x86-rep-matrix.s"
objcopy -j .text -O binary "$work/rep-mini.o" "$work/rep-mini.bin"
objcopy -j .text -O binary "$work/rep-gnu.o" "$work/rep-gnu.bin"
cmp "$work/rep-mini.bin" "$work/rep-gnu.bin"
for bad in 'rep rdtsc' 'repe movq %rax,%rbx' 'rep bad_instruction'; do
  printf '.text\n%s\n' "$bad" >"$work/rep-bad.s"
  if "$MINIAS" --target=x86_64 -o "$work/rep-bad.o" "$work/rep-bad.s" >/dev/null 2>&1; then
    echo "MiniAS incorrectly accepted REP operand $bad" >&2
    exit 1
  fi
done
echo 'MINIAS_X86_REP=PASS GNU_GAS_BYTES rep_repz_repe_repne_repnz canonical_66_REX_prefix'
# GNU function-like spellings are Common directive lexical forms,
# not independent target assembler implementations.
cat > "$work/common-glued.s" <<'COMMON_GLUED'
.data
.long(3 + 4)
.byte(2)
.quad(0x11223344)
COMMON_GLUED
"$MINIAS" --target=x86_64 -o "$work/glued-mini.o" "$work/common-glued.s"
as --64 -o "$work/glued-gnu.o" "$work/common-glued.s"
objcopy -j .data -O binary "$work/glued-mini.o" "$work/glued-mini.bin"
objcopy -j .data -O binary "$work/glued-gnu.o" "$work/glued-gnu.bin"
cmp "$work/glued-mini.bin" "$work/glued-gnu.bin"
echo 'MINIAS_COMMON_GLUED_DATA=PASS GNU_GAS_directive_parenthesized'

# GNU REP BSF/BSR are target-level TZCNT/LZCNT aliases, not string
# prefixes. Also compare x87 memory /digit and cmpxchg16b PC32 bytes.
cat > "$work/x86-sys-batch.s" <<'X86_SYS_BATCH'
.text
rep bsfq %rcx,%rcx
rep bsrq %rax,%r9
rep bsfl %ecx,%edx
rep bsfq 8(%rsp),%rax
fnstsw 4(%rsp)
fnstcw %gs:4(%rbp)
fldcw (%rbp)
fnclex
cmpxchg8b (%rax)
cmpxchg16b (%rax)
cmpxchg16b ext_16b(%rip)
X86_SYS_BATCH
"$MINIAS" --target=x86_64 -o "$work/x86-sys-mini.o" "$work/x86-sys-batch.s"
as --64 -o "$work/x86-sys-gnu.o" "$work/x86-sys-batch.s"
objcopy -j .text -O binary "$work/x86-sys-mini.o" "$work/x86-sys-mini.bin"
objcopy -j .text -O binary "$work/x86-sys-gnu.o" "$work/x86-sys-gnu.bin"
cmp "$work/x86-sys-mini.bin" "$work/x86-sys-gnu.bin"
readelf -Wr "$work/x86-sys-mini.o" > "$work/x86-sys-mini-rel.txt"
readelf -Wr "$work/x86-sys-gnu.o" > "$work/x86-sys-gnu-rel.txt"
grep -q 'R_X86_64_PC32.*ext_16b - 4' "$work/x86-sys-mini-rel.txt"
grep -q 'R_X86_64_PC32.*ext_16b - 4' "$work/x86-sys-gnu-rel.txt"
for bad in 'rep bsfq' 'fnstsw %rax' 'cmpxchg16b %rax'; do
    printf '.text\n%s\n' "$bad" > "$work/x86-sys-bad.s"
    if "$MINIAS" --target=x86_64 -o "$work/x86-sys-bad.o" "$work/x86-sys-bad.s" >/dev/null 2>&1; then
        echo "invalid GNU x86 system operand accepted: $bad" >&2
        exit 1
    fi
done
echo 'MINIAS_X86_GROUPED_SYS=PASS GNU_REP_BSF_BSR_x87_CMPEXCHG16B_PCREL'

# Real GNU SSE/XMM memory widths, 0F38 escape, REX and PC32 relocation.
cat > "$work/xmm-memory.s" <<'XMM_MEMORY'
.text
movntdqa (%rbx), %xmm0
movntdqa ext_simd(%rip), %xmm9
movntdq %xmm12, 16(%rsp)
movdqa 32(%r8), %xmm8
movdqa %xmm1, 16(%rax)
movdqu (%r15), %xmm0
movdqu %xmm15, (%rbp)
movaps (%rdi), %xmm3
movaps %xmm4, (%r12)
movups 16(%r12,%r13,2), %xmm5
movups %xmm6, ext_movups(%rip)
XMM_MEMORY
"$MINIAS" --target=x86_64 -o "$work/xmm-mini.o" "$work/xmm-memory.s"
as --64 -o "$work/xmm-gnu.o" "$work/xmm-memory.s"
objcopy -j .text -O binary "$work/xmm-mini.o" "$work/xmm-mini.bin"
objcopy -j .text -O binary "$work/xmm-gnu.o" "$work/xmm-gnu.bin"
cmp "$work/xmm-mini.bin" "$work/xmm-gnu.bin"
readelf -Wr "$work/xmm-mini.o" > "$work/xmm-mini-rel.txt"
readelf -Wr "$work/xmm-gnu.o" > "$work/xmm-gnu-rel.txt"
grep -q 'R_X86_64_PC32.*ext_simd - 4' "$work/xmm-mini-rel.txt"
grep -q 'R_X86_64_PC32.*ext_simd - 4' "$work/xmm-gnu-rel.txt"
for bad in 'movntdqa %xmm0,(%rbx)' 'movntdq (%rbx),%xmm0' \
           'movdqa %rax,(%rbx)' 'movups %xmm16,(%rsp)'; do
    printf '.text\n%s\n' "$bad" > "$work/xmm-invalid.s"
    if "$MINIAS" --target=x86_64 -o "$work/xmm-invalid.o" "$work/xmm-invalid.s" >/dev/null 2>&1; then
        echo "invalid XMM memory instruction accepted: $bad" >&2; exit 1
    fi
done
echo 'MINIAS_X86_XMM=PASS GNU_BYTES_PC32 MOVNTDQA_MOVDQA_MOVDQU_MOVAPS_MOVUPS'

# Real GNU BMI2 memory operand VEX.R/X/B/W/vvvv and PC32 relocation.
# The exact Linux SHLX/SHRX frontier cases use the same typed memory
# and fixup path as MOVNTDQA and ordinary x86 ModR/M instructions.
cat > "$work/x86-bmi2-memory.s" <<'BMI2_MEM_GNU'
.text
shlx %rsi, 8(%rsp), %rax
shlx %r14, -168(%rbp), %r12
shrx %esi, (%rcx), %eax
sarx %r9, long_symbol(%rip), %r13
shlxl %ecx, (%r15,%r13,4), %r8d
shrxq %rsi, 32(%rax), %rdi
BMI2_MEM_GNU
"$MINIAS" --target=x86_64 -o "$work/bmi2m-mini.o" "$work/x86-bmi2-memory.s"
as --64 -o "$work/bmi2m-gnu.o" "$work/x86-bmi2-memory.s"
objcopy -j .text -O binary "$work/bmi2m-mini.o" "$work/bmi2m-mini.bin"
objcopy -j .text -O binary "$work/bmi2m-gnu.o" "$work/bmi2m-gnu.bin"
cmp "$work/bmi2m-mini.bin" "$work/bmi2m-gnu.bin"
readelf -Wr "$work/bmi2m-mini.o" > "$work/bmi2m-mini-rel.txt"
readelf -Wr "$work/bmi2m-gnu.o" > "$work/bmi2m-gnu-rel.txt"
grep -q 'R_X86_64_PC32.*long_symbol - 4' "$work/bmi2m-mini-rel.txt"
grep -q 'R_X86_64_PC32.*long_symbol - 4' "$work/bmi2m-gnu-rel.txt"
for bad in 'shlx %eax,(%rax),%rdi' 'sarx %r9,(%rax),%r9d' \
           'shrxq %ecx,(%rsp),%rax' 'shlxl %r9,(%rsp),%eax'; do
    printf '.text\n%s\n' "$bad" >"$work/bmi2m-bad.s"
    if "$MINIAS" --target=x86_64 -o "$work/bmi2m-bad.o" "$work/bmi2m-bad.s" >/dev/null 2>&1; then
      echo "GNU-invalid BMI2 mixed-width memory operand accepted: $bad" >&2
      exit 1
    fi
done
echo 'MINIAS_X86_BMI2_MEM=PASS GNU_VEX_MEM_PCREL_R_X_B_W_VVVV'
# GNU Common expression, textual .ifc and symbolic data relocation:
# (1) full-width unsigned constant masked back into a small .set,
# (2) empty operands, quoted commas and literal quote preservation,
# (3) nested parentheses around relocatable symbol/addend.
# Do not delegate expression/layout semantics to individual ISA encoders.
cat > "$work/common-gas-text-data.s" <<'COMMON_GAS_TEXT'
.data
.set high_index, (((0xffff888000000000) >> 39) & 511)
.quad high_index
.quad (preempt_schedule_thunk)
.quad ((preempt_schedule_thunk+16))
.ifc ,
.byte 0x11
.else
.byte 0xee
.endif
.ifnc a,
.byte 0x22
.else
.byte 0xee
.endif
.ifc "a,b","a,b"
.byte 0xee
.else
.byte 0x33
.endif
.ifc "a",a
.byte 0xee
.else
.byte 0x44
.endif
.ifc a,b,c
.byte 0xee
.else
.byte 0x55
.endif
COMMON_GAS_TEXT
for target in x86_64 loongarch64; do
    "$MINIAS" --target="$target" -o "$work/$target-common-gas-mini.o" "$work/common-gas-text-data.s"
    if [ "$target" = x86_64 ]; then
        as --64 -o "$work/$target-common-gas-gnu.o" "$work/common-gas-text-data.s"
        objcopy -j .data -O binary "$work/$target-common-gas-mini.o" "$work/$target-common-gas-mini.bin"
        objcopy -j .data -O binary "$work/$target-common-gas-gnu.o" "$work/$target-common-gas-gnu.bin"
    else
        "$LOONGAS" -mabi=lp64d -o "$work/$target-common-gas-gnu.o" "$work/common-gas-text-data.s"
        "$LOONGCOPY" -j .data -O binary "$work/$target-common-gas-mini.o" "$work/$target-common-gas-mini.bin"
        "$LOONGCOPY" -j .data -O binary "$work/$target-common-gas-gnu.o" "$work/$target-common-gas-gnu.bin"
    fi
    cmp "$work/$target-common-gas-mini.bin" "$work/$target-common-gas-gnu.bin"
done
readelf -Wr "$work/x86_64-common-gas-mini.o" > "$work/common-gas-mini-rel.txt"
readelf -Wr "$work/x86_64-common-gas-gnu.o" > "$work/common-gas-gnu-rel.txt"
test "$(grep -c 'R_X86_64_64.*preempt_schedule_thunk' "$work/common-gas-mini-rel.txt")" -eq 2
test "$(grep -c 'R_X86_64_64.*preempt_schedule_thunk' "$work/common-gas-gnu-rel.txt")" -eq 2
grep -q 'preempt_schedule_thunk + 10' "$work/common-gas-mini-rel.txt"
grep -q 'preempt_schedule_thunk + 10' "$work/common-gas-gnu-rel.txt"
cat > "$work/common-gas-invalid.s" <<'COMMON_GAS_INVALID'
.data
.ifc a
.byte 1
.endif
COMMON_GAS_INVALID
if "$MINIAS" --target=x86_64 -o "$work/common-gas-invalid.o" "$work/common-gas-invalid.s" >/dev/null 2>&1; then
    echo 'invalid GNU .ifc without delimiter accepted' >&2; exit 1
fi
echo 'MINIAS_COMMON_GAS_TEXT=PASS GNU_x86_LA_bytes .ifc_empty_quoted_comma unsigned_set parenthesized_relocations'

# GNU x86 system selector grammar: register aliases and 66/REX order,
# plus LAR/LSL selector loads both register and memory forms. Compare
# exact 0F00/0F02/0F03 bytes and PC32 relocation against real GNU AS.
cat > "$work/x86-descriptor-isa.s" <<'X86_DESCRIPTOR_ISA'
.text
str %ax
str %eax
str %rax
str %r8w
str %r8d
ltr %ax
ltr %rax
ltr %r9w
sldt %r8w
lldt %r8w
verr %r8w
verw %r8w
lsl %ax,%eax
lsl %rax,%rax
lsl %r8w,%r9w
lsl %r8d,%r9d
lsl (%rax),%r9
lar %r8w,%r9d
lar %r8d,%r9d
lar (%rax),%r9w
lar ext_selector(%rip),%ecx
emms
X86_DESCRIPTOR_ISA
"$MINIAS" --target=x86_64 -o "$work/descriptor-mini.o" "$work/x86-descriptor-isa.s"
as --64 -o "$work/descriptor-gnu.o" "$work/x86-descriptor-isa.s"
objcopy -j .text -O binary "$work/descriptor-mini.o" "$work/descriptor-mini.bin"
objcopy -j .text -O binary "$work/descriptor-gnu.o" "$work/descriptor-gnu.bin"
cmp "$work/descriptor-mini.bin" "$work/descriptor-gnu.bin"
readelf -Wr "$work/descriptor-mini.o" > "$work/descriptor-mini-rel.txt"
readelf -Wr "$work/descriptor-gnu.o" > "$work/descriptor-gnu-rel.txt"
grep -q 'R_X86_64_PC32.*ext_selector - 4' "$work/descriptor-mini-rel.txt"
grep -q 'R_X86_64_PC32.*ext_selector - 4' "$work/descriptor-gnu-rel.txt"
for bad in 'lsl %eax,%r9w' 'lar %eax,%r9w' \
           'str %xmm0' 'lsl %xmm1,%rax' 'emms %eax'; do
  printf '.text\n%s\n' "$bad" >"$work/descriptor-invalid.s"
  if "$MINIAS" --target=x86_64 -o "$work/descriptor-invalid.o" "$work/descriptor-invalid.s" >/dev/null 2>&1; then
    echo "invalid descriptor ISA operand accepted: $bad" >&2;exit 1
  fi
done
echo 'MINIAS_X86_DESCRIPTOR=PASS GNU_0F00_0F02_0F03_and_PC32'
# GNU GAS .if compares register *identities* including width,
# while Common must preserve its target-neutral symbolic arithmetic.
cat > "$work/x86-register-if.s" <<'X86_REGISTER_IF'
.text
.if %rsp == %rsp
.byte 0x11
.else
.byte 0xff
.endif
.if %rax != %eax
.byte 0x22
.else
.byte 0xff
.endif
.if %rax == %r8
.byte 0xff
.else
.byte 0x33
.endif
.if %ah == %al
.byte 0xff
.else
.byte 0x44
.endif
.if %r8 == %r8d
.byte 0xff
.else
.byte 0x55
.endif
.if %xmm8 == %xmm8
.byte 0x66
.else
.byte 0xff
.endif
X86_REGISTER_IF
"$MINIAS" --target=x86_64 -o "$work/x86-register-if-mini.o" "$work/x86-register-if.s"
as --64 -o "$work/x86-register-if-gnu.o" "$work/x86-register-if.s"
objcopy -j .text -O binary "$work/x86-register-if-mini.o" "$work/x86-register-if-mini.bin"
objcopy -j .text -O binary "$work/x86-register-if-gnu.o" "$work/x86-register-if-gnu.bin"
cmp "$work/x86-register-if-mini.bin" "$work/x86-register-if-gnu.bin"
for bad in '.if %bogus == %bogus' '.if %rax < %rbx' '.if %rax == fake'; do
    printf '.text\n%s\n.byte 1\n.endif\n' "$bad" > "$work/x86-register-if-invalid.s"
    if "$MINIAS" --target=x86_64 -o "$work/x86-register-if-invalid.o" "$work/x86-register-if-invalid.s" >/dev/null 2>&1; then
        echo "invalid GNU x86 register conditional accepted: $bad" >&2; exit 1
    fi
done
echo 'MINIAS_X86_REG_IF=PASS GNU_GAS_register_identity_and_invalid_reject'

# x86 GNU Kbuild displacement grammar: arithmetic before an
# addressing tuple, constant-first symbolic addends, and parenthesized
# symbol subtraction all share one target memory decoder. The normal
# semantic 32S/PC32 ELF Fixup route must remain authoritative.
cat > "$work/x86-expression-memory.s" <<'X86_EXPRESSION_MEM'
.text
testb $1, 29+pvclock_page(%rip)
cmpb $0, 16+steal_time(%rax)
movq (1 << 3)(%rsi), %r11
leaq (__end_init_task - 168)(%rip), %rsp
movq (2+6)(%rsi), %rax
movl 29+pvclock_page(%rip), %eax
movq -168+__end_init_task(%rip), %rax
movq __end_init_task-168(%rip), %rax
X86_EXPRESSION_MEM
"$MINIAS" --target=x86_64 -o "$work/x86-expression-memory-mini.o" "$work/x86-expression-memory.s"
as --64 -o "$work/x86-expression-memory-gnu.o" "$work/x86-expression-memory.s"
objcopy -j .text -O binary "$work/x86-expression-memory-mini.o" "$work/x86-expression-memory-mini.bin"
objcopy -j .text -O binary "$work/x86-expression-memory-gnu.o" "$work/x86-expression-memory-gnu.bin"
cmp "$work/x86-expression-memory-mini.bin" "$work/x86-expression-memory-gnu.bin"
readelf -Wr "$work/x86-expression-memory-mini.o" > "$work/x86-expression-memory-mini-rel.txt"
readelf -Wr "$work/x86-expression-memory-gnu.o" > "$work/x86-expression-memory-gnu-rel.txt"
for family in mini gnu; do
    test "$(grep -c 'R_X86_64_PC32.*pvclock_page' "$work/x86-expression-memory-$family-rel.txt")" -eq 2
    test "$(grep -c 'R_X86_64_32S.*steal_time' "$work/x86-expression-memory-$family-rel.txt")" -eq 1
    test "$(grep -c 'R_X86_64_PC32.*__end_init_task' "$work/x86-expression-memory-$family-rel.txt")" -eq 3
    grep -q 'R_X86_64_32S.*steal_time + 10' "$work/x86-expression-memory-$family-rel.txt"
    grep -q 'R_X86_64_PC32.*__end_init_task - ac' "$work/x86-expression-memory-$family-rel.txt"
done
for bad in 'movq (1+2)(%unknown),%rax' 'movq (foo+bar)(%rax),%rcx' \
           'movq 999999999999999999999+sym(%rip),%rax'; do
    printf '.text\n%s\n' "$bad" > "$work/x86-expression-memory-invalid.s"
    if "$MINIAS" --target=x86_64 -o "$work/x86-expression-memory-invalid.o" "$work/x86-expression-memory-invalid.s" >/dev/null 2>&1; then
        echo "invalid symbolic displacement accepted: $bad" >&2;exit 1
    fi
done
echo 'MINIAS_X86_EXPRESSION_MEMORY=PASS GNU_GAS_bytes_PCREL32_ABS32S_nested_expression'

# GNU Common conditional state machine, including nested .elseif
# and skipped invalid expressions after earlier branch selection.
cat > "$work/gnu-elseif.s" <<'GNU_ELSEIF'
.data
.if 0
.byte 0xff
.elseif 0
.byte 0xff
.elseif 1
.byte 0x11
.elseif never_defined_symbol
.byte 0xff
.else
.byte 0xff
.endif
.if 1
  .if 0
  .byte 0xff
  .elseif 2==2
  .byte 0x22
  .else
  .byte 0xff
  .endif
.elseif 0
.byte 0xff
.endif
.if 0
  .if missing_symbol
  .byte 0xff
  .endif
.elseif 1
.byte 0x33
.endif
GNU_ELSEIF
for target in x86_64 loongarch64; do
    "$MINIAS" --target="$target" -o "$work/$target-elseif-mini.o" "$work/gnu-elseif.s"
    if [ "$target" = x86_64 ]; then
        as --64 -o "$work/$target-elseif-gnu.o" "$work/gnu-elseif.s"
        objcopy -j .data -O binary "$work/$target-elseif-mini.o" "$work/$target-elseif-mini.bin"
        objcopy -j .data -O binary "$work/$target-elseif-gnu.o" "$work/$target-elseif-gnu.bin"
    else
        "$LOONGAS" -mabi=lp64d -o "$work/$target-elseif-gnu.o" "$work/gnu-elseif.s"
        "$LOONGCOPY" -j .data -O binary "$work/$target-elseif-mini.o" "$work/$target-elseif-mini.bin"
        "$LOONGCOPY" -j .data -O binary "$work/$target-elseif-gnu.o" "$work/$target-elseif-gnu.bin"
    fi
    cmp "$work/$target-elseif-mini.bin" "$work/$target-elseif-gnu.bin"
done
for bad in '.elseif 1' '.if 1
.else
.elseif 1
.endif'; do
    printf '.data\n%s\n' "$bad" > "$work/gnu-elseif-bad.s"
    if "$MINIAS" --target=x86_64 -o "$work/gnu-elseif-bad.o" "$work/gnu-elseif-bad.s" >/dev/null 2>&1; then
        echo "illegal .elseif accepted" >&2;exit 1
    fi
done
echo 'MINIAS_COMMON_ELSEIF=PASS GNU_x86_Loong_bytes nested_chains_skip_inactive'

cat > "$work/x86-waitpkg-movabs.s" <<'GNU_WAITPKG'
.text
tpause %ecx
tpause %r9d
umwait %eax
umwait %r8d
umonitor %rax
umonitor %r9
movabs $0x0101010101010101,%rax
movabs $0x8000000000000000,%r15
movabs $external_wait_symbol,%r8
GNU_WAITPKG
"$MINIAS" --target=x86_64 -o "$work/waitpkg-mini.o" "$work/x86-waitpkg-movabs.s"
as --64 -o "$work/waitpkg-gnu.o" "$work/x86-waitpkg-movabs.s"
objcopy -j .text -O binary "$work/waitpkg-mini.o" "$work/waitpkg-mini.bin"
objcopy -j .text -O binary "$work/waitpkg-gnu.o" "$work/waitpkg-gnu.bin"
cmp "$work/waitpkg-mini.bin" "$work/waitpkg-gnu.bin"
readelf -Wr "$work/waitpkg-mini.o" > "$work/waitpkg-mini-rel.txt"
readelf -Wr "$work/waitpkg-gnu.o" > "$work/waitpkg-gnu-rel.txt"
grep -q 'R_X86_64_64.*external_wait_symbol' "$work/waitpkg-mini-rel.txt"
grep -q 'R_X86_64_64.*external_wait_symbol' "$work/waitpkg-gnu-rel.txt"
for bad in 'tpause %rax' 'tpause %ax' 'umwait %rcx' 'umonitor %ecx' 'movabs $3,%eax'; do
    printf '.text\n%s\n' "$bad" > "$work/waitpkg-invalid.s"
    if "$MINIAS" --target=x86_64 -o "$work/waitpkg-invalid.o" "$work/waitpkg-invalid.s" >/dev/null 2>&1; then
        echo "bad x86 WAITPKG/MOVABS width accepted: $bad" >&2; exit 1
    fi
done
echo 'MINIAS_X86_WAITPKG_MOVABS=PASS GNU_bytes ABS64 WAITPKG_REX register_widths'
# GNU .rep is .rept, including nested mixed .rep/.rept.
# Check actual source expansion on two targets, not a no-op directive.
cat > "$work/gnu-rep-nested.s" <<'GNU_REP_NESTED'
.data
.rep 3
.byte 0x19
.rept 2
.byte 0x27
.endr
.endr
.rept 2
.rep 2
.byte 0x3a
.endr
.endr
GNU_REP_NESTED
for target in x86_64 loongarch64; do
    "$MINIAS" --target="$target" -o "$work/$target-rep-nested-mini.o" "$work/gnu-rep-nested.s"
    if [ "$target" = x86_64 ]; then
        as --64 -o "$work/$target-rep-nested-gnu.o" "$work/gnu-rep-nested.s"
        objcopy -j .data -O binary "$work/$target-rep-nested-mini.o" "$work/$target-rep-nested-mini.bin"
        objcopy -j .data -O binary "$work/$target-rep-nested-gnu.o" "$work/$target-rep-nested-gnu.bin"
    else
        "$LOONGAS" -mabi=lp64d -o "$work/$target-rep-nested-gnu.o" "$work/gnu-rep-nested.s"
        "$LOONGCOPY" -j .data -O binary "$work/$target-rep-nested-mini.o" "$work/$target-rep-nested-mini.bin"
        "$LOONGCOPY" -j .data -O binary "$work/$target-rep-nested-gnu.o" "$work/$target-rep-nested-gnu.bin"
    fi
    cmp "$work/$target-rep-nested-mini.bin" "$work/$target-rep-nested-gnu.bin"
done
echo 'MINIAS_COMMON_GNU_REP=PASS GNU_AS_x86_Loong_nested_repeat_bytes'

cat > "$work/x86-sysret.s" <<'GNU_SYSRET'
.text
sysretl
sysretq
sysret
GNU_SYSRET
"$MINIAS" --target=x86_64 -o "$work/sysret-mini.o" "$work/x86-sysret.s"
as --64 -o "$work/sysret-gnu.o" "$work/x86-sysret.s"
objcopy -j .text -O binary "$work/sysret-mini.o" "$work/sysret-mini.bin"
objcopy -j .text -O binary "$work/sysret-gnu.o" "$work/sysret-gnu.bin"
cmp "$work/sysret-mini.bin" "$work/sysret-gnu.bin"
for bad in 'sysretq %rax' 'sysretl $1'; do
    printf '.text\n%s\n' "$bad" > "$work/sysret-bad.s"
    if "$MINIAS" --target=x86_64 -o "$work/sysret-bad.o" "$work/sysret-bad.s" >/dev/null 2>&1; then
        echo "invalid SYSRET operands accepted: $bad" >&2;exit 1
    fi
done
echo 'MINIAS_X86_SYSRET=PASS GNU_bytes long_mode_32_64_aliases'
# GNU kernel INVPCID 66/0F38 and x87 scalar loads/stores:
# identical target ModRM/SIB/ELF fixup semantics, with full bytes and
# PC32 relocation addend independently compared to real GNU GAS.
cat > "$work/x86-kernel-memory-isa.s" <<'KERNEL_MEM_GNU'
.text
invpcid (%rsp), %rax
invpcid 8(%r12), %r13
invpcid ext_inv(%rip), %r9
fldl x(%rip)
fildl y(%rip)
fstpl 16(%rsp)
fistpl 24(%rsp)
KERNEL_MEM_GNU
"$MINIAS" --target=x86_64 -o "$work/x86-kernel-memory-mini.o" "$work/x86-kernel-memory-isa.s"
as --64 -o "$work/x86-kernel-memory-gnu.o" "$work/x86-kernel-memory-isa.s"
objcopy -j .text -O binary "$work/x86-kernel-memory-mini.o" "$work/x86-kernel-memory-mini.bin"
objcopy -j .text -O binary "$work/x86-kernel-memory-gnu.o" "$work/x86-kernel-memory-gnu.bin"
cmp "$work/x86-kernel-memory-mini.bin" "$work/x86-kernel-memory-gnu.bin"
for mode in mini gnu; do
    readelf -Wr "$work/x86-kernel-memory-$mode.o" > "$work/x86-kernel-memory-$mode-rel.txt"
    for symbol in ext_inv x y; do
       grep -q "R_X86_64_PC32.*$symbol - 4" "$work/x86-kernel-memory-$mode-rel.txt"
    done
done
for bad in 'invpcid %rax,%rdi' 'invpcid (%rsp),%eax' \
           'fldl %rax' 'fildl $1' 'fstpl %rsp' 'fistpl %rax'; do
    printf '.text\n%s\n' "$bad" > "$work/x86-kernel-memory-invalid.s"
    if "$MINIAS" --target=x86_64 -o "$work/x86-kernel-memory-invalid.o" "$work/x86-kernel-memory-invalid.s" >/dev/null 2>&1; then
        echo "bad x86 kernel memory ISA accepted: $bad" >&2;exit 1
    fi
done
echo 'MINIAS_X86_KERNEL_MEMORY=PASS GNU_INVPCID_0F38_x87_bytes_PCREL'

# Linux BMI1/BMI2 three-register VEX reg forms and MOVW imm16 must
# exactly equal GNU GAS machine code. Cover W, R, B and complemented
# VEX.vvvv fields, not merely candidate assembly status.
cat > "$work/x86-bmi-movw.s" <<'X86_BMI_MOVW'
.text
andn %rdi,%rax,%rax
andn %r11,%r13,%r13
andnl %edi,%eax,%eax
andnq %r9,%r12,%r10
bzhi %r11,%r13,%r13
bzhi %rdi,%rax,%rax
bzhil %edi,%eax,%eax
bzhiq %r9,%r12,%r10
movw $(3*8),%ax
movw $0x5658,%dx
movw $-1,%r8w
movw $65535,%r15w
X86_BMI_MOVW
"$MINIAS" --target=x86_64 -o "$work/x86-bmi-movw-mini.o" "$work/x86-bmi-movw.s"
as --64 -o "$work/x86-bmi-movw-gnu.o" "$work/x86-bmi-movw.s"
objcopy -j .text -O binary "$work/x86-bmi-movw-mini.o" "$work/x86-bmi-movw-mini.bin"
objcopy -j .text -O binary "$work/x86-bmi-movw-gnu.o" "$work/x86-bmi-movw-gnu.bin"
cmp "$work/x86-bmi-movw-mini.bin" "$work/x86-bmi-movw-gnu.bin"
for bad in 'andn %rdi,%eax,%rax' 'bzhi %rdi,%eax,%r9' \
           'movw $65536,%ax' 'movw $4,%eax' \
           'andn %rax,%rbx' 'bzhi %rax,%rbx'; do
    printf '.text\n%s\n' "$bad" > "$work/x86-bmi-movw-bad.s"
    if "$MINIAS" --target=x86_64 -o "$work/x86-bmi-movw-bad.o" "$work/x86-bmi-movw-bad.s" >/dev/null 2>&1; then
        echo "illegal x86 BMI/MOVW width accepted: $bad" >&2;exit 1
    fi
done
echo 'MINIAS_X86_BMI_MOVW=PASS GNU_BMI_VEX_reg_all_widths MOVW_IMM16'

# Segment MOV and x87 m64real arithmetic share the existing x86
# ModR/M, SIB and true semantic PC32, not new instruction emitters.
cat > "$work/x86-seg-x87.s" <<'GNU_SEG_X87'
.text
movw saved_context+172(%rip), %fs
movw %gs, saved_context+16(%rip)
movw %fs, 4(%r12)
movw 4(%r13), %gs
faddl 8(%rax)
fmull 8(%rax)
fsubrl 8(%rax)
fsubl 8(%rax)
fdivl y(%rip)
fdivrl 8(%rax)
GNU_SEG_X87
"$MINIAS" --target=x86_64 -o "$work/x86-seg-x87-mini.o" "$work/x86-seg-x87.s"
as --64 -o "$work/x86-seg-x87-gnu.o" "$work/x86-seg-x87.s"
objcopy -j .text -O binary "$work/x86-seg-x87-mini.o" "$work/x86-seg-x87-mini.bin"
objcopy -j .text -O binary "$work/x86-seg-x87-gnu.o" "$work/x86-seg-x87-gnu.bin"
cmp "$work/x86-seg-x87-mini.bin" "$work/x86-seg-x87-gnu.bin"
for variant in mini gnu; do
    readelf -Wr "$work/x86-seg-x87-$variant.o" > "$work/x86-seg-x87-$variant.rel"
    grep -q 'R_X86_64_PC32.*saved_context + a8' "$work/x86-seg-x87-$variant.rel"
    grep -q 'R_X86_64_PC32.*saved_context + c' "$work/x86-seg-x87-$variant.rel"
    grep -q 'R_X86_64_PC32.*y - 4' "$work/x86-seg-x87-$variant.rel"
done
for bad in 'movw 8(%rax),%cs' 'movw %cs, %eax' 'movw %cr3, 8(%rax)' \
           'fdivl %rax' 'faddl $1' 'fdivrl %rax'; do
    printf '.text\n%s\n' "$bad" > "$work/x86-seg-x87-invalid.s"
    if "$MINIAS" --target=x86_64 -o "$work/x86-seg-x87-invalid.o" "$work/x86-seg-x87-invalid.s" >/dev/null 2>&1; then
        echo "bad segment / x87 operands accepted: $bad" >&2;exit 1
    fi
done
echo 'MINIAS_X86_SEG_X87=PASS GNU_bytes PC32_reloc_addend x87_DC_family'

# GNU far indirect jump/call pointer through Group-5 /5 and /3, same
# MCInst memory/PC32 route as normal indirect jmp/call. Reject far-reg.
cat > "$work/x86-far-memory.s" <<'X86_FAR_MEM'
.text
ljmpl *32(%rax)
ljmp *40(%r12)
lcall *48(%r12)
ljmpl *far_target(%rip)
lcall *far_target(%rip)
X86_FAR_MEM
"$MINIAS" --target=x86_64 -o "$work/x86-far-mini.o" "$work/x86-far-memory.s"
as --64 -o "$work/x86-far-gnu.o" "$work/x86-far-memory.s"
objcopy -j .text -O binary "$work/x86-far-mini.o" "$work/x86-far-mini.bin"
objcopy -j .text -O binary "$work/x86-far-gnu.o" "$work/x86-far-gnu.bin"
cmp "$work/x86-far-mini.bin" "$work/x86-far-gnu.bin"
for f in mini gnu; do
    readelf -Wr "$work/x86-far-$f.o" > "$work/x86-far-$f.reloc"
    test "$(grep -c 'R_X86_64_PC32.*far_target - 4' "$work/x86-far-$f.reloc")" -eq 2
done
for bad in 'ljmpl *%rax' 'lcall *%r15' 'ljmpl $12' 'lcall $9'; do
    printf '.text\n%s\n' "$bad" > "$work/x86-far-invalid.s"
    if "$MINIAS" --target=x86_64 -o "$work/x86-far-invalid.o" "$work/x86-far-invalid.s" >/dev/null 2>&1; then
        echo "invalid far indirect control operand accepted: $bad" >&2;exit 1
    fi
done
echo 'MINIAS_X86_FAR_MEMORY=PASS GNU_bytes_FF_ModRM_group3_5_PC32'
# Native GNU port IO aliases are explicit DX port tokens, not memory.
# x87 DE register stack group 0/1/4/5/6/7 uses the same validated
# fixed-word emitter as other no-fixup ISA opcodes.
cat > "$work/x86-port-x87-stack.s" <<'GNU_IO_X87'
.text
inl (%dx),%eax
inb (%dx),%al
inw (%dx),%ax
outl %eax,(%dx)
outw %ax,(%dx)
outb %al,(%dx)
inl %dx,%eax
faddp
fmulp
fsubp
fsubrp
fdivp
fdivrp
fsubp %st,%st(1)
fmulp %st,%st(3)
faddp %st,%st(7)
GNU_IO_X87
"$MINIAS" --target=x86_64 -o "$work/x86-port-x87-mini.o" "$work/x86-port-x87-stack.s"
as --64 -o "$work/x86-port-x87-gnu.o" "$work/x86-port-x87-stack.s"
objcopy -j .text -O binary "$work/x86-port-x87-mini.o" "$work/x86-port-x87-mini.bin"
objcopy -j .text -O binary "$work/x86-port-x87-gnu.o" "$work/x86-port-x87-gnu.bin"
cmp "$work/x86-port-x87-mini.bin" "$work/x86-port-x87-gnu.bin"
for bad in 'inl (%ax),%eax' 'outl %ecx,(%dx)' 'fsubp %st,%st(8)' \
           'fdivp %st(1),%st' 'fsubp %st,%st(-1)'; do
    printf '.text\n%s\n' "$bad" > "$work/x86-port-x87-invalid.s"
    if "$MINIAS" --target=x86_64 -o "$work/x86-port-x87-invalid.o" "$work/x86-port-x87-invalid.s" >/dev/null 2>&1; then
        echo "invalid IO/x87 operand accepted: $bad" >&2;exit 1
    fi
done
echo 'MINIAS_X86_PORT_X87=PASS GNU_bytes DX_port_aliases_x87_stack_pop'
# GNU allows both named and numeric labels on the SAME LINE as
# .irp/.rept, including macro expansions. Common must bind labels once.
cat > "$work/gnu-labeled-irp.s" <<'GNU_LABELED_IRP'
.data
first_repeat: .irp val,1,2,3
.byte \val
.endr
4: .rept 2
.byte 4
.endr
middle_repeat: .irp val,5,6
.byte \val
.endr
.macro LOOP_LABELED
body_start: .irp key,7,8
.byte \key
.endr
.endm
LOOP_LABELED
GNU_LABELED_IRP
for target in x86_64 loongarch64; do
    "$MINIAS" --target="$target" -o "$work/$target-labeled-mini.o" "$work/gnu-labeled-irp.s"
    if [ "$target" = x86_64 ]; then
        as --64 -o "$work/$target-labeled-gnu.o" "$work/gnu-labeled-irp.s"
        objcopy -j .data -O binary "$work/$target-labeled-mini.o" "$work/$target-labeled-mini.bin"
        objcopy -j .data -O binary "$work/$target-labeled-gnu.o" "$work/$target-labeled-gnu.bin"
    else
        "$LOONGAS" -mabi=lp64d -o "$work/$target-labeled-gnu.o" "$work/gnu-labeled-irp.s"
        "$LOONGCOPY" -j .data -O binary "$work/$target-labeled-mini.o" "$work/$target-labeled-mini.bin"
        "$LOONGCOPY" -j .data -O binary "$work/$target-labeled-gnu.o" "$work/$target-labeled-gnu.bin"
    fi
    cmp "$work/$target-labeled-mini.bin" "$work/$target-labeled-gnu.bin"
    for sym in first_repeat middle_repeat body_start; do
        readelf -Ws "$work/$target-labeled-mini.o" | grep -q " $sym$"
        readelf -Ws "$work/$target-labeled-gnu.o" | grep -q " $sym$"
    done
done
echo 'MINIAS_COMMON_GNU_LABELED_IRP=PASS GNU_x86_Loong_bytes_and_labels'
# GNU .ifdef/.ifndef must check only CURRENT defined symbol state,
# ignore forward references and global declarations, honor inactive
# parent branches, and share the standard .elseif/.else stack.
cat > "$work/gnu-defined-conditional.s" <<'GNU_DEFINED_COND'
.data
.set defined_equ, 17
.globl undefined_global
.ifdef defined_equ
.byte 17
.else
.byte 99
.endif
.ifndef undefined_global
.byte 23
.endif
.if 0
.ifdef never_defined
.byte 99
.endif
.else
.ifndef never_defined
.byte 24
.endif
.endif
defined_label:
.byte 1
.ifdef defined_label
.byte 42
.endif
.ifndef defined_equ
.byte 99
.elseif 1
.byte 25
.endif
GNU_DEFINED_COND
for target in x86_64 loongarch64; do
    "$MINIAS" --target="$target" -o "$work/$target-defined-mini.o" "$work/gnu-defined-conditional.s"
    if [ "$target" = x86_64 ]; then
        as --64 -o "$work/$target-defined-gnu.o" "$work/gnu-defined-conditional.s"
        objcopy -j .data -O binary "$work/$target-defined-mini.o" "$work/$target-defined-mini.bin"
        objcopy -j .data -O binary "$work/$target-defined-gnu.o" "$work/$target-defined-gnu.bin"
    else
        "$LOONGAS" -mabi=lp64d -o "$work/$target-defined-gnu.o" "$work/gnu-defined-conditional.s"
        "$LOONGCOPY" -j .data -O binary "$work/$target-defined-mini.o" "$work/$target-defined-mini.bin"
        "$LOONGCOPY" -j .data -O binary "$work/$target-defined-gnu.o" "$work/$target-defined-gnu.bin"
    fi
    cmp "$work/$target-defined-mini.bin" "$work/$target-defined-gnu.bin"
done
for bad in '.ifdef' '.ifndef' '.ifdef a,b' '.ifndef a b'; do
    printf '.text\n%s\n' "$bad" > "$work/gnu-defined-invalid.s"
    if "$MINIAS" --target=x86_64 -o "$work/gnu-defined-bad.o" "$work/gnu-defined-invalid.s" >/dev/null 2>&1; then
       echo "invalid .ifdef/.ifndef operand accepted: $bad" >&2;exit 1
    fi
done
echo 'MINIAS_COMMON_IFDEF_IFNDEF=PASS GNU_x86_Loong_defined_symbols'
echo 'MINIAS_COMMON_NUMERIC_U64=PASS GNU_GAS_x86_LA64_exact_data_bytes'
echo 'MINIAS_X86_SEGMENTED_INDIRECT=PASS GNU_AS_text_and_relocations segment_base_index_abs_rip=true indirect_memory_call_jmp=true absolute_symbol32S=true'
echo 'MINIAS_TWO_TARGET_ORACLE=PASS x86_GNU_bytes=equal loong_GNU_bytes=equal ELF_machine=correct lp64d_v1=0x43 CET_endbr64=1 ALU_imm8_imm32=1 x86_ModRM_SIB_disp=1 x86_test_cmp_alu_memory=1 x86_scaled_index_SIB=1 LA_jr=1 LA_scaled_ptr14=1 LA_imm12_shift_upper20=1 invalid=reject'

# Original Linux ARM64 fpsimdmacros GNU ALT macro numeric evaluation
# and LABEL: .ifc directive use the single Common stack on x86 too.
cat > "$work/gnu-altmacro-inline-ifc.s" <<'GNU_ALT_INLINE'
.data
.macro __for from:req,to:req
.if (\from) == (\to)
.byte \from
.else
__for %\from, %((\from) + ((\to) - (\from)) / 2)
__for %((\from) + ((\to) - (\from)) / 2 + 1), %\to
.endif
.endm
.altmacro
__for 0,7
.noaltmacro
.macro test_inline pn
0: .ifc \pn, p64
.byte 0x51
.else
.byte 0x52
.endif
.endm
test_inline p64
test_inline p32
GNU_ALT_INLINE
for target in x86_64 loongarch64; do
    "$MINIAS" --target="$target" -o "$work/$target-altinline-mini.o" "$work/gnu-altmacro-inline-ifc.s"
    if [ "$target" = x86_64 ]; then
        as --64 -o "$work/$target-altinline-gnu.o" "$work/gnu-altmacro-inline-ifc.s"
        objcopy -j .data -O binary "$work/$target-altinline-mini.o" "$work/$target-altinline-mini.bin"
        objcopy -j .data -O binary "$work/$target-altinline-gnu.o" "$work/$target-altinline-gnu.bin"
    else
        "$LOONGAS" -mabi=lp64d -o "$work/$target-altinline-gnu.o" "$work/gnu-altmacro-inline-ifc.s"
        "$LOONGCOPY" -j .data -O binary "$work/$target-altinline-mini.o" "$work/$target-altinline-mini.bin"
        "$LOONGCOPY" -j .data -O binary "$work/$target-altinline-gnu.o" "$work/$target-altinline-gnu.bin"
    fi
    cmp "$work/$target-altinline-mini.bin" "$work/$target-altinline-gnu.bin"
done
echo 'MINIAS_COMMON_ALT_MACRO_INLINE_IFC=PASS GNU_recursive_evaluated_and_lexical_labels'

# Named GNU relocation spelling belongs to x86 Target, but its section
# offset and RELA fixup lifecycle remain the same Common code as A64.
cat > "$work/x86-explicit-reloc.s" <<'X64_EXPLICIT'
.text
.long 0
.long 0
.reloc 0, R_X86_64_PC32, ext_rel+16
.reloc 4, R_X86_64_PC32, ext_rel+32
X64_EXPLICIT
"$MINIAS" --target=x86_64 -o "$work/x86-explicit-reloc-mini.o" "$work/x86-explicit-reloc.s"
as --64 -o "$work/x86-explicit-reloc-gnu.o" "$work/x86-explicit-reloc.s"
for owner in mini gnu; do
    readelf -Wr "$work/x86-explicit-reloc-$owner.o" |
        awk '$3=="R_X86_64_PC32" {print $1,$3,$5,$6,$7}' |
        sort > "$work/x86-explicit-reloc-$owner.txt"
done
[ "$(wc -l < "$work/x86-explicit-reloc-mini.txt")" -eq 2 ]
cmp "$work/x86-explicit-reloc-mini.txt" "$work/x86-explicit-reloc-gnu.txt"
echo 'MINIAS_COMMON_NAMED_X86_RELOC=PASS GNU_RELA_offsets_type_symbol_addend'

# Same Common .if numeric-label resolver for x86 and Loong, not A64-only.
cat > "$work/gnu-numeric-if.s" <<'GNU_NUMERIC_IF'
.data
661: .long 0
662: .long 0
.if ((662b-661b) != (1 * 4))
 .byte 0xff
.else
 .byte 0x2a
.endif
GNU_NUMERIC_IF
for target in x86_64 loongarch64; do
 "$MINIAS" --target="$target" -o "$work/$target-if-mini.o" "$work/gnu-numeric-if.s"
 if [ "$target" = x86_64 ]; then
  as --64 -o "$work/$target-if-gnu.o" "$work/gnu-numeric-if.s"
  objcopy -j .data -O binary "$work/$target-if-mini.o" "$work/$target-if-mini.bin"
  objcopy -j .data -O binary "$work/$target-if-gnu.o" "$work/$target-if-gnu.bin"
 else
  "$LOONGAS" -mabi=lp64d -o "$work/$target-if-gnu.o" "$work/gnu-numeric-if.s"
  "$LOONGCOPY" -j .data -O binary "$work/$target-if-mini.o" "$work/$target-if-mini.bin"
  "$LOONGCOPY" -j .data -O binary "$work/$target-if-gnu.o" "$work/$target-if-gnu.bin"
 fi
 cmp "$work/$target-if-mini.bin" "$work/$target-if-gnu.bin"
done
echo 'MINIAS_COMMON_NUMERIC_IF=PASS GNU_x86_and_Loong_bytes'
