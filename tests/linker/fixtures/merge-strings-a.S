.section .rodata.str1.1,"aMS",@progbits,1
.Lfull_a:
    .asciz "abc-tail"
.Ltail_a:
    .asciz "tail"
.Lshort_before_long_a:
    .asciz "suffix"
.Llong_after_short_a:
    .asciz "prefix-suffix"

.section .rodata.cst4,"aM",@progbits,4
.Lconstant_a:
    .long 0x11223344

.section .rodata.str2.2,"aMS",@progbits,2
.Lwide2_a:
    .short 0x41, 0x42, 0x43, 0
.Lwide2_tail_a:
    .short 0x42, 0x43, 0

.section .rodata.str4.4,"aMS",@progbits,4
.Lwide4_a:
    .long 0x51, 0x52, 0x53, 0
.Lwide4_tail_a:
    .long 0x52, 0x53, 0

.section .data,"aw",@progbits
.global merge_full_a
merge_full_a:
    .quad .Lfull_a
.global merge_tail_a
merge_tail_a:
    .quad .Ltail_a
.global merge_short_before_long_a
merge_short_before_long_a:
    .quad .Lshort_before_long_a
.global merge_long_after_short_a
merge_long_after_short_a:
    .quad .Llong_after_short_a
.global merge_constant_a
merge_constant_a:
    .quad .Lconstant_a
.global merge_wide2_a
merge_wide2_a:
    .quad .Lwide2_a
.global merge_wide2_tail_a
merge_wide2_tail_a:
    .quad .Lwide2_tail_a
.global merge_wide4_a
merge_wide4_a:
    .quad .Lwide4_a
.global merge_wide4_tail_a
merge_wide4_tail_a:
    .quad .Lwide4_tail_a

.text
.global _start
_start:
    mov merge_full_a(%rip), %rdi
    mov merge_tail_a(%rip), %rsi
    lea 4(%rdi), %rax
    cmp %rax, %rsi
    jne .Lbad
    mov merge_full_b(%rip), %rax
    cmp %rax, %rdi
    jne .Lbad
    mov merge_tail_b(%rip), %rax
    cmp %rax, %rsi
    jne .Lbad
    mov merge_long_after_short_a(%rip), %rdi
    mov merge_short_before_long_a(%rip), %rsi
    lea 7(%rdi), %rax
    cmp %rax, %rsi
    jne .Lbad
    mov merge_constant_a(%rip), %rdi
    mov merge_constant_b(%rip), %rsi
    cmp %rsi, %rdi
    jne .Lbad
    cmpl $0x11223344, (%rdi)
    jne .Lbad
    mov merge_wide2_a(%rip), %rdi
    mov merge_wide2_tail_a(%rip), %rsi
    lea 2(%rdi), %rax
    cmp %rax, %rsi
    jne .Lbad
    mov merge_wide2_b(%rip), %rax
    cmp %rax, %rdi
    jne .Lbad
    mov merge_wide4_a(%rip), %rdi
    mov merge_wide4_tail_a(%rip), %rsi
    lea 4(%rdi), %rax
    cmp %rax, %rsi
    jne .Lbad
    mov merge_wide4_b(%rip), %rax
    cmp %rax, %rdi
    jne .Lbad
    xor %edi, %edi
    jmp .Lexit
.Lbad:
    mov $1, %edi
.Lexit:
    mov $60, %eax
    syscall
