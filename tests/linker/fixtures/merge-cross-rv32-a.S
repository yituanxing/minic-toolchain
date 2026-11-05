.section .rodata.str1.1,"aMS",@progbits,1
.Lfull:
    .asciz "prefix-tail"
.Ltail:
    .asciz "tail"
.section .data,"aw",@progbits
.globl merge_cross_full_a
merge_cross_full_a:
    .long .Lfull
.globl merge_cross_tail_a
merge_cross_tail_a:
    .long .Ltail
.text
.globl _start
_start:
    .long 0
