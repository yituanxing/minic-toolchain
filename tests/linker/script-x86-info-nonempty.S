.section .text,"ax",@progbits
.globl _start
_start:
  mov $60,%eax
  xor %edi,%edi
  syscall

.section .data,"aw",@progbits
.quad 0x1122334455667788

/* An allocatable input converted by GNU's (INFO) output-section type. */
.section .got.plt,"aw",@progbits
.quad 0x0102030405060708
