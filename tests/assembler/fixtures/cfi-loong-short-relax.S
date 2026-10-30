    .text
    .globl cfi_short_fixed
    .type cfi_short_fixed, @function
cfi_short_fixed:
    .cfi_startproc
    addi.d $r0,$r0,0
    .cfi_def_cfa_offset 8
    addi.d $r0,$r0,0
    addi.d $r0,$r0,0
    .cfi_def_cfa_offset 16
    addi.d $r0,$r0,0
    .cfi_endproc
    .size cfi_short_fixed,.-cfi_short_fixed

    .globl cfi_short_relax
    .type cfi_short_relax, @function
cfi_short_relax:
    .cfi_startproc
    addi.d $r0,$r0,0
    .cfi_def_cfa_offset 8
    # Two instructions initially occupy eight bytes, but R_LARCH_RELAX
    # allows the linker to shorten this span.  Its CFI delta must stay
    # symbolic, even though the assembler-time distance is only eight.
    la.global $r12,external_data
    .cfi_def_cfa_offset 16
    addi.d $r0,$r0,0
    .cfi_endproc
    .size cfi_short_relax,.-cfi_short_relax
