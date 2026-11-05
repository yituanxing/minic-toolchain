.section .text,"ax",@progbits
.globl _start
.globl live_global
.weak live_weak
_start:
    ret
live_global:
    ret
live_weak:
    ret
live_local:
    ret

.section .data.alive,"aw",@progbits
    .quad live_global

.section .data.discard,"aw",@progbits
    .quad live_global
    .quad live_weak
    .quad live_local
