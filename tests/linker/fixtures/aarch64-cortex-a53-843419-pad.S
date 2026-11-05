/* Initial .text size = 0x1010.  A real erratum allocates 0x1000 page-stable
 * veneer bytes (GNU BFD semantics), which enables the script-generated
 * .pad NOBITS section only after the second layout trial. */
.section .text.pad_trigger,"ax",@progbits
.p2align 12
.space 4088
.globl _start
.type _start,@function
_start:
    adrp x0,pad_fixture_value
    str x2,[x1]
    ldr x3,[x0,:lo12:pad_fixture_value]
    ret
    nop
    nop
.size _start,.-_start

.section .data,"aw",@progbits
.p2align 3
.globl pad_fixture_value
pad_fixture_value:
    .quad 0x9988776655443322
