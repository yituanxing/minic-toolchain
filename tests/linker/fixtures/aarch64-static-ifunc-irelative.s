/* One real AArch64 STT_GNU_IFUNC, with a glibc-style static IRELATIVE pass.
 * Calling the resolver itself as the implementation returns a code address,
 * not 73; the runtime consequently catches a missing IPLT/RELATIVE table. */
.text
.globl _start
.type _start, @function
_start:
    adrp x19,__rela_iplt_start
    add x19,x19,:lo12:__rela_iplt_start
    adrp x20,__rela_iplt_end
    add x20,x20,:lo12:__rela_iplt_end
.Lirel_next:
    cmp x19,x20
    b.hs .Lirel_done
    ldr x9,[x19,#16]
    blr x9
    ldr x10,[x19]
    str x0,[x10]
    add x19,x19,#24
    b .Lirel_next
.Lirel_done:
    bl test_ifunc
    cmp x0,#73
    b.ne .Lfailed
    adrp x11,resolver_calls
    add x11,x11,:lo12:resolver_calls
    ldr x12,[x11]
    cmp x12,#1
    b.ne .Lfailed
    mov x0,#0
    b .Lexit
.Lfailed:
    mov x0,#1
.Lexit:
    mov x8,#93
    svc #0
.size _start,.-_start

.globl test_ifunc
.type test_ifunc,@gnu_indirect_function
test_ifunc:
    adrp x9,resolver_calls
    add x9,x9,:lo12:resolver_calls
    ldr x10,[x9]
    add x10,x10,#1
    str x10,[x9]
    adrp x0,ifunc_impl
    add x0,x0,:lo12:ifunc_impl
    ret

.type ifunc_impl,@function
ifunc_impl:
    mov x0,#73
    ret

.data
.balign 8
resolver_calls:
    .quad 0
