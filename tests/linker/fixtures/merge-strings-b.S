.section .rodata.str1.1,"aMS",@progbits,1
.Lfull_b:
    .asciz "abc-tail"
.Ltail_b:
    .asciz "tail"

.section .rodata.cst4,"aM",@progbits,4
.Lconstant_b:
    .long 0x11223344

.section .rodata.str2.2,"aMS",@progbits,2
.Lwide2_b:
    .short 0x41, 0x42, 0x43, 0
.Lwide2_tail_b:
    .short 0x42, 0x43, 0

.section .rodata.str4.4,"aMS",@progbits,4
.Lwide4_b:
    .long 0x51, 0x52, 0x53, 0
.Lwide4_tail_b:
    .long 0x52, 0x53, 0

.section .data,"aw",@progbits
.global merge_full_b
merge_full_b:
    .quad .Lfull_b
.global merge_tail_b
merge_tail_b:
    .quad .Ltail_b
.global merge_constant_b
merge_constant_b:
    .quad .Lconstant_b
.global merge_wide2_b
merge_wide2_b:
    .quad .Lwide2_b
.global merge_wide4_b
merge_wide4_b:
    .quad .Lwide4_b
