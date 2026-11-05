.section .rodata.str1.8,"aMS",@progbits,1
.p2align 3
.Lmessage:
    .asciz "alpha-message"
.Ltail:
    .asciz "message"

.section .text,"ax",@progbits
.p2align 2
.globl _start
.type _start,@function
_start:
    adrp x0,.Lmessage
    add x0,x0,:lo12:.Lmessage
    adrp x1,.Ltail
    ldrb w2,[x1,:lo12:.Ltail]
    ret
.size _start,.-_start
