/* Each test section starts on a 4 KiB boundary.  The positive ADRPs land at
 * +0xff8, the negative one at +0xff4.  Keep executable padding separate from
 * the named code label so a real AArch64 object provides $x/$d mappings. */

.section .text.positive,"ax",@progbits
.p2align 12
.space 4088
.globl _start
.type _start,@function
_start:
    adrp x0,fixture_value
    str x2,[x1]
    ldr x3,[x0,:lo12:fixture_value]
    ret
.size _start,.-_start
/* A trailing literal pool marks the end of code ($d); the appended island
 * must announce a fresh $x mapping even though its section was all code
 * up to this point. */
    .word 0x12345678

/* This ADRP sits at +0xff0 and is safe.  GNU BFD reserves a full 4 KiB
 * capacity in each erratum stub section, so an earlier source's patch
 * must NOT move this ADRP to +0xff8 and create a false cascade. */
.section .text.cascade,"ax",@progbits
.p2align 2
.space 4068
.globl cascade_sequence
.type cascade_sequence,@function
cascade_sequence:
    adrp x0,fixture_value
    str x2,[x1]
    ldr x3,[x0]
    ret
.size cascade_sequence,.-cascade_sequence

.section .text.optional,"ax",@progbits
.p2align 12
.space 4088
.globl optional_sequence
.type optional_sequence,@function
optional_sequence:
    adrp x0,fixture_value
    str x2,[x1]
    nop
    ldr x3,[x0]
    ret
.size optional_sequence,.-optional_sequence

.section .text.page_last,"ax",@progbits
.p2align 12
.space 4092
.globl page_last_sequence
.type page_last_sequence,@function
page_last_sequence:
    adrp x0,fixture_value
    str x2,[x1]
    ldr x3,[x0]
    ret
.size page_last_sequence,.-page_last_sequence

.section .text.write_destination,"ax",@progbits
.p2align 12
.space 4088
.globl write_destination_sequence
.type write_destination_sequence,@function
write_destination_sequence:
    adrp x0,fixture_value
    ldr x0,[x1]
    ldr x3,[x0]
    ret
.size write_destination_sequence,.-write_destination_sequence

.section .text.negative,"ax",@progbits
.p2align 12
.space 4084
.globl unpatched_sequence
.type unpatched_sequence,@function
unpatched_sequence:
    adrp x0,fixture_value
    str x2,[x1]
    ldr x3,[x0]
    ret
.size unpatched_sequence,.-unpatched_sequence

.section .text.embedded_data,"ax",@progbits
.p2align 12
.space 4088
.globl embedded_data_sequence
embedded_data_sequence:
    .word 0x90000000
    .word 0xf9000022
    .word 0xf9400003
.globl end_of_embedded_data
end_of_embedded_data:
    ret

.section .data,"aw",@progbits
.p2align 3
.space 24
.globl fixture_value
.type fixture_value,@object
fixture_value:
    .quad 0x9988776655443322
.size fixture_value,8
