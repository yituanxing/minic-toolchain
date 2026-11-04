/* Static IFUNC positive oracle: the relocation table is traversed by a
 * tiny CRT, exactly as glibc apply_irel does, and resolver side effects
 * prove that the .iplt slot is not an unresolved resolver address. */
.text
.globl _start
.type _start, @function
_start:
    leaq __rela_iplt_start(%rip), %r12
    leaq __rela_iplt_end(%rip), %r13
.Lnext_irel:
    cmpq %r13, %r12
    jae .Lafter_irel
    movq 16(%r12), %rax
    call *%rax
    movq (%r12), %rdi
    movq %rax, (%rdi)
    addq $24, %r12
    jmp .Lnext_irel
.Lafter_irel:
    call test_ifunc
    cmpq $73, %rax
    jne .Lfailed
    movq resolver_calls(%rip), %rax
    cmpq $1, %rax
    jne .Lfailed
    xorl %edi, %edi
    jmp .Lexit
.Lfailed:
    movl $1, %edi
.Lexit:
    movl $60, %eax
    syscall
.size _start, .-_start

.type ifunc_resolver, @function
ifunc_resolver:
    incq resolver_calls(%rip)
    leaq ifunc_impl(%rip), %rax
    ret

.globl test_ifunc
.type test_ifunc, @gnu_indirect_function
.set test_ifunc, ifunc_resolver

.type ifunc_impl, @function
ifunc_impl:
    movl $73, %eax
    ret

.data
.balign 8
resolver_calls:
    .quad 0
