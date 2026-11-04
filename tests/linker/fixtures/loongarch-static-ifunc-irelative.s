/* One real LoongArch STT_GNU_IFUNC and a static glibc-like apply_irel loop.
 * Calling the resolver instead of the selected implementation returns a
 * function pointer, not 73, so the executable catches missing IPLT support. */
.text
.globl _start
.type _start,@function
.weak __rela_iplt_start
.weak __rela_iplt_end
_start:
    la.local $r19,__rela_iplt_start
    la.local $r20,__rela_iplt_end
.Lirel_next:
    beq $r19,$r20,.Lirel_done
    ld.d $r12,$r19,16
    jirl $r1,$r12,0
    ld.d $r13,$r19,0
    st.d $r4,$r13,0
    addi.d $r19,$r19,24
    b .Lirel_next
.Lirel_done:
    bl test_ifunc
    addi.d $r4,$r4,-73
    bnez $r4,.Lfailed
    la.local $r12,resolver_calls
    ld.d $r13,$r12,0
    addi.d $r4,$r13,-1
    bnez $r4,.Lfailed
    ori $r4,$r0,0
    b .Lexit
.Lfailed:
    ori $r4,$r0,1
.Lexit:
    ori $r11,$r0,93
    syscall 0
.size _start,.-_start

.globl test_ifunc
.type test_ifunc,@gnu_indirect_function
test_ifunc:
    la.local $r12,resolver_calls
    ld.d $r13,$r12,0
    addi.d $r13,$r13,1
    st.d $r13,$r12,0
    la.local $r4,ifunc_impl
    jirl $r0,$r1,0

.type ifunc_impl,@function
ifunc_impl:
    ori $r4,$r0,73
    jirl $r0,$r1,0

.section .data,"aw",@progbits
.p2align 3
resolver_calls:
    .quad 0
