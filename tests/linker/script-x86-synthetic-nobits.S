.section .text,"ax",@progbits
.globl _start
_start:
  mov $60,%eax
  xor %edi,%edi
  syscall
