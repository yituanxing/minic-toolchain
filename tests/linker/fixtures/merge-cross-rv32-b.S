.section .rodata.str1.1,"aMS",@progbits,1
.Lfull:
    .asciz "prefix-tail"
.Ltail:
    .asciz "tail"
.section .data,"aw",@progbits
.globl merge_cross_full_b
merge_cross_full_b:
    .long .Lfull
.globl merge_cross_tail_b
merge_cross_tail_b:
    .long .Ltail
