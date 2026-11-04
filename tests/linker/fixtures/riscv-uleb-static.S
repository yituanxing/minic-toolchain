.section .text,"ax",@progbits
.globl _start
_start:
    addi a0,zero,0
    addi a7,zero,93
    ecall
.Luleb_begin:
    .zero 120
.Luleb_end:

.section .debug_info,"",@progbits
/* A deliberately padded two-byte ULEB128.  The following .word values
 * are relocation holders; the probe rewrites their offsets and r_types
 * into a matched SET_ULEB128 / SUB_ULEB128 pair at the first byte. */
    .byte 0x80,0x00
    .word .Luleb_end
    .word .Luleb_begin
