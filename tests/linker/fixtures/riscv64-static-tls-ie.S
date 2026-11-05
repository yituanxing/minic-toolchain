/* Initial-exec GOT relocation source, patched from MiniAS GOT_HI20. */
.section .tdata,"awT",@progbits
.p2align 3
.globl test_tls_word
.type test_tls_word,@tls_object
test_tls_word:
    .dword 0x12345678
.size test_tls_word,8

.section .tbss,"awT",@nobits
.p2align 3
.globl test_tls_extra
.type test_tls_extra,@tls_object
test_tls_extra:
    .zero 8
.size test_tls_extra,8

.text
.globl _start
.type _start,@function
_start:
    .option push
    .option pic
    la a0,test_tls_extra
    .option pop
    add a0,a0,tp
    ld a0,0(a0)
    addi a0,zero,0
    addi a7,zero,93
    ecall
.size _start,.-_start
