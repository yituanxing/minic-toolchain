.section .init.pi.data,"awx",@progbits
.balign 8
.globl init_pi_data
init_pi_data:
    .quad 0x8877665544332211

.section .init.pi.bss,"aw",@nobits
.balign 8
.globl init_pi_zero
init_pi_zero:
    .zero 16

.section .tail.data,"aw",@progbits
.balign 8
.globl init_pi_tail
init_pi_tail:
    .quad 0xaabbccdd10203040
