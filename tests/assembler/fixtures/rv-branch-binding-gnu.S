/* Binding-sensitive RV branches: GNU GAS is the bytes + RELA oracle.
 * The script replaces .option MODE_PLACEHOLDER with rvc or norvc. */
.text
.option norelax
.option MODE_PLACEHOLDER
.Llocal:
    beq a0, zero, .Llocal
    jal ra, .Llocal
    j .Llocal
.globl strong
strong:
    beq a0, zero, strong
    jal ra, strong
    beq a0, zero, strong+4
    jal ra, strong+4
.weak weak_symbol
weak_symbol:
    beq a0, zero, weak_symbol
    jal ra, weak_symbol
    j weak_symbol
    beq a0, zero, weak_symbol+4
    jal ra, weak_symbol+4
    beq a0, zero, external_symbol
    jal ra, external_symbol
    j external_symbol
    beq a0, zero, external_symbol+4
    jal ra, external_symbol+4
    beq a0, zero, cross_symbol
    beq a0, zero, cross_symbol+4
    beq a0, a1, far_target
    .space 4200
far_target:
    nop
.section .text.more,"ax"
cross_symbol:
    nop
