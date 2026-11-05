.section .text,"ax",@progbits
.globl _start
_start:
    lla t0,pi_code
    lbu t1,0(t0)
    addi t2,zero,81
    bne t1,t2,bad_pi
    lla t0,pi_bss
    ld t1,0(t0)
    bne t1,zero,bad_pi
    ld t1,8(t0)
    bne t1,zero,bad_pi
    lla t0,pi_tail
    ld t1,0(t0)
    addi t2,zero,37
    bne t1,t2,bad_pi
    addi a0,zero,0
    addi a7,zero,93
    ecall
bad_pi:
    addi a0,zero,1
    addi a7,zero,93
    ecall

.section .init.pi.code,"ax",@progbits
.globl pi_code
pi_code:
    .byte 81, 0, 0, 0, 0, 0, 0, 0

.section .init.pi.bss,"aw",@nobits
.balign 8
.globl pi_bss
pi_bss:
    .zero 16

.section .init.pi.tail,"aw",@progbits
.balign 8
.globl pi_tail
pi_tail:
    .quad 37
