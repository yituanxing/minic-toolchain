.section .debug_abbrev,"",@progbits
    .byte 0xbc,0xde
.Ldebug_abbrev_second:
    .byte 0xf0

.section .debug_info,"",@progbits
.globl debug_extra_anchor
debug_extra_anchor:
    .long .Ldebug_abbrev_second
