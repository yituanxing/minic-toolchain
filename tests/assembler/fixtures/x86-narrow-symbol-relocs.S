.text
.globl narrow_start
narrow_start:
    .byte abs_byte + 1
    .short abs_half + 2
    .byte cross_data - .
    .short cross_data - .
    .long abs_long + 3
.data
.globl cross_data
cross_data:
    .long 0
