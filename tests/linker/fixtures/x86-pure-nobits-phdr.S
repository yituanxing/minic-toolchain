.section .text.entry,"ax",@progbits
.globl _start
_start:
    mov $60, %eax
    xor %edi, %edi
    syscall

.section .bss.payload,"aw",@nobits
.globl bss_payload
bss_payload:
    .skip 80
