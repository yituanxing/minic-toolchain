/* A GNU linker-script contiguous R -> RX -> R -> RW -> BSS boundary.
 * The section flags differ, but all permission transitions fall within
 * one genuine LoongArch 16 KiB PT_LOAD page. */
.section .head,"a",@progbits
.global _start
_start:
    .space 4096

.section .text,"ax",@progbits
    .space 4096

.section .rodata,"a",@progbits
    .space 4096

.section .data,"aw",@progbits
    .space 512

.section .bss,"aw",@nobits
    .space 1024

.section .note.GNU-stack,"",@progbits
