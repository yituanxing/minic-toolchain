.section .data..ro_after_init,"aw",@progbits
.balign 8
.globl ro_after_init_value
ro_after_init_value:
    .quad 0x1122334455667788
