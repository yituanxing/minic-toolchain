.text
.globl _start
.type _start,@function
_start:
    mov $60,%eax
    xor %edi,%edi
    syscall
.size _start,.-_start

.section .debug_abbrev,"",@progbits
    .byte 0x12,0x34,0x56
.Ldebug_abbrev0:
    .byte 0x78,0x9a

.section .debug_info,"",@progbits
.globl debug_main_anchor
debug_main_anchor:
    .long .Ldebug_abbrev0
    .long .Ldebug_abbrev0+1
    .quad _start
