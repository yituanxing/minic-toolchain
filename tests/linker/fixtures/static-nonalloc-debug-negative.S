.text
.globl _start
_start:
    mov $60,%eax
    xor %edi,%edi
    syscall

.section .debug_abbrev,"",@progbits
.Ldebug_unmapped:
    .byte 0x11

.data
.globl illegally_mapped_debug_pointer
illegally_mapped_debug_pointer:
    .quad .Ldebug_unmapped
