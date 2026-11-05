.section .text,"ax",@progbits
.global _start
_start:
  ret

.section .data,"aw",@progbits
.global data_anchor
data_anchor:
  .long 0x11223344
