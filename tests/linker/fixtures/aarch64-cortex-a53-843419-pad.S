/* Initial .text size = 0x1010, so an ALIGN(16) output is initially empty.
 * Moving the affected load into an 8-byte island grows .text to 0x1018;
 * the subsequent ALIGN creates an 8-byte pure SHT_NOBITS script section. */
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
