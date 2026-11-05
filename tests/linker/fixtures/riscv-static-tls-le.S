.section .tdata,"awT",@progbits
.p2align 3
.globl test_tls_word
.type test_tls_word,@tls_object
test_tls_word:
    .word 47
.size test_tls_word,4

.section .tbss,"awT",@nobits
.p2align 3
.globl test_tls_extra
.type test_tls_extra,@tls_object
test_tls_extra:
    .zero 4
.size test_tls_extra,4

.text
.globl _start
.type _start,@function
_start:
    lui a0,%hi(test_tls_word)
    add a0,a0,tp
    lw a0,%lo(test_tls_word)(a0)
    lui a1,%hi(test_tls_extra)
    add a1,a1,tp
    sw a0,%lo(test_tls_extra)(a1)
    addi a0,zero,0
    addi a7,zero,93
    ecall
.size _start,.-_start
/* These placeholders supply genuine ELF R_RISCV_32 relocations.  The probe
 * changes each to a section-relative TPREL_ADD marker at the corresponding
 * ADD instruction; MiniAS does not yet parse GNU %tprel_add syntax. */
.word test_tls_word
.word test_tls_extra
