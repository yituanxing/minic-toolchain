.section .text,"ax",@progbits
.global _start
_start:
  ret

.section .data,"aw",@progbits
.global data_anchor
data_anchor:
  .long 0x11223344

.section .tdata,"awT",@progbits
.global tls_anchor
tls_anchor:
  .quad 0x1122334455667788
