/* Tiny freestanding RV64 exit trampoline; no libc/toolchain runtime. */
    .text
    .globl _start
_start:
    call anchor
    li a7, 93
    ecall
