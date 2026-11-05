/* RISC-V Initial Exec optional weak TLS symbol, matching glibc's
 * _nl_current_LC_COLLATE binding without relying on glibc archives.
 * The probe changes the MiniAS GOT_HI20 relocation to TLS_GOT_HI20. */
.section .tdata,"awT",@progbits
.p2align 3
.word 0xdecafbad
.p2align 3
.globl fixture_tls_defined
.type fixture_tls_defined,@tls_object
fixture_tls_defined:
    .word 7
.size fixture_tls_defined,4

.text
.weak fixture_tls_optional
.type fixture_tls_optional,@tls_object
.globl _start
.type _start,@function
_start:
    .option push
    .option pic
    la a0,fixture_tls_optional
    .option pop
    add a0,a0,tp
    lw a0,0(a0)
    .option push
    .option pic
    la a1,fixture_tls_defined
    .option pop
    add a1,a1,tp
    lw a1,0(a1)
    addi a0,zero,0
    addi a7,zero,93
    ecall
.size _start,.-_start
