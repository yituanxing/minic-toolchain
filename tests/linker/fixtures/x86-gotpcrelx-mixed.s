/* x86 GCC/CRT compatibility: R_X86_64_REX_GOTPCRELX names a GOT-relative
 * relocation, not a promise that the containing instruction is MOV.
 *
 * GNU AS ordinarily selects relaxable GOTPCRELX from a MOV; explicit .reloc
 * attaches the very same ELF relocation to valid RIP-relative ADD/XOR
 * operands, as produced by differing toolchains and assembly sources.
 * Static GNU BFD must leave these operations indirect through a GOT slot. */
.text
.globl _start
.type _start,@function
_start:
    xorq %rax,%rax
    addq 0(%rip),%rax
    .reloc .-4,R_X86_64_REX_GOTPCRELX,fixture_value-4
    movq (%rax),%rax
    cmpq $73,%rax
    jne fail

    leaq fixture_value(%rip),%rax
    xorq 0(%rip),%rax
    .reloc .-4,R_X86_64_REX_GOTPCRELX,fixture_value-4
    testq %rax,%rax
    jne fail

    /* The no-REX variant must likewise preserve a genuine GOT operand. */
    movl $fixture_value,%eax
    xorl 0(%rip),%eax
    .reloc .-4,R_X86_64_GOTPCRELX,fixture_value-4
    testl %eax,%eax
    jne fail

    /* Ordinary relaxable REX MOV remains optimized to a direct LEA. */
    movq fixture_value@GOTPCREL(%rip),%r11
    movq (%r11),%rax
    cmpq $73,%rax
    jne fail

    /* A weak undefined reference is zero in the GOT; never LEA its
     * nonexistent address or accidentally allocate a TLS offset. */
    movq optional_hook@GOTPCREL(%rip),%r10
    testq %r10,%r10
    jne fail

    /* The symbol itself can be out of signed-32 displacement range even
     * though the .got remains nearby; keep the GOT load in that case. */
    movq far_absolute@GOTPCREL(%rip),%r12
    movabsq $0x123456788000,%r13
    cmpq %r13,%r12
    jne fail

    movl $60,%eax
    xorl %edi,%edi
    syscall
fail:
    movl $60,%eax
    movl $7,%edi
    syscall
.size _start,.-_start

.data
.balign 8
.globl fixture_value
.type fixture_value,@object
fixture_value:
    .quad 73
.size fixture_value,8
.weak optional_hook
.globl far_absolute
.set far_absolute,0x123456788000
