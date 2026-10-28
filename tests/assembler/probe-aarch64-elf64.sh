#!/usr/bin/env bash
set -Eeuo pipefail
: "${MINIAS:?MINIAS must be set}"
: "${AARCH64_AS:=aarch64-linux-gnu-as}"
: "${AARCH64_LD:=aarch64-linux-gnu-ld}"
: "${AARCH64_OBJCOPY:=aarch64-linux-gnu-objcopy}"
: "${AARCH64_READELF:=aarch64-linux-gnu-readelf}"
: "${QEMU_AARCH64:=qemu-aarch64}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# One real AArch64 instruction stream, ISA-specific immediate syntax,
# an external call relocation, a local jump and generic data relocations.
cat >"$work/vertical.s" <<'SRC'
# 0 "linux/arch/arm64/kernel/example.S"
# 18 "linux/arch/arm64/kernel/example.S" 2
.arch armv8-a
.text
.globl _start
.type _start, @function
_start:
#APP
    nop // AArch64 inline comment
#NO_APP
    mov x0, #0
    hint #25
    hint 25
    hint #0
    hint #127
    // Linux BTI macro expands HINT with an absolute .equ symbol.
    .equ .L__bti_targets_c, 34
    .equ .L__bti_targets_j, 36
    .equ .L__bti_targets_jc, 38
    hint #.L__bti_targets_c
    hint #.L__bti_targets_j+1
    .macro bti targets
        hint #.L__bti_targets_\targets
    .endm
    bti targets=jc
    bl after_call
    b exit_code
after_call:
    ret
exit_code:
    mov x8, #93
    svc #0
.globl external_probe
external_probe:
    // GNU-byte ISA test only: privileged MRS/MSR must not execute at EL0.
    // GNU A64 target-owned register aliases: exact-site alias binding,
    // memory operands, alias withdrawal and subsequent safe name reuse.
    wx0 .req w0
    xx1 .req x1
    add wx0, wx0, #1
    mov xx1, xx1
    ldr x2, [xx1]
    .unreq wx0
    .unreq xx1
    wx0 .req w3
    add wx0, wx0, #2
    .unreq wx0
    // GNU SIMD .req alias lifecycle + ORR-based MOV vector widths.
    SHASH .req v0
    dga .req q20
    mov SHASH.16b, v1.16b
    mov v3.16b, v1.16b
    mov v2.8b, v5.8b
    stp dga, q21, [x2]
    .unreq SHASH
    .unreq dga
    mrs x0, sp_el0
    mrs x2, sp_el0
    mrs x7, tpidr_el0
    mrs x0, tpidr_el1
    // GNU privileged ARMv8 sysregs: read/write permission encoded by ISA.
    mrs x0, CurrentEL
    // GNU one-parameter arithmetic macro (Linux arm64 nops convention).
    .macro nops num
        .rept \num
        nop
        .endr
    .endm
    nops (32 - 16) / 4
    nops 2 + 1
    // Linux AArch64 conditional arithmetic with nested GNU ||/&&.
    .if (((0xbadca11) >> 31) == 0 || ((0xbadca11) >> 31) == 0x1ffffffff)
        nop
    .else
        brk #1
    .endif
    .if !0 && (1 != 2)
        nop
    .endif
    // Real kernel nops arithmetic: numeric local backward expressions
661:
    nop
662:
    nops (662b - 661b) / 4
    // AArch64 Linux EL0/EL1/EL2 sysreg matrix; exact GNU bytes and
    // read-only permission guards, including case-insensitive spelling.
    mrs x1, ID_AA64MMFR1_EL1
    mrs x3, HPFAR_EL2
    msr hpfar_el2, x0
    mrs x2, dbgbcr0_el1
    msr dbgbcr0_el1, x2
    mrs x3, dbgbvr0_el1
    msr dbgbvr0_el1, x4
    mrs x4, par_el1
    msr par_el1, x5
    mrs x1, hcr_el2
    msr hcr_el2, x7
    mrs x0, mdcr_el2
    msr mdcr_el2, x0
    at s1e1r, x2
    at s1e1w, x3
    at s1e0r, x4
    at s1e0w, x5
    at s1e2r, x6
    at s1e2w, x7
    at s12e1r, x3
    at s12e1w, x4
    at s1e3r, x1
    at s1e3w, x2
    mrs x1, isr_el1
    mrs x3, spsr_el2
    msr spsr_el2, x4
    mrs x2, dbgwcr0_el1
    msr dbgwcr0_el1, x6
    mrs x3, vttbr_el2
    msr vttbr_el2, x0
    mrs x1, cnthctl_el2
    msr cnthctl_el2, x1
    mrs x3, clidr_el1
    mrs x1, ctr_el0
    mrs x0, CNTVCT_EL0
    mrs x0, SCTLR_EL1
    // Architectural generic-numbered sysregs plus named FP/EL2 aliases:
    // GNU native oracle validates *both* exact words and strict grammar.
    mrs x1, fpsr
    msr fpsr, x1
    mrs x2, fpcr
    msr fpcr, x2
    mrs x0, hstr_el2
    msr hstr_el2, x0
    mrs x2, pmuserenr_el0
    msr pmuserenr_el0, x1
    mrs x1, pmccntr_el0
    msr pmccntr_el0, x1
    mrs x0, ifsr32_el2
    msr ifsr32_el2, x1
    mrs x0, cptr_el2
    msr cptr_el2, x1
    mrs x0, dbgauthstatus_el1
    mrs x0, vmpidr_el2
    msr vmpidr_el2, x2
    mrs x0, aidr_el1
    mrs x0, pmceid0_el0
    mrs x0, pmccfiltr_el0
    msr pmccfiltr_el0, x0
    mrs x0, cntp_cval_el0
    msr cntp_cval_el0, x0
    mrs x0, mair_el2
    msr mair_el2, x1
    mrs x0, nzcv
    msr nzcv, x1
    // Cross-check privileged system register permissions and the
    // indexed PMU family with native GNU AS exact instruction words.
    mrs x1, dbgvcr32_el2
    msr dbgvcr32_el2, x1
    mrs x0, revidr_el1
    mrs x1, spsel
    msr spsel, x1
    msr oslar_el1, xzr
    mrs x0, cntkctl_el1
    msr cntkctl_el1, x1
    mrs x19, pmceid1_el0
    mrs x0, sp_el1
    msr sp_el1, x0
    mrs x10, oslsr_el1
    mrs x0, cntv_cval_el0
    msr cntv_cval_el0, x0
    mrs x0, pmevtyper0_el0
    msr pmevtyper15_el0, x1
    mrs x0, pmevtyper30_el0
    msr pmevtyper30_el0, x1
    mrs x0, pmevcntr0_el0
    msr pmevcntr16_el0, x1
    mrs x0, pmevcntr30_el0
    mov x0, #(-1)
    mov w2, #(-22)
    mov x3, #(-65536)
    // NEG/SUB shifted register aliases are real GNU Linux asm.
    neg x2, x2, asr 2
    neg x15, x15, lsl 6
    neg w0, w0, lsl 1
    negs x4, x0, lsr 2
    neg x10, x1, lsl 4
    mrs x2, pmselr_el0
    msr pmselr_el0, x2
    mrs x0, dacr32_el2
    msr dacr32_el2, x1
    mrs x21, s0_3_c1_c0_1
    msr s0_3_c1_c0_1, x21
    mrs x22, s3_3_c4_c4_1
    msr s3_3_c4_c4_1, x22
    // SIMD register-bank pair and structure LD1/ST1: all exact GNU
    // words tested, using existing typed memory and pair encoders.
    stp q0, q1, [x0]
    stp q5, q7, [x1, #-32]!
    stp q29, q30, [sp], #32
    ldp q0, q1, [x0]
    ldp q2, q3, [x5, #16]
    ldnp q2, q3, [x5, #16]
    stnp q2, q3, [x5, #16]
    stp q0, q1, [x0, #16 * 0]
    ld1 {v0.16b}, [x2]
    ld1 {v1.8b}, [x2]
    ld1 {v1.8h}, [x2]
    ld1 {v1.4h}, [x2]
    ld1 {v1.4s}, [x2]
    ld1 {v1.2s}, [x2]
    ld1 {v1.2d}, [x2]
    ld1 {v1.1d}, [x2]
    st1 {v1.16b}, [x2]
    st1 {v1.8b}, [x2]
    st1 {v1.2d}, [x2]
    // Real Kbuild LD1 post-index and matching ST1 forms (Q-dependent
    // increment and register Rm). Compare exact GNU .text words.
    ld1 {v1.4s}, [x0], #16
    ld1 {v2.16b}, [x4], #16
    ld1 {v3.2d}, [x4], #16
    st1 {v5.8b}, [x7], #8
    st1 {v5.2d}, [x7], #16
    ld1 {v5.8b}, [x7], x12
    st1 {v5.16b}, [x7], x11
    // Raw ARMv8 bitfield family (not only UBFx aliases): GNU exact bytes.
    ubfm x3, x3, #16, #19
    sbfm x4, x5, #31, #32
    bfm x6, x7, #63, #0
    ubfm w3, w3, #16, #19
    sbfm w4, w5, #31, #0
    bfm w6, w7, #0, #31
    movz x1, #(((1 << 15) | 0x1234))
    movk w2, #((0xf000 | 0xf))
    movn x4, #((1 << 5) + 3)
    // GNU Linux macros emit .inst(<logical expression>) without space.
    .inst(0xd5200000|((((3)<<19))))
    .inst((0xd503201f))
    // Real GNU CPP style: spaces INSIDE a glued function-like .inst.
    .inst(0xd5200000|((((3) << 19) | ((0) << 16) | ((1) << 12) | ((0) << 8) | ((3) << 5) | (2))))
    // Native GNU/LLVM checked A64 W-exclusive subword variants.
    ldxrb w2, [x3]
    ldaxrb w2, [x3]
    ldxrh w2, [x3]
    ldaxrh w2, [x3]
    stxrb w4, w2, [x3]
    stlxrb w4, w2, [x3]
    stxrh w4, w2, [x3]
    stlxrh w4, w2, [x3]
    // Non-temporal signed imm7 pair encodings, no writeback allowed.
    stnp x2, x3, [x0, #-256]
    ldnp xzr, xzr, [x0]
    stnp w2, w3, [x0, #252]
    ldnp w4, w5, [sp, #-256]
    // EL2 and ARM debug system registers, independently byte-checked.
    mrs x5, elr_el2
    msr elr_el2, x5
    mrs x2, dbgwvr0_el1
    msr dbgwvr0_el1, x2
    mrs x2, dbgwcr14_el1
    msr dbgwcr14_el1, x2
    mrs x0, spsr_abt
    msr spsr_abt, x0
    // Table-free debug-bank index coding + EL1/banked privilege forms.
    mrs x0, mdccint_el1
    msr mdccint_el1, x0
    mrs x1, spsr_und
    msr spsr_und, x1
    mrs x0, fpexc32_el2
    msr fpexc32_el2, x0
    mrs x1, spsr_irq
    msr spsr_irq, x1
    mrs x2, spsr_fiq
    msr spsr_fiq, x2
    mrs x2, dbgwcr13_el1
    msr dbgwcr13_el1, x2
    mrs x1, dbgbcr15_el1
    msr dbgbcr15_el1, x1
    mrs x2, dbgbvr7_el1
    mrs x3, dbgwvr15_el1
    eret
    msr tpidrro_el0, x0
    msr daif, xzr
    // Privileged system maintenance, GNU/LLVM verified; not executed.
    ic ialluis
    ic iallu
    ic ivau, x4
    dc civac, x0
    dc cvac, x1
    dc cvau, x2
    dc ivac, x3
    dc zva, x8
    tlbi vmalle1
    tlbi vmalle1is
    tlbi alle1
    tlbi alle1is
    tlbi vae1, x5
    tlbi vae1is, x6
    tlbi aside1, x7
    tlbi aside1is, x8
    tlbi vale1, x9
    tlbi vaale1, x10
    tlbi ipas2e1is, x11
    tlbi alle2
    tlbi alle3
    tlbi vmalls12e1is
    mrs x0, sctlr_el1
    msr sctlr_el1, x2
    mrs x3, dczid_el0
    mrs x4, cntfrq_el0
    mrs x5, ttbr0_el1
    msr ttbr0_el1, x6
    mrs x7, vbar_el2
    msr vbar_el2, x8
    mrs x9, id_aa64dfr0_el1
    mrs x10, esr_el2
    mrs x11, far_el1
    msr cntvoff_el2, x12
    msr vtcr_el2, x13
    mrs x14, tcr_el1
    msr tcr_el1, x15
    mrs x16, mair_el1
    msr mair_el1, x17
    mrs x18, cntvct_el0
    mrs x19, spsr_el1
    msr spsr_el1, x20
    mrs x21, osdlr_el1
    msr osdlr_el1, x22
    mrs x5, tpidr_el2
    msr tpidr_el2, x6
    msr tpidr_el1, x1
    mrs x2, daif
    msr daif, x3
    msr daifset, #3
    msr daifclr, #12
    msr daifset, #15
    yield
    wfe
    wfi
    sev
    sevl
    mrs x11, tpidrro_el0
    msr sp_el0, x2
    dmb oshst
    dmb ish
    dmb sy
    dsb sy
    dsb ish
    dsb oshst
    isb
    isb sy
    isb #15
    cmn x0, #4096
    cmn w1, #4096
    // GNU normal conditional select/negate family, not CSET pseudo aliases.
    csel w0, w1, wzr, eq
    csel x2, x3, x4, ne
    csinc w5, w6, w7, cs
    csinv x8, x9, x10, lt
    csneg x11, x12, x13, mi
    // GNU condensed CNEG/CINV are CSNEG/CSINV with inverted cond.
    cneg x0, x1, eq
    cneg w4, w5, ne
    cinv x9, x10, gt
    cinv w11, w12, le
    // CCMP/CCMN register and immediate forms (NZCV + condition bits).
    // Immediate CCMP/NZCV and GNU EL1 MRS encodings on real target.
    lsl w1, w2, #1
    lsl x1, x2, #1
    lsr w1, w2, #1
    lsr x1, x2, #1
    lsl w1, w2, w3
    lsl x1, x2, x3
    lsr w1, w2, w3
    lsr x1, x2, x3
    // GNU signed ALU/carry and multiply alias families, typed MCInst.
    neg w0, w0
    neg x1, x2
    negs w3, w4
    ngc x5, x6
    ngcs w7, w8
    adc w0, w1, w2
    adc x3, x4, x5
    adcs x6, x7, x8
    sbc x0, xzr, xzr
    sbcs w9, w10, w11
    mul w12, w13, w14
    mul x15, x16, x17
    mneg x18, x19, x20
    umull x1, w19, w1
    smull x2, w3, w4
    umulh x5, x6, x7
    smulh x8, x9, x10
    // Signed/unsigned W/X division, same typed three-register family.
    udiv w0, w1, w2
    udiv x3, x4, x5
    sdiv w6, w7, w8
    sdiv x9, x10, x11
    // GNU shifted ALU and compare, exact data encoding for W/X.
    add x1, x1, x1, lsl #1
    add w0, w1, w2, asr #3
    sub x0, x1, x2, lsr #4
    adds x3, x4, x5, lsl #6
    subs w6, w7, w8, asr #7
    orr x0, x1, x2, ror #5
    and x0, x1, x2, lsl #6
    eor w9, w10, w11, lsr #12
    bic x12, x13, x14, ror #15
    ands w15, w16, w17, asr #1
    cmp x0, x1, asr #2
    cmn w0, w2, lsl #2
    tst x0, x1, ror #2
    // Indexed LDR/STR (X/W), sign-extension, register-extension and scale.
    ldr x0, [x1, x2]
    ldr x0, [x1, x2, lsl #3]
    ldr x0, [x1, w2, uxtw 3]
    ldr x0, [x1, w2, sxtw #3]
    ldr x0, [x1, x2, sxtx #3]
    str w0, [sp, x2, lsl #2]
    ldrb w0, [x1, w2, uxtw]
    ldrh w0, [x1, x2, lsl #1]
    ldrsw x0, [x1, x2, lsl #2]
    ldrsb w0, [x1, x2]
    ldrsb x0, [x1, x2]
    ldrsh w0, [x1, x2]
    ldrsh x0, [x1, x2]
    strh w0, [x1, w2, uxtw #1]
    // GNU MOV one-instruction wide constant: MOVZ/MOVN/ORR bitmask.
    mov x0, #4294967295
    mov x1, #68719476704
    mov x2, #281474976710656
    mov x3, #2147483647
    mov x4, #9223372036854775807
    mov x5, #65536
    mov x6, #0xffff0000
    mov w7, #0xffff0000
    mov w8, #0xffff
    mov x12, #0xffffffff00000000
    // Extended-index arithmetic: GNU SP, UXTW/SXTW/UXTX/SXTX and CMP.
    add x1, x0, w1, uxtw
    add x20, x20, w0, sxtw
    add x0, x1, w2, uxtw #3
    add x3, x4, x5, sxtx #1
    add x6, x7, x8, uxtx
    sub x9, x10, w11, sxtw #2
    adds x0, x1, w2, sxtw
    subs x4, x5, w6, uxtw #4
    cmp x0, w1, uxtw #1
    cmn x0, w1, sxtw
    add x0, sp, x1, lsl #3
    add sp, sp, x2, lsl #1
    // GNU signed loads and pre/post-index base writeback, imm9/imm12.
    ldr x2, [x1, #16]!
    ldr x2, [x1], #16
    str w3, [x4, #-8]!
    strb w5, [x6], #1
    ldrh w0, [x1, #2]!
    ldrsw x0, [x1, #16]
    ldrsw x0, [x1, #-4]
    ldrsw x0, [x1, #12]!
    ldrsw x0, [x1], #4
    ldrsb w0, [x1, #3]
    ldrsb x0, [x1, #3]
    ldrsh w0, [x1, #2]
    ldrsh x0, [x1, #2]
    // GNU A64 single-source and BIC/BICS, all W/X forms.
    clz x0, x1
    clz w2, w3
    cls x4, x5
    cls w6, w7
    rbit x8, x9
    rbit w10, w11
    rev16 x12, x13
    rev16 w14, w15
    rev32 x16, x17
    bics x0, x1, x2
    bics w3, w4, w5, lsr #7
    bics x6, x7, x8, ror #9
    bic x9, x10, #255
    // GNU signed MOV constants: one canonical MOVZ/MOVN/logical mask.
    mov x1, -4398046511104
    mov w0, -2147483648
    mov w7, -65536
    mov w0, -65537
    mov x2, -281474976710656
    mov x3, -1
    mov w4, -1
    // GNU ADR/SXTB/SXTH/EXTR and unprivileged LDTR/STTR families.
    adr x0, external_probe
    adr x3, external_adr_target
    adr x4, external_function
    sxtb x0, w1
    sxtb w0, w1
    sxth x2, w3
    sxth w2, w3
    uxtb w6, w7
    uxth w8, w9
    uxtb x10, w11
    uxth x12, w13
    ldtr x0, [x1]
    sttr x2, [sp, #8]
    ldtrb w3, [x4, #1]
    sttrb w5, [x6, #-1]
    ldtrh w7, [x8, #2]
    sttrh w9, [x10, #-2]
    ldtrsw x11, [x12, #4]
    extr x0, x1, x2, #3
    extr w3, w4, w5, #7
external_adr_target:
    // GNU complemented logical, signed pair and trap families.
    orn x0, x1, x2
    orn w3, w4, w5, lsr #7
    eon x6, x7, x8, asr #8
    eon w9, w10, w11, ror #3
    mvn x0, x1, lsl #7
    mvn w2, w3, ror #5
    ldpsw x0, x1, [x2]
    ldpsw x3, x4, [x5, #-16]!
    ldpsw x6, x7, [x8], #12
    hvc #3
    smc #7
    // GNU GCC stack-relative ADD and explicit zero/positive shift12.
    add x0, sp, x2
    add x3, sp, x4
    sub x5, sp, x6
    add x7, x8, #0, lsl 12
    add x9, sp, #0, lsl 12
    add x10, x11, #1, lsl 12
    sub x12, x13, #4, lsl 12
    adds x14, x15, #0, lsl 12
    // GNU :lo12: LDRSW is LDST32 relocation, not generic LDR X.
    ldrsw x0, [x1, #:lo12:external_data]
    ldrsw x3, [x4, #:lo12:external_data+8]
    // GNU genuine multiply-accumulate register Ra and ASR/ROR forms.
    madd x0, x1, x2, x3
    madd w4, w5, w6, w7
    msub x8, x9, x10, x11
    msub w12, w13, w14, w15
    umaddl x0, w1, w2, x3
    smaddl x4, w5, w6, x7
    umsubl x0, w1, w2, x3
    smsubl x8, w9, w10, x11
    asr w0, w1, #1
    asr x0, x1, #1
    asr x0, x1, x2
    ror x0, x1, #3
    ror x0, x1, x2
    // One typed UBFM/SBFM/BFM field model, tested W/X + GNU alias semantics.
    ubfx w0, w1, #2, #8
    ubfx x2, x3, #23, #32
    ubfiz w4, w5, #4, #12
    ubfiz x6, x7, #48, #16
    sbfx w8, w9, #10, #5
    sbfx x10, x11, #1, #60
    sbfiz w12, w13, #0, #32
    sbfiz x14, x15, #32, #32
    bfi w16, w17, #8, #12
    bfi x18, x19, #32, #16
    bfxil w20, w21, #3, #9
    bfxil x22, x23, #20, #40
    ccmp x0, xzr, #15, eq
    ccmp x1, #0, #4, ne
    ccmp w2, w3, #7, eq
    ccmn x4, #31, #15, mi
    ccmn w5, w6, #2, gt
    ccmp x7, x8, #0, ls
    ccmn w9, #15, #12, cs
    cmn x0, x1
    cmp x1, #8192
    and w1, w1, #255
    and w3, w3, #65535
    // Negative W logical masks are 32-bit sign truncations, unlike
    // oversized positive values which must remain rejected.
    and w0, w0, -2
    and w1, w1, -4
    orr w2, w2, -8
    tst w3, -2
    and w0, w1, #0xaaaaaaaa
    and x1, x2, #0x00ff00ff00ff00ff
    orr w3, w4, #4
    orr x5, x6, #0x8000000000000000
    eor w7, w8, #0x33333333
    eor x9, x10, #0x5555555555555555
    ands w11, w12, #0x1f
    tst w3, #255
    tst x4, #0x00ff00ff00ff00ff
    tst w1, w2
    tst x3, x4
    bic w5, w6, w7
    bic x8, x9, x10
    ands x11, x12, x13
    and x4, x4, #255
    and x5, x5, #4294967295
    sxtw x0, w0
    uxtw x1, w1
    rev w2, w3
    rev x5, x6
    .arch_extension lse
    cas w0, w1, [x2]
    casal x3, x4, [x5]
    ldadd w0, w1, [x2]
    ldaddal x2, x3, [x4]
    ldclrl w0, w1, [x2]
    ldsetal x0, x1, [x2]
    swp w0, w1, [x2]
    swpal x2, x3, [x4]
    stadd x0, [x2]
    staddl w0, [x2]
    stset x1, [x2]
    stclrl w0, [x2]
    // Native GNU-checked LSE EOR class, including byte/half release
    // and returned-register aliases sharing one target MCInst backend.
    ldeor w0, w1, [x2]
    ldeoral x2, x3, [x4]
    ldeorh w0, w1, [x2]
    ldeorb w0, w1, [x2]
    steor w0, [x2]
    steorl w0, [x2]
    steorh w0, [x2]
    steorlh w0, [x2]
    steorb w0, [x2]
    steorlb w0, [x2]
    // All LSE byte/halfword variants on existing typed MCInst and
    // profile-gated ISA, independently compared with GNU AS below.
    staddlh w1, [x2]
    staddlb w1, [x2]
    staddh w1, [x2]
    staddb w1, [x2]
    ldaddalh w1, w3, [x2]
    ldaddab w1, w3, [x2]
    casalh w1, w3, [x2]
    casab w1, w3, [x2]
    caslb w1, w3, [x2]
    swplh w1, w3, [x2]
    prfm pstl1strm, [x2]
    prfm pldl1keep, [x3, #16]
    prfm pldl1keep, [x0, x7]
    prfm pstl1keep, [x20, x0]
    prfm PLDL2KEEP, [x2, x0]
    prfm pldl1keep, [x0, x7, lsl #3]
    prfm pldl1keep, [x0, w7, uxtw #3]
    prfm plil2strm, [x4, #24]
    .inst 0xd503201f, 0xd65f03c0
    // GNU target raw-word list is not limited to four MC operands.
    .inst 0xd503201f, 0xd503201f, 0xd503201f, 0xd503201f, 0xd503201f, 0xd503201f
    .inst (0xd500401f | ((0) << 16 | (4) << 5) | ((!!(0)) << 8))
    .inst (0xd500401f | ((0) << 16 | (4) << 5) | ((!!(9)) << 8))
    // Linux arch/arm64 sysreg macros assemble .inst with .L__gpr_num_X
    // created by .set; validate the symbolic word against real GNU bytes.
    .set .L__gpr_num_x17, 17
    .set .L__gpr_num_x23, 23
    .inst (0xd5200000|((((3) << 19)|((0) << 16)|((0) << 12)|((0) << 8)|((5) << 5)))|(.L__gpr_num_x17))
    .inst (0xd5200000|((((3) << 19)|((5) << 16)|((1) << 12)|((0) << 8)|((0) << 5)))|(.L__gpr_num_x23))

    .arch_extension nolse
    ldxr w0, [x2]
    ldxr x1, [x2]
    ldaxr w3, [x4]
    ldaxr x5, [x6]
    stxr w7, w8, [x9]
    stxr w10, x11, [x12]
    stlxr w13, w14, [x15]
    stlxr w16, x17, [x18]
    ldar w0, [x0]
    ldar x1, [x2]
    ldarb w0, [x1]
    ldarb w2, [sp]
    ldarh w3, [x4]
    stlrb w5, [x6]
    stlrh w7, [x8]
    stlr w7, [x8]
    stlr x9, [x10]
    brk #0x800
    brk #65535
    mov w0, -22
    mov x1, -95
    mov w7, -65536
    mvn w0, w1
    mvn x2, x3
    mvn xzr, x4
    subs x2, x2, x1
    subs x3, x3, x2
    subs w0, w0, #4096
    adds x8, x8, #8192
    adds w2, w1, w0
    add x4, x4, #4096
    beq after_call
    bne after_call
    bhi after_call
    bhs after_call
    blt after_call
    bge after_call
    cset w0, eq
    cset x11, gt
    csetm x10, ne
    cinc w7, w8, lt
    bl external_function
    b external_branch
    cbz x0, external_branch
    cbnz w1, external_branch
    cbz xzr, after_call
    b.eq external_branch
    b.ne external_branch
    b.gt exit_code
    b.lt exit_code
    tbz w0, #0, external_branch
    tbnz x1, #63, external_branch
    tbz xzr, #31, exit_code
    movz x2, #4660, lsl #16
    movk x2, #22136, lsl #32
    movn x3, #255
    movk w4, #43981, lsl #16
    br x10
    blr x11
.globl addr64_probe
addr64_probe:
    adrp x0, external_data
    add x0, x0, :lo12:external_data
    ldr x1, [x0, :lo12:external_data]
    ldr x5, [x4, #:lo12:external_data]
    str w6, [x7, #:lo12:external_data+4]
    ldr w2, [x0, :lo12:external_data]
    str x1, [x0, :lo12:external_data]
    str w2, [x0, :lo12:external_data]
    ldrb w3, [x0, :lo12:external_data]
    strb w3, [x0, :lo12:external_data]
    ldrh w4, [x0, :lo12:external_data]
    strh w4, [x0, :lo12:external_data]
.globl mc_operand_probe
.type mc_operand_probe, @function
mc_operand_probe:
    mov w3, #1234
    ret x9
.size mc_operand_probe, .-mc_operand_probe
.data
.globl function_pointer
function_pointer:
    .quad after_call
    .hword 0x1234
    .xword 0x1122334455667788
    .xword after_call
    // GNU Linux exception tables encode forward label differences /4.
    .byte (.Ldiv_after - .Ldiv_start) / 4
    .2byte (.Ldiv_after - .Ldiv_start) / 4
.Ldiv_start:
    .byte 0x10,0x20,0x30,0x40,0x50,0x60,0x70,0x80
.Ldiv_after:
    .byte (.Ldiv_after - .Ldiv_start) / 4
.section ".initcall6.init", "a"
.global initcall_slot
initcall_slot:
    .long after_call - .
    .long external_function - .
.previous
SRC
"$MINIAS" --target=aarch64 -march=armv8-a -mabi=lp64 \
    -o "$work/mini.o" "$work/vertical.s"
"$AARCH64_AS" -march=armv8-a -o "$work/gnu.o" "$work/vertical.s"
"$AARCH64_READELF" -h "$work/mini.o" | grep -q 'Machine:.*AArch64'
"$AARCH64_READELF" -h "$work/mini.o" | grep -q 'Class:.*ELF64'
"$AARCH64_OBJCOPY" -j .text -O binary "$work/mini.o" "$work/mini-text.bin"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/gnu.o" "$work/gnu-text.bin"
if ! cmp "$work/mini-text.bin" "$work/gnu-text.bin"; then
    echo "A64 GNU text mismatch: MiniAS bytes near first offender" >&2
    od -An -tx1 -v -j 88 -N 80 "$work/mini-text.bin" >&2
    echo "A64 GNU text mismatch: GNU bytes same range" >&2
    od -An -tx1 -v -j 88 -N 80 "$work/gnu-text.bin" >&2
    exit 1
fi
"$AARCH64_OBJCOPY" -j .data -O binary "$work/mini.o" "$work/mini-data.bin"
"$AARCH64_OBJCOPY" -j .data -O binary "$work/gnu.o" "$work/gnu-data.bin"
cmp "$work/mini-data.bin" "$work/gnu-data.bin"
typed_a64_size="$("$AARCH64_READELF" -Ws "$work/mini.o" |
    awk '$NF == "mc_operand_probe" { print $3 }')"
test "$typed_a64_size" = "8"
echo 'MINIAS_AARCH64_PARSED_LAYOUT=PASS typed_4B=2 symbol_size=8'
"$AARCH64_READELF" -Wr "$work/mini.o" | grep -q 'R_AARCH64_CALL26'
"$AARCH64_READELF" -Wr "$work/mini.o" | grep -q 'R_AARCH64_JUMP26'
"$AARCH64_READELF" -Wr "$work/mini.o" | grep -q 'R_AARCH64_ABS64'
"$AARCH64_READELF" -Wr "$work/mini.o" | grep -q 'R_AARCH64_CONDBR19'
"$AARCH64_READELF" -Wr "$work/gnu.o" | grep -q 'R_AARCH64_CONDBR19'
"$AARCH64_READELF" -Wr "$work/mini.o" | grep -q 'R_AARCH64_TSTBR14'
"$AARCH64_READELF" -Wr "$work/gnu.o" | grep -q 'R_AARCH64_TSTBR14'
for reloc in R_AARCH64_ADR_PREL_PG_HI21 R_AARCH64_ADD_ABS_LO12_NC \
    R_AARCH64_LDST8_ABS_LO12_NC R_AARCH64_LDST16_ABS_LO12_NC \
    R_AARCH64_LDST32_ABS_LO12_NC R_AARCH64_LDST64_ABS_LO12_NC; do
    "$AARCH64_READELF" -Wr "$work/mini.o" | grep -q "$reloc"
    "$AARCH64_READELF" -Wr "$work/gnu.o" | grep -q "$reloc"
done
cat >"$work/external.s" <<'EXTERNAL'
.text
.globl external_function
external_function:
    ret
.globl external_branch
external_branch:
    ret
.data
.p2align 3
.globl external_data
external_data:
    .quad 0
EXTERNAL
"$AARCH64_AS" -o "$work/external.o" "$work/external.s"
"$AARCH64_LD" -static -e _start -o "$work/mini.elf" "$work/mini.o" "$work/external.o"
"$QEMU_AARCH64" "$work/mini.elf"
# Type-and-size sensitive AArch64 integer/memory matrix: compare with GNU AS
# rather than trusting constants or treating successful ELF output as enough.
cat >"$work/integer-matrix.s" <<'A64_INT'
.arch armv8-a
.text
.globl integer_matrix
integer_matrix:
    mov x0, x1
    mov x29, sp
    mov sp, x29
    mov x2, 51711
    add x0, x0, x2
    add sp, sp, #16
    sub w2, w2, #1
    cmp x0, x1
    cmp w0, #5
    and x0, x0, x2
    orr w0, w1, w2
    eor x0, x0, x1
    ldr x2, [x0, #8]
    ldr w2, [sp, #12]
    str w1, [sp, #16]
    ldrb w1, [x0]
    strb w1, [x0, #1]
    ldrh w1, [x0, #2]
    strh w1, [x0, #4]
    // ARM signed byte-granular simm9 uses LDUR/STUR even if LDR mnemonic
    // chooses the unscaled addressing encoding in GNU GAS.
    ldur x0, [x1, #-8]
    stur w2, [sp, #-1]
    ldurb w3, [x4, #-3]
    sturb w5, [x6, #7]
    ldurh w7, [x8, #-2]
    sturh w9, [x10, #3]
    ldr x11, [x12, #-8]
    str w13, [sp, #-4]
    ldr w14, [x15, #3]
    stp x29, x30, [sp, -16]!
    ldp x29, x30, [sp], 16
    ret
A64_INT
"$MINIAS" --target=aarch64 -march=armv8-a -mabi=lp64 -o "$work/int-mini.o" "$work/integer-matrix.s"
"$AARCH64_AS" -march=armv8-a -o "$work/int-gnu.o" "$work/integer-matrix.s"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/int-mini.o" "$work/int-mini.bin"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/int-gnu.o" "$work/int-gnu.bin"
cmp "$work/int-mini.bin" "$work/int-gnu.bin"
for bad in 'add x0, w0, x1' 'cmp x0, #4097' 'ldr x0, [x1, -257]' \
           'stp x29, x30, [sp, -520]!' 'mov x0, #0x123456789abcdef0' \
           'ldaxrh x2,[x3]' 'ldxrb x2,[x3]' \
           'stlxrh w4,x2,[x3]' 'stxrb x4,w2,[x3]' \
           'stnp x2,x3,[x0,#-520]' 'stp q0,x1,[x0]' 'ldp q0,q1,[x0,#-1040]' \
           'ldnp q0,q1,[x0],16' 'ld1 {v0.16b}, [x2,#16]' \
           'ld1 {v0.16b,v2.16b},[x2]' \
           'ldnp x2,x3,[x0],8' \
           'ldnp x2,w3,[x0]' 'ubfm w2,w3,#32,#0' 'sbfm x2,x3,#0,#64' \
           'bfm x2,w3,#1,#2' 'movk x0,#65536' \
           'stnp w2,w3,[x0,#254]' \
           'mrs x2,dbgwcr16_el1' 'msr dbgbcr99_el1,x1' \
           'mrs x0,s4_3_c1_c0_1' 'msr s0_8_c1_c0_1,x0' \
           'mrs x0,s0_3_c16_c0_1' 'mrs x0,s0_3_c1_c0_8' \
           'mrs x0,s0_3_c1_c0_1oops' \
           'mrs x0,oslar_el1' 'msr oslsr_el1,x0' \
           'msr revidr_el1,x0' 'msr pmceid1_el0,x0' \
           'msr aidr_el1,x0' 'msr pmceid0_el0,x0' \
           'mrs x0,pmevtyper31_el0' 'mrs x0,pmevcntr31_el0' \
           'msr dbgauthstatus_el1,x0' \
           'neg x0,x1,ror 2' 'neg x0,x1,lsl 64' \
           'negs w0,w1,asr -1' \
           'mrs w0,pmselr_el0' 'msr dacr32_el2,w0' \
           'tlbi alle1is,x1' 'ld1 {v1.4s},[x0],#8' \
           'st1 {v5.8b},[x0],#16' 'ld1 {v1.4s},[x0],sp' \
           'at s1e1r,w2' 'at s1e1r' 'at unknown,x2' \
           'mrs x0,unknown_el1'; do
    printf '.arch armv8-a\n.text\n%s\n' "$bad" >"$work/bad-integer.s"
    if "$MINIAS" --target=aarch64 -o "$work/bad-integer.o" \
         "$work/bad-integer.s" >/dev/null 2>"$work/bad-integer.err"; then
        echo "MiniAS unexpectedly accepted invalid AArch64 integer operand: $bad" >&2
        exit 1
    fi
done
echo 'MINIAS_AARCH64_INTEGER_MATRIX=PASS gnu_text_bytes=equal instruction_families=11 strict_invalid=5'

# Invalid ISA/ABI and unsupported instructions must fail, not silently be
# assembled as RISC-V bytes or advertise a false ELF machine.
for option in '-march=rv64gc' '-mabi=ilp32' '-march=armv9-a'; do
    if "$MINIAS" --target=aarch64 "$option" -o "$work/bad.o" \
        "$work/vertical.s" >"$work/bad.stdout" 2>"$work/bad.stderr"; then
        echo "AARCH64_VSLICE=FAIL accepted invalid $option" >&2
        exit 1
    fi
done
printf '.arch armv9-a\n.text\nnop\n' >"$work/unsupported-arch.s"
if "$MINIAS" --target=aarch64 -o "$work/unsupported-arch.o" \
    "$work/unsupported-arch.s" >"$work/unsupported-arch.out" \
    2>"$work/unsupported-arch.err"; then
    echo 'AARCH64_VSLICE=FAIL accepted unsupported .arch feature' >&2
    exit 1
fi
grep -q 'unsupported-aarch64-architecture' "$work/unsupported-arch.err"
printf '.text\nfadd d0, d0, d1\n' >"$work/unsupported.s"
if "$MINIAS" --target=aarch64 -o "$work/unsupported.o" \
    "$work/unsupported.s" >"$work/bad.stdout" 2>"$work/bad.stderr"; then
    echo 'AARCH64_VSLICE=FAIL accepted unsupported instruction' >&2
    exit 1
fi
# MCInst must be validated while parsing, independent of later emit/ELF.
for bad_source in 'ldxr x0, [x1,#4]' 'stxr x0, x1, [x2]' \
                  'stlxr w0, w1, [x2,#4]' \
                  'brk #65536' 'mov w0, -4294967296' \
                  'subs x0, w0, x1' 'adds x0, x1, #4097' \
                  'cset sp, eq' 'cinc w0, x1, eq' \
                  'cset x0, bogus' \
                  'csel x0, w1, x2, eq' \
                  'csel sp, x1, x2, eq' \
                  'csinc x0, x1, x2, bad' \
                  'cneg x0,w1,eq' 'cneg x0,x1,bad' \
                  'cinv w0,x1,ne' \
                  'mrs w0, sp_el0' 'msr clidr_el1, x0' \
                  'mrs x0, fake_el0' 'dmb #16' 'dmb invalid' \
                  'lsl x0, x1, #64' \
                  'lsr w0, w1, #-1' \
                  'lsl x0, w1, #1' \
                  'lsr w0, w1, x2' \
                  'wx0 .req bogus' \
                  'SHASH .req z32' 'dga .req q33' \
                  'mov v0.16b,v1.8b' 'mov v0.4s,v1.4s' \
                  'ccmp x0, w1, #1, eq' \
                  'ccmp x0, #32, #1, eq' \
                  'ccmp x0, #-1, #1, eq' \
                  'ccmp x0, #1, #16, eq' \
                  'ccmn x0, #1, #1, bogus' \
                  'mrs w0, tpidr_el1' 'msr daif, w0' \
                  'msr clidr_el1, x0' 'msr ctr_el0, x1' \
                  'mrs x0, made_up_el2' \
                  'msr daifset, #16' 'msr daifclr, #-1' \
                  'ldarb x1, [x2]' 'stlrh x3, [x4]' \
                  'mvn x0, w1' \
                  'neg x0, w1' \
                  'adc x0, w1, x2' \
                  'sbc w0, x1, w2' \
                  'umull x0, x1, w2' \
                  'smull w0, w1, w2' \
                  'umulh x0, w1, x2' \
                  'mul w0, w1, x2' \
                  'udiv x0, w1, x2' \
                  'sdiv w0, x1, w2' \
                  'udiv sp, x1, x2' \
                  'ubfx w0, w1, #32, #1' \
                  'ubfx w0, w1, #31, #2' \
                  'ubfx x0, x1, #0, #0' \
                  'ubfiz x0, x1, #48, #17' \
                  'sbfx x0, w1, #1, #2' \
                  'bfi w0, sp, #1, #2' \
                  'bfxil x0, x1, #1, #64' \
                  'sbfiz x0, x1, #-1, #8' \
                  'add x0, x1, x2, ror #1' \
                  'add w0, w1, w2, lsl #32' \
                  'and x0, x1, x2, ror #64' \
                  'cmp x0, x1, asr #-1' \
                  'sub x0, w1, x2, lsl #1' \
                  'tst w1, w2, ror #32' \
                  'ldr x0, [x1, w2]' \
                  'ldr x0, [x1, w2, lsl #3]' \
                  'ldr x0, [x1, x2, uxtw #3]' \
                  'ldr x0, [x1, x2, lsl #2]' \
                  'ldr w0, [x1, x2, lsl #3]' \
                  'ldrb x0, [x1, x2]' \
                  'ldrsw w0, [x1, x2]' \
                  'str x0, [x1, sp]' \
                  'ldr x0, [w1, x2]' \
                  'mov w0, -4294967296' \
                  'mov w0, #4294967296' \
                  'mov x0, #0x123456789abcdef0' \
                  'mov x0, #18446744073709551616' \
                  'add x0, x1, w2, uxtw #5' \
                  'add x0, x1, x2, uxtw' \
                  'add w0, w1, x2, sxtx' \
                  'add x0, x1, w2, sxtx' \
                  'add x0, x1, x2, sxtw' \
                  'cmp x0, w2, sxtw #5' \
                  'adds sp, x1, w2, uxtw' \
                  'sub x0, w1, w2, uxtw' \
                  'ldr x0, [x1, #256]!' \
                  'ldr x0, [x1, #16]!, #1' \
                  'ldrsw w0, [x1]' \
                  'ldrsb x0, [x1, #256]!' \
                  'ldursw x0, [x1], #4' \
                  'str w0, [sp], #-257' \
                  'ldr x0, [x1], #256' \
                  'umaddl x0, x1, w2, x3' \
                  'smaddl w0, w1, w2, x3' \
                  'madd x0, x1, w2, x3' \
                  'msub w0, w1, w2, x3' \
                  'umsubl x0, w1, w2, w3' \
                  'asr x0, x1, #64' \
                  'ror w0, w1, #32' \
                  'ror x0, w1, x2' \
                  'clz x0, w1' \
                  'rbit w0, x1' \
                  'cls sp, x0' \
                  'rev32 w0, w1' \
                  'bics x0, w1, x2' \
                  'bics x0, x1, x2, lsl #64' \
                  'bic x0, x1, #0' \
                  'bics x0, x1, #0xffffffffffffffff' \
                  'bics w11, w12, #255' \
                  'sxtb x0, x1' \
                  'sxth w0, x1' \
                  'uxtb w0, x1' \
                  'ldtr x0, [x1, #256]' \
                  'ldtr w0, [x1, #-257]' \
                  'sttrh x0, [x1]' \
                  'ldtrsw w0, [x1]' \
                  'sttr x0, [x1], #4' \
                  'extr x0, x1, x2, #64' \
                  'extr w0, w1, w2, #-1' \
                  'extr w0, x1, w2, #1' \
                  'adr w0, integer_matrix' \
                  'orn x0, x1, #5' \
                  'eon x0, w1, x2' \
                  'mvn x0, w1, lsl #2' \
                  'mvn x0, x1, lsl #64' \
                  'ldpsw w0, w1, [x2]' \
                  'ldpsw x0, x1, [x2,#512]' \
                  'ldpsw x0, x1, [x2],#256' \
                  'hvc #65536' \
                  'smc #-1' \
                  'add x0,sp,#4096,lsl 12' \
                  'add x0,sp,#1,lsl 11' \
                  'ldrsw w0,[x1,#:lo12:fg_console]' \
                  'ldrsw x0,[w1,#:lo12:fg_console]' \
                  'adds sp,x1,#0,lsl 12' \
                  'msr CurrentEL, x0' \
                  'msr dczid_el0, x1' \
                  'msr id_aa64dfr0_el1, x2' \
                  'msr cntvct_el0, x3' \
                  'mrs w0, sctlr_el1' \
                  'mrs x0, made_up_el3' \
                  'tlbi vae1, w0' \
                  'tlbi vmalle1, x0' \
                  'dc civac, w0' \
                  'ic ivau, w1' \
                  'dc unknown, x0' \
                  'tlbi bogus, x0' \
                  'tlbi rvae1is, x9' \
                  'tlbi rvale1is, x18' \
                  'ic ialluis, x0' \
                  'prfm pldl1keep,[w0,x1]' \
                  'prfm pldl1keep,[x0,w1]' \
                  'prfm pldl1keep,[x0,x1,lsl #2]' \
                  'prfm junk,[x0,x1]' \
                  'yield #1' \
                  'hint #.Lnever_defined' \
                  '.equ .Lbad_hint,128; hint #.Lbad_hint' \
                  'bad_label: nop; hint #bad_label' \
                  'dsb #16' 'dsb bogus' 'isb ish' 'isb #16' \
                  'prfm pstl4strm, [x2]' 'prfm pstl1strm, [w2]' \
                  '.inst 0x100000000' \
                  '.inst 0xd503201f, ' \
                  '.inst 0xd503201f, 0x100000000' \
                  '.inst .Lnonexistent_sysreg' \
                  'cmn x0, #-1' 'cmn w0, #4097' \
                  'and w0, w0, #0xffffffff' 'and w0, w0, #0' \
                  'and w0, w1, #0x100000001' \
                  'and w0, w1, -4294967297' \
                  'orr w0, w1, #0' 'eor x0, x1, #0xffffffffffffffff' \
                  'tst w0, #0' 'tst w0, x1' \
                  'bic w0, x1, w2' \
                  'and x0, w0, #255' 'sxtw w0, w0' \
                  'uxtw x0, x0' 'rev w1, x2' \
                  'ldar w0, [x0, #4]' 'stlr x0, [w0]' \
                  'ldur x0, [x1, #-257]' 'stur w2, [sp, #256]' \
                  'ldurh x0, [x1, #-2]' \
                  'cmp x0, #16777216' \
                  'hint #-1' 'hint #128' 'hint #18446744073709551616' \
                  'hint bad' 'ret x31' 'mov x2, #0x123456789abcdef0' 'b external_function + 2' \
                  'cbz sp, external_function' 'cbnz x0, external_function+2' \
                  'tbz w0, #32, external_function' \
                  'tbnz x0, #64, external_function' \
                  'b.invalid external_function' \
                  'movk w0, #1, lsl #32' \
                  'br w0' \
                  'adrp w0, external_data' \
                  'add x0, x0, :lo12:' \
                  'ldr x0, [w1, :lo12:external_data]' \
                  'ldrb x0, [x1, :lo12:external_data]'; do
    printf '.text\n%s\n' "$bad_source" >"$work/invalid-mcinst.s"
    if "$MINIAS" --target=aarch64 -o "$work/invalid-mcinst.o" \
        "$work/invalid-mcinst.s" >"$work/invalid-mcinst.out" \
        2>"$work/invalid-mcinst.err"; then
        echo "MiniAS accepted invalid AArch64 MCInst: $bad_source" >&2
        exit 1
    fi
done
# Only RISC-V's selected ISA gate forbids raw .word in executable
# sections. Keep this acceptance target-owned, not an accidental common
# assembler restriction on AArch64.
printf '.text\n.globl raw_a64\nraw_a64:\n.word 0xd503201f\n' \
    >"$work/aarch64-raw.s"
"$MINIAS" --target=aarch64 -o "$work/aarch64-raw.o" "$work/aarch64-raw.s"
"$AARCH64_AS" -o "$work/aarch64-raw-gnu.o" "$work/aarch64-raw.s"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/aarch64-raw.o" \
    "$work/aarch64-raw.bin"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/aarch64-raw-gnu.o" \
    "$work/aarch64-raw-gnu.bin"
cmp "$work/aarch64-raw.bin" "$work/aarch64-raw-gnu.bin"
echo 'MINIAS_AARCH64_MCINST=PASS typed_reg64_reg32_imm16_symbol=1 invalid_operands=3 target_raw_data=1'

# DWARF debug_frame must contain a real CIE/FDE and parsed state,
# not merely accept .cfi directives while silently deleting unwind data.
cat >"$work/cfi-debug.s" <<'A64_CFI'
.arch armv8-a
.text
.globl cfi_one
.type cfi_one, %function
.cfi_sections .debug_frame
cfi_one:
    .cfi_startproc
    stp x29, x30, [sp, -16]!
    .cfi_def_cfa_offset 16
    .cfi_offset 29, -16
    .cfi_offset 30, -8
    .cfi_remember_state
    .cfi_window_save
    nop
    .cfi_restore_state
    ldp x29, x30, [sp], 16
    .cfi_restore 29
    .cfi_restore 30
    .cfi_def_cfa_offset 0
    ret
    .cfi_endproc
.size cfi_one, .-cfi_one
A64_CFI
"$MINIAS" --target=aarch64 -o "$work/cfi-mini.o" "$work/cfi-debug.s"
"$AARCH64_AS" -o "$work/cfi-gnu.o" "$work/cfi-debug.s"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/cfi-mini.o" "$work/cfi-mini.bin"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/cfi-gnu.o" "$work/cfi-gnu.bin"
cmp "$work/cfi-mini.bin" "$work/cfi-gnu.bin"
"$AARCH64_READELF" --debug-dump=frames "$work/cfi-mini.o" >"$work/cfi-mini.frame"
"$AARCH64_READELF" --debug-dump=frames "$work/cfi-gnu.o" >"$work/cfi-gnu.frame"
for marker in 'CIE' 'FDE' 'DW_CFA_def_cfa_offset: 16'               'DW_CFA_offset: r29' 'DW_CFA_restore: r29'; do
    grep -q "$marker" "$work/cfi-mini.frame"
    grep -q "$marker" "$work/cfi-gnu.frame"
done
"$AARCH64_READELF" -Wr "$work/cfi-mini.o" | grep -q 'R_AARCH64_ABS64'
"$AARCH64_READELF" -Wr "$work/cfi-gnu.o" | grep -q 'R_AARCH64_ABS64'
for directive in '.cfi_sections .eh_frame' '.cfi_startproc simple'                  '.cfi_unsupported 1'; do
    printf '.arch armv8-a\n.text\n.cfi_sections .debug_frame\n%s\n' "$directive"         >"$work/cfi-reject.s"
    if "$MINIAS" --target=aarch64 -o "$work/cfi-reject.o"        "$work/cfi-reject.s" >/dev/null 2>&1; then
        echo "CFI invalid input accepted: $directive" >&2
        exit 1
    fi
done
echo 'MINIAS_AARCH64_DWARF_DEBUG_FRAME=PASS actual_CIE_FDE=1 relocation=1 state_ops=1'


# Precise GNU data bytes are the oracle for common variable-width LEB
# directives, AArch aliases and exact APP/NO_APP boundaries.
cat >"$work/common-data.s" <<'GNU_DATA'
.arch armv8-a
.text
.globl data_source_entry
data_source_entry:
#APP
    nop
#NO_APP
    ret
.data
    .xword 0x123456789
    .hword 0x1234
    .uleb128 0
    .uleb128 127
    .uleb128 624485
    .sleb128 -624485
    .sleb128 -1
GNU_DATA
"$MINIAS" --target=aarch64 -o "$work/data-mini.o" "$work/common-data.s"
"$AARCH64_AS" -o "$work/data-gnu.o" "$work/common-data.s"
for section in .text .data; do
    "$AARCH64_OBJCOPY" -j "$section" -O binary "$work/data-mini.o" "$work/data-mini.bin"
    "$AARCH64_OBJCOPY" -j "$section" -O binary "$work/data-gnu.o" "$work/data-gnu.bin"
    cmp "$work/data-mini.bin" "$work/data-gnu.bin"
done
echo 'MINIAS_COMMON_LEB_GNU=PASS uleb_sleb=byte_exact hword_xword=byte_exact app_no_app=correct'


# Real DWARF line program must retain source locations and one PC relocation,
# rather than dropping .loc directives while succeeding on machine bytes.
cat >"$work/debug-line.s" <<'DBG_LINE'
.arch armv8-a
.file 1 "/tmp/source.c"
.text
.globl debug_line_entry
debug_line_entry:
    .loc 1 10 3 is_stmt 1
    nop
    .loc 1 12 1 is_stmt 0
    ret
DBG_LINE
"$MINIAS" --target=aarch64 -o "$work/line-mini.o" "$work/debug-line.s"
"$AARCH64_AS" -o "$work/line-gnu.o" "$work/debug-line.s"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/line-mini.o" "$work/line-mini.bin"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/line-gnu.o" "$work/line-gnu.bin"
cmp "$work/line-mini.bin" "$work/line-gnu.bin"
"$AARCH64_READELF" --debug-dump=decodedline "$work/line-mini.o" >"$work/line-mini.txt"
"$AARCH64_READELF" --debug-dump=decodedline "$work/line-gnu.o" >"$work/line-gnu.txt"
for needle in 'source.c' ' 10 ' ' 12 '; do
    grep -q "$needle" "$work/line-mini.txt"
    grep -q "$needle" "$work/line-gnu.txt"
done
"$AARCH64_READELF" -Wr "$work/line-mini.o" | grep -q R_AARCH64_ABS64
echo 'MINIAS_COMMON_DWARF_DEBUG_LINE=PASS real_address_fixup=1 file_line_rows=2'
"$AARCH64_READELF" -Wr "$work/mini.o" | grep -q 'R_AARCH64_PREL32'
"$AARCH64_READELF" -Wr "$work/gnu.o" | grep -q 'R_AARCH64_PREL32'
"$AARCH64_READELF" -Wr "$work/mini.o" | grep -q 'external_function'
"$AARCH64_OBJCOPY" -j .initcall6.init -O binary "$work/mini.o" "$work/prel-mini.bin"
"$AARCH64_OBJCOPY" -j .initcall6.init -O binary "$work/gnu.o" "$work/prel-gnu.bin"
cmp "$work/prel-mini.bin" "$work/prel-gnu.bin"
echo 'MINIAS_COMMON_DOT_PREL32=PASS aarch64_cross_section_initcall=1 target_relocation=261'
# Validate LSE ISA extension is a real typed feature gate.
cat >"$work/lse-disabled.s" <<'LSE_DISABLED'
.arch armv8-a
.text
cas w0, w1, [x2]
LSE_DISABLED
if "$MINIAS" --target=aarch64 -o "$work/lse-disabled.o" "$work/lse-disabled.s" >/dev/null 2>&1; then
    echo "LSE without .arch_extension unexpectedly accepted" >&2
    exit 1
fi
cat >"$work/lse-enabled.s" <<'LSE_ENABLED'
.arch armv8-a
.arch_extension lse
.text
cas w0, w1, [x2]
.arch_extension nolse
ldar w0,[x2]
LSE_ENABLED
"$MINIAS" --target=aarch64 -o "$work/lse-mini.o" "$work/lse-enabled.s"
"$AARCH64_AS" -o "$work/lse-gnu.o" "$work/lse-enabled.s"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/lse-mini.o" "$work/lse-mini.bin"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/lse-gnu.o" "$work/lse-gnu.bin"
cmp "$work/lse-mini.bin" "$work/lse-gnu.bin"

# Subword LSE always takes W data registers, regardless of base width;
# invalid suffix and size must not silently encode a 64-bit RMW.
for bad in 'staddlh x1,[x2]' 'casalh x1,x3,[x2]' \
           'ldaddlb w1,x3,[x2]' 'staddalh w1,[x2]' 'steorlh x1,[x2]' \
           'ldeorbh x1,x2,[x3]'; do
    printf '.arch armv8-a\n.arch_extension lse\n.text\n%s\n' "$bad" > "$work/lse-subword-invalid.s"
    if "$MINIAS" --target=aarch64 -o "$work/lse-subword-invalid.o" \
         "$work/lse-subword-invalid.s" >/dev/null 2>"$work/lse-subword-invalid.err"; then
        echo "invalid AArch64 subword LSE accepted: $bad" >&2
        exit 1
    fi
done

# GNU Linux AArch64 :got: and :got_lo12: have distinct semantic ELF
# relocations (311/312), not ordinary ADRP page+LDR LO12 (275/286).
# Verify machine bytes AND linker-visible relocation type/name/addend.
cat > "$work/a64-got-family.s" <<'A64_GOT_FAMILY'
.arch armv8-a
.text
.globl got_entry
got_entry:
    adrp x2, :got:__start_notes
    ldr x2, [x2, #:got_lo12:__start_notes]
    adrp x9, :got:other_external
    ldr x9, [x9, #:got_lo12:other_external]
    adrp x4, ordinary_symbol
    ldr x4, [x4, #:lo12:ordinary_symbol]
A64_GOT_FAMILY
"$MINIAS" --target=aarch64 -o "$work/got-mini.o" "$work/a64-got-family.s"
"$AARCH64_AS" -o "$work/got-gnu.o" "$work/a64-got-family.s"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/got-mini.o" "$work/got-mini.bin"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/got-gnu.o" "$work/got-gnu.bin"
cmp "$work/got-mini.bin" "$work/got-gnu.bin"
"$AARCH64_READELF" -Wr "$work/got-mini.o" > "$work/got-mini-rel.txt"
"$AARCH64_READELF" -Wr "$work/got-gnu.o" > "$work/got-gnu-rel.txt"
for rel in R_AARCH64_ADR_GOT_PAGE R_AARCH64_LD64_GOT_LO12_NC \
           R_AARCH64_ADR_PREL_PG_HI21 R_AARCH64_LDST64_ABS_LO12_NC; do
    grep -q "$rel" "$work/got-mini-rel.txt"
    grep -q "$rel" "$work/got-gnu-rel.txt"
done
test "$(grep -c 'R_AARCH64_ADR_GOT_PAGE' "$work/got-mini-rel.txt")" -eq 2
test "$(grep -c 'R_AARCH64_LD64_GOT_LO12_NC' "$work/got-mini-rel.txt")" -eq 2
for bad in 'adrp w2, :got:ext' 'adrp x2, :got:' \
           'ldr w0, [x1, #:got_lo12:ext]' \
           'str x0, [x1, #:got_lo12:ext]'; do
    printf '.arch armv8-a\n.text\n%s\n' "$bad" > "$work/got-invalid.s"
    if "$MINIAS" --target=aarch64 -o "$work/got-invalid.o" "$work/got-invalid.s" >/dev/null 2>&1; then
        echo "invalid A64 GOT form accepted: $bad" >&2; exit 1
    fi
done
echo 'MINIAS_AARCH64_GOT=PASS GNU_BYTES_311_312_and_page_LO12'
echo 'MINIAS_AARCH64_LSE=PASS GNU_BYTES=equal enabled_gate=1 disabled_gate=1'



# GNU optional fill/max-skip common fragment semantics. Unlike a parse-only
# gate, the two object sections must match the GNU bytes after layout.
cat >"$work/align-meta.s" <<'GNU_ALIGN'
.arch armv8-a
.text
    nop
    .p2align 4,,7
    nop
    .p2align 4
    nop
.data
    .byte 0x55
    .p2align 3,0xa5,2
    .byte 0xaa
    .p2align 3,0xb6
    .byte 0x33
GNU_ALIGN
"$MINIAS" --target=aarch64 -o "$work/align-mini.o" "$work/align-meta.s"
"$AARCH64_AS" -o "$work/align-gnu.o" "$work/align-meta.s"
for sect in .text .data; do
    "$AARCH64_OBJCOPY" -j "$sect" -O binary "$work/align-mini.o" "$work/am.bin"
    "$AARCH64_OBJCOPY" -j "$sect" -O binary "$work/align-gnu.o" "$work/ag.bin"
    cmp "$work/am.bin" "$work/ag.bin"
done
echo 'MINIAS_COMMON_GNU_ALIGN=PASS optional_fill_maxskip=gnu_byte_equal'

# Verify GNU v8.5-a implies native LSE, while v8+crypto resets it.
# Unimplemented ISA opcodes must still fail independently of .arch syntax.
cat >"$work/arch-profiles.s" <<'A64_ARCH_PROFILES'
.arch armv8.5-a
.text
.globl profile_entry
profile_entry:
    cas x0, x1, [x2]
    swp x3, x4, [x5]
    tlbi rvae1is, x9
    tlbi rvale1is, x18
    tlbi rvae1, x9
    tlbi rvale1, x18
    tlbi vale2is, x0
    tlbi ripas2e1is, x3
    tlbi ripas2le1is, x3
    tlbi ripas2e1, x3
    tlbi ripas2le1, x3
.arch armv8-a+crypto
    nop
    .inst 0xd503201f, 0xd503201f, 0xd503201f, 0xd503201f, 0xd503201f
A64_ARCH_PROFILES
"$MINIAS" --target=aarch64 -o "$work/profile-mini.o" "$work/arch-profiles.s"
"$AARCH64_AS" -o "$work/profile-gnu.o" "$work/arch-profiles.s"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/profile-mini.o" "$work/profile-mini.bin"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/profile-gnu.o" "$work/profile-gnu.bin"
cmp "$work/profile-mini.bin" "$work/profile-gnu.bin"
echo 'MINIAS_AARCH64_PROFILE=PASS GNU_armv8_5_lse_crypto_profile_and_inst_lists'
# Shared GNU macro vararg and blank tests (also used by Linux arm64).
cat >"$work/a64-vararg.s" <<'A64_VARARG'
.arch armv8-a
.data
.macro emit first, rest:vararg
    .byte \first
    .ifnb \rest
       .byte \rest
    .endif
.endm
emit 1,2,3,4
emit 5
.ifb
    .byte 6
.endif
.ifnb xyz
    .byte 7
.endif
A64_VARARG
"$MINIAS" --target=aarch64 -o "$work/vararg-mini.o" "$work/a64-vararg.s"
"$AARCH64_AS" -o "$work/vararg-gnu.o" "$work/a64-vararg.s"
"$AARCH64_OBJCOPY" -j .data -O binary "$work/vararg-mini.o" "$work/vararg-mini.bin"
"$AARCH64_OBJCOPY" -j .data -O binary "$work/vararg-gnu.o" "$work/vararg-gnu.bin"
cmp "$work/vararg-mini.bin" "$work/vararg-gnu.bin"
echo 'MINIAS_COMMON_MACRO_VARARG_IFB=PASS GNU_BYTES'
# Local symbol differences are absolute even in a different output
# section. GNU's .byte/.long/.quad are checked from the same source.
cat >"$work/local-diff.s" <<'A64_DIFF'
.arch armv8-a
.text
.Ldelta_start:
    nop
.Ldelta_end:
    nop
.globl global_delta_start
global_delta_start:
    nop
.weak weak_delta_end
weak_delta_end:
    nop
.data
.local delta_fields
delta_fields:
    .byte .Ldelta_end-.Ldelta_start
    .long .Ldelta_end-.Ldelta_start
    .quad .Ldelta_end-.Ldelta_start
    .quad weak_delta_end-global_delta_start
    .long weak_delta_end-global_delta_start
A64_DIFF
"$MINIAS" --target=aarch64 -o "$work/diff-mini.o" "$work/local-diff.s"
"$AARCH64_AS" -o "$work/diff-gnu.o" "$work/local-diff.s"
"$AARCH64_OBJCOPY" -j .data -O binary "$work/diff-mini.o" "$work/diff-mini.bin"
"$AARCH64_OBJCOPY" -j .data -O binary "$work/diff-gnu.o" "$work/diff-gnu.bin"
cmp "$work/diff-mini.bin" "$work/diff-gnu.bin"
echo 'MINIAS_COMMON_SYMBOL_DIFF=PASS local_cross_section_byte_long_quad=gnu_equal'

# AArch64 PREL64/PREL32 data relocations and local anchor differences
# use one common S+A-P fixup, not a target-local expression engine.
cat >"$work/a64-pcrel-diff.s" <<'A64_PCREL_DIFF'
.arch armv8-a
.data
.globl a64_pcrel_base
a64_pcrel_base:
.Lpcrel_a:
    .quad external_pcrel_target - .Lpcrel_a
    .quad external_pcrel_target - . + 8
.Lpcrel_b:
    .long external_pcrel_target - .Lpcrel_b
    .long external_pcrel_target - .
    .quad external_pcrel_target - .Lpcrel_b + 11
.globl global_pcrel_anchor
global_pcrel_anchor:
    .quad external_pcrel_target - global_pcrel_anchor
.weak weak_pcrel_anchor
weak_pcrel_anchor:
    .quad external_pcrel_target - weak_pcrel_anchor
    .quad external_pcrel_target - (.Lpcrel_b+11)
A64_PCREL_DIFF
"$MINIAS" --target=aarch64 -o "$work/a64-pcrel-mini.o" "$work/a64-pcrel-diff.s"
"$AARCH64_AS" -o "$work/a64-pcrel-gnu.o" "$work/a64-pcrel-diff.s"
"$AARCH64_OBJCOPY" -j .data -O binary "$work/a64-pcrel-mini.o" "$work/a64-pcrel-mini.bin"
"$AARCH64_OBJCOPY" -j .data -O binary "$work/a64-pcrel-gnu.o" "$work/a64-pcrel-gnu.bin"
cmp "$work/a64-pcrel-mini.bin" "$work/a64-pcrel-gnu.bin"
readelf -Wr "$work/a64-pcrel-mini.o" >"$work/a64-pcrel-mini.rel"
readelf -Wr "$work/a64-pcrel-gnu.o" >"$work/a64-pcrel-gnu.rel"
for flavor in mini gnu; do
    test "$(grep -c 'R_AARCH64_PREL64.*external_pcrel_target' "$work/a64-pcrel-$flavor.rel")" -eq 6
    test "$(grep -c 'R_AARCH64_PREL32.*external_pcrel_target' "$work/a64-pcrel-$flavor.rel")" -eq 2
done
for flavor in mini gnu; do
    readelf -Wr "$work/a64-pcrel-$flavor.o" |
        awk '$3 ~ /^R_AARCH64_PREL(32|64)$/ {print $1,$3,$5,$6,$7}' |
        LC_ALL=C sort >"$work/a64-pcrel-$flavor.normalized"
done
diff -u "$work/a64-pcrel-gnu.normalized" "$work/a64-pcrel-mini.normalized"
echo 'MINIAS_COMMON_A64_PCREL64=PASS GNU_DATA_RELOC_TYPE_ADDEND_AND_BINDING'

# ARMv8 ISA profiles, CRC32/CRC32C eight width forms and all AES
# pair-register instructions are target-owned, typed, feature-gated.
# GNU native AS supplies exact bytes, not guessed opcodes or success only.
cat > "$work/a64-crc-aes.s" <<'A64_CRC_AES'
.arch armv8-a+crypto
.text
aese v0.16b,v1.16b
aesd v2.16b,v3.16b
aesmc v4.16b,v5.16b
aesimc v6.16b,v7.16b
.arch armv8-a+crc
crc32b w0,w1,w2
crc32h w3,w4,w5
crc32w w6,w7,w8
crc32x w9,w10,x11
crc32cb w12,w13,w14
crc32ch w15,w16,w17
crc32cw w18,w19,w20
crc32cx w21,w22,x23
mrs x0,pmcr_el0
msr pmcr_el0,x0
mrs x1,pmcntenclr_el0
msr pmcntenclr_el0,x1
A64_CRC_AES
"$MINIAS" --target=aarch64 -o "$work/a64-crc-aes-mini.o" "$work/a64-crc-aes.s"
"$AARCH64_AS" -o "$work/a64-crc-aes-gnu.o" "$work/a64-crc-aes.s"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/a64-crc-aes-mini.o" "$work/a64-crc-aes-mini.bin"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/a64-crc-aes-gnu.o" "$work/a64-crc-aes-gnu.bin"
cmp "$work/a64-crc-aes-mini.bin" "$work/a64-crc-aes-gnu.bin"
for invalid in 'aese v0.8b,v1.8b' 'crc32x x0,w1,x2' \
               'crc32x w0,w1,w2' 'crc32w w0,w1,x2'; do
    printf '.arch armv8-a+crypto\n.arch_extension crc\n.text\n%s\n' "$invalid" > "$work/a64-crc-aes-bad.s"
    if "$MINIAS" --target=aarch64 -o "$work/a64-crc-aes-bad.o" "$work/a64-crc-aes-bad.s" >/dev/null 2>&1; then
        echo "invalid ARM crypto/CRC operand accepted: $invalid" >&2;exit 1
    fi
done
for invalid in 'aese v0.16b,v1.16b' 'crc32x w0,w1,x2'; do
    printf '.arch armv8-a\n.text\n%s\n' "$invalid" > "$work/a64-crc-aes-bad.s"
    if "$MINIAS" --target=aarch64 -o "$work/a64-crc-aes-bad.o" "$work/a64-crc-aes-bad.s" >/dev/null 2>&1; then
        echo "ISA extension gate bypassed: $invalid" >&2;exit 1
    fi
done
echo 'MINIAS_A64_CRC_AES=PASS GNU_all_8_CRC_plus_AES_words feature_gates'

# Genuine GNU 6.6 crypto EOR, SIMD boolean family, whitespace-free
# shift spelling in ARM kernel ASM, and PMU EL0/EL1 MRS/MSR ABI.
cat >"$work/a64-simd-pmu-shift.s" <<'GNU_SIMD_PMU'
.arch armv8-a+crypto
.text
eor v0.16b,v0.16b,v3.16b
eor v1.16b,v1.16b,v1.16b
eor v2.8b,v3.8b,v4.8b
orr v5.16b,v6.16b,v7.16b
and v8.16b,v9.16b,v10.16b
bic v11.16b,v12.16b,v13.16b
bsl v14.16b,v15.16b,v16.16b
bit v17.16b,v18.16b,v19.16b
bif v20.16b,v21.16b,v22.16b
add x2,x1,x2,lsl#6
sub w9,w10,w11,asr#3
mrs x0,actlr_el1
msr actlr_el1,x1
mrs x0,pmcntenset_el0
msr pmcntenset_el0,x0
mrs x0,pmintenclr_el1
msr pmintenclr_el1,x0
GNU_SIMD_PMU
"$MINIAS" --target=aarch64 -o "$work/a64-simd-pmu-mini.o" "$work/a64-simd-pmu-shift.s"
"$AARCH64_AS" -o "$work/a64-simd-pmu-gnu.o" "$work/a64-simd-pmu-shift.s"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/a64-simd-pmu-mini.o" "$work/a64-simd-pmu-mini.bin"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/a64-simd-pmu-gnu.o" "$work/a64-simd-pmu-gnu.bin"
cmp "$work/a64-simd-pmu-mini.bin" "$work/a64-simd-pmu-gnu.bin"
for bad in 'eor v1.16b,v1.8b,v3.16b' 'bic v0.4s,v1.4s,v2.4s' \
           'orr v0.8b,v1.16b,v2.8b' 'mrs w0,actlr_el1' \
           'msr pmintenclr_el1,w1' 'add x0,x1,x2,lsl#64'; do
    printf '.arch armv8-a+crypto\n.text\n%s\n' "$bad" >"$work/a64-simd-pmu-invalid.s"
    if "$MINIAS" --target=aarch64 -o "$work/a64-simd-pmu-invalid.o" "$work/a64-simd-pmu-invalid.s" >/dev/null 2>&1; then
        echo "bad ARM SIMD/shift/PMU operand accepted: $bad" >&2;exit 1
    fi
done
echo 'MINIAS_A64_SIMD_PMU_SHIFT=PASS GNU_exact_words SIMD7_nospace_LSL_PMU'

# GNU AArch64 MOVZ/MOVK/MOVN absolute symbol relocations. Each G0..G3
# modifier carries an ELF relocation on the INSTRUCTION, with HW
# placement dictated by the group, independent of explicit LSL syntax.
cat >"$work/a64-movw-reloc.s" <<'A64_MOVW_RELOC'
.arch armv8-a
.text
movz x0, #:abs_g0:foo
movz x1, #:abs_g0_nc:foo+8
movk x2, #:abs_g1_nc:foo
movk x3, #:abs_g2_nc:foo
movz x4, #:abs_g3:foo
movk x5, #:abs_g1:foo
movz w6, #:abs_g0_nc:foo
movn x7, #:abs_g0:foo
movz x8, :abs_g0_s:foo
movz x9, :abs_g1_s:foo
movz x10, :abs_g2_s:foo
movn x11, :abs_g1_s:foo
A64_MOVW_RELOC
"$MINIAS" --target=aarch64 -o "$work/a64-movw-mini.o" "$work/a64-movw-reloc.s"
"$AARCH64_AS" -o "$work/a64-movw-gnu.o" "$work/a64-movw-reloc.s"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/a64-movw-mini.o" "$work/a64-movw-mini.bin"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/a64-movw-gnu.o" "$work/a64-movw-gnu.bin"
cmp "$work/a64-movw-mini.bin" "$work/a64-movw-gnu.bin"
for flavor in mini gnu; do
    "$AARCH64_READELF" -Wr "$work/a64-movw-$flavor.o" |
      awk '$3 ~ /^R_AARCH64_MOVW_[US]ABS_G/ {print $1,$3,$5,$6,$7}' |
      LC_ALL=C sort > "$work/a64-movw-$flavor.rels"
    test "$(wc -l < "$work/a64-movw-$flavor.rels")" -eq 12
done
diff -u "$work/a64-movw-gnu.rels" "$work/a64-movw-mini.rels"
for bad in 'movz w0, #:abs_g3:foo' \
           'movz x0, :abs_g1_s:(1 << 32)' \
           'movz x0, :abs_g2_s:(1 << 48)' \
           'movz x0, :abs_g0:(1 << 16)' \
           'movk x0, #:abs_g1_nc:foo, lsl #16' \
           'movz x0, #:abs_g4:foo' \
           'movz x0, #:abs_g0_nc:'; do
    printf '.text\n%s\n' "$bad" > "$work/a64-movw-bad.s"
    if "$MINIAS" --target=aarch64 -o "$work/a64-movw-bad.o" "$work/a64-movw-bad.s" >/dev/null 2>&1; then
        echo "bad MOVW symbolic ISA operand accepted: $bad" >&2;exit 1
    fi
done
echo 'MINIAS_A64_MOVW_RELOC=PASS GNU_bytes_signed_unsigned_G0_G1_G2_G3_RELA_addends'

# GNU architecture's mov_q macro uses signed/unsigned MOVW groups with
# numeric CONSTANT arguments as well as ELF SYMBOL arguments. Signed
# negative values select MOVN, all others use the HW lane extractor.
cat > "$work/a64-movw-constant.s" <<'GNU_MOVQ_NUMBER'
.arch armv8-a
.text
movz x0,:abs_g1_s:0x12345678
movz x1,:abs_g2_s:(0x123456789abc)
movk x2,:abs_g1_nc:0x12345678
movz x3,:abs_g0_nc:0x12345678
movz x4,:abs_g1_s:(0xffffffffffffffff)
movz x5,:abs_g1_s:(-0x80000000)
movz x6,:abs_g2_s:(1 << 46)
movk x7,:abs_g0_nc:(0x8000 | 0xffff)
GNU_MOVQ_NUMBER
"$MINIAS" --target=aarch64 -o "$work/a64-movw-number-mini.o" "$work/a64-movw-constant.s"
"$AARCH64_AS" -o "$work/a64-movw-number-gnu.o" "$work/a64-movw-constant.s"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/a64-movw-number-mini.o" "$work/a64-movw-number-mini.bin"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/a64-movw-number-gnu.o" "$work/a64-movw-number-gnu.bin"
cmp "$work/a64-movw-number-mini.bin" "$work/a64-movw-number-gnu.bin"
"$AARCH64_READELF" -Wr "$work/a64-movw-number-mini.o" | grep -q 'There are no relocations'
echo 'MINIAS_A64_MOVW_NUMERIC=PASS GNU_numeric_signed_MOVN_lane_extraction'

# Real Linux AArch64 crypto and kernel SIMD register families,
# decoded through shared target arrangement/parser, not copied MC backends.
cat > "$work/a64-multivector-dup-sys.s" <<'GNU_SIMD_NEW'
.arch armv8.5-a
.text
ld1 {v0.4s-v3.4s}, [x8], #64
ld1 {v16.2d-v18.2d}, [x8]
ld1 {v0.16b-v1.16b}, [x8]
ld1 {v0.16b-v3.16b}, [x8], x9
ld1 {v1.4s,v2.4s,v3.4s}, [x8]
st1 {v0.16b-v3.16b}, [x8]
st1 {v16.2d-v18.2d}, [x8]
dup v0.4s, w6
dup v1.4s, w0
dup v0.16b, w6
dup v2.2d, x9
dup v0.8h, w1
rev32 v0.16b, v0.16b
rev32 v1.8b, v2.8b
rev32 v0.8h, v1.8h
sys 3, c7, c13, 1, x3
mrs x1, id_aa64pfr1_el1
msr pmovsclr_el0, x1
GNU_SIMD_NEW
"$MINIAS" --target=aarch64 -o "$work/a64-multivector-mini.o" "$work/a64-multivector-dup-sys.s"
"$AARCH64_AS" -o "$work/a64-multivector-gnu.o" "$work/a64-multivector-dup-sys.s"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/a64-multivector-mini.o" "$work/a64-multivector-mini.bin"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/a64-multivector-gnu.o" "$work/a64-multivector-gnu.bin"
cmp "$work/a64-multivector-mini.bin" "$work/a64-multivector-gnu.bin"
for bad in 'ld1 {v0.4s-v4.4s},[x8]' \
           'ld1 {v0.4s-v3.4s},[x8],#48' \
           'ld1 {v0.16b,v2.16b},[x8]' \
           'dup v0.2d,w1' 'dup v1.4s,x3' \
           'rev32 v0.8b,v1.16b' \
           'sys 8,c7,c13,1,x3' 'sys 3,c16,c13,1,x3' \
           'msr id_aa64pfr1_el1,x0'; do
    printf '.arch armv8.5-a\n.text\n%s\n' "$bad" > "$work/a64-multivector-bad.s"
    if "$MINIAS" --target=aarch64 -o "$work/a64-multivector-bad.o" "$work/a64-multivector-bad.s" >/dev/null 2>&1; then
        echo "invalid A64 SIMD/SYS operand accepted: $bad" >&2;exit 1
    fi
done
echo 'MINIAS_A64_SIMD_STRUCTURE_DUP_REV32_SYS=PASS GNU_18_words_N_vector_list_and_arch_fields'

# GNU ARMv8.5+memtag STGM, ARMv8 LSE paired CASP and lane-indexed INS.
# Compare the ENTIRE real GNU text, not just accepted status; reject
# extension-gate bypass, pair misalignment and out-of-range SIMD lanes.
cat >"$work/a64-mte-casp-ins.s" <<'GNU_CASP_MTE_INS'
.arch armv8.5-a+memtag
.arch_extension lse
.text
stgm xzr,[x0]
stgm x1,[x2]
casp x0,x1,x2,x3,[x4]
caspa x4,x5,x6,x7,[x8]
caspl x8,x9,x10,x11,[x12]
caspal x12,x13,x14,x15,[x16]
ins v1.b[0],w7
ins v1.b[15],w7
ins v2.h[3],w3
ins v3.s[2],w4
ins v3.d[1],x4
rev32 x16,x17
GNU_CASP_MTE_INS
"$MINIAS" --target=aarch64 -o "$work/a64-mte-casp-mini.o" "$work/a64-mte-casp-ins.s"
"$AARCH64_AS" -o "$work/a64-mte-casp-gnu.o" "$work/a64-mte-casp-ins.s"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/a64-mte-casp-mini.o" "$work/a64-mte-casp-mini.bin"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/a64-mte-casp-gnu.o" "$work/a64-mte-casp-gnu.bin"
cmp "$work/a64-mte-casp-mini.bin" "$work/a64-mte-casp-gnu.bin"
for bad in 'casp x1,x2,x4,x5,[x0]' 'casp x0,x2,x4,x5,[x0]' \
           'casp x0,x1,w2,w3,[x4]' 'ins v0.b[16],w1' \
           'ins v0.h[8],w1' 'ins v1.d[1],w1' 'stgm w0,[x1]'; do
    printf '.arch armv8.5-a+memtag\n.arch_extension lse\n.text\n%s\n' "$bad" > "$work/a64-mte-casp-invalid.s"
    if "$MINIAS" --target=aarch64 -o "$work/a64-mte-casp-invalid.o" "$work/a64-mte-casp-invalid.s" >/dev/null 2>&1; then
        echo "invalid CASP/MTE/INS operand accepted: $bad" >&2;exit 1
    fi
done
for disabled in 'stgm xzr,[x0]' 'casp x0,x1,x2,x3,[x4]'; do
    printf '.arch armv8-a\n.text\n%s\n' "$disabled" > "$work/a64-mte-casp-feature.s"
    if "$MINIAS" --target=aarch64 -o "$work/a64-mte-casp-feature.o" "$work/a64-mte-casp-feature.s" >/dev/null 2>&1; then
        echo "unsupported extension was silently enabled: $disabled" >&2;exit 1
    fi
done
echo 'MINIAS_A64_CASP_STGM_INS=PASS GNU_12_words_MTE_LSE_scalar_REV32'

# AArch64 GNU Linux crypto/atomic source root families: SIMD ADD/TRN1,
# EXT, MOVI constant splats, and LDXP/STXP paired exclusive accesses.
cat > "$work/a64-kernel-simd-atomic.s" <<'GNU_SIMD_ATOMIC'
.arch armv8.5-a+memtag
.text
add v4.4s,v4.4s,v0.4s
add v22.4s,v16.4s,v0.4s
add v0.8b,v1.8b,v2.8b
trn1 v1.2d,v0.2d,v16.2d
trn1 v1.4s,v2.4s,v3.4s
ext v1.16b,v1.16b,v1.16b,#1
ext v2.8b,v3.8b,v4.8b,#7
movi v0.16b,#0
movi v1.8b,#255
movi v2.4s,#0
movi v3.4s,#255
movi v4.2d,#0
ldxp x22,x21,[x4]
ldaxp x0,x1,[x2]
ldxp w0,w1,[x2]
stxp w12,x0,x1,[x2]
stlxp w9,x10,x11,[x12]
stxp w4,w5,w6,[x7]
dc gzva,x0
mrs x1,pmintenset_el1
msr pmintenset_el1,x2
GNU_SIMD_ATOMIC
"$MINIAS" --target=aarch64 -o "$work/a64-simd-atomic-mini.o" "$work/a64-kernel-simd-atomic.s"
"$AARCH64_AS" -o "$work/a64-simd-atomic-gnu.o" "$work/a64-kernel-simd-atomic.s"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/a64-simd-atomic-mini.o" "$work/a64-simd-atomic-mini.bin"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/a64-simd-atomic-gnu.o" "$work/a64-simd-atomic-gnu.bin"
cmp "$work/a64-simd-atomic-mini.bin" "$work/a64-simd-atomic-gnu.bin"
for bad in 'add v0.1d,v1.1d,v2.1d' 'trn1 v0.1d,v1.1d,v2.1d' \
           'add v0.8b,v1.16b,v2.8b' 'ext v0.16b,v1.16b,v2.16b,#16' \
           'ext v0.8b,v1.8b,v2.8b,#8' 'movi v0.8h,#1' \
           'ldxp x0,w1,[x2]' 'ldxp x0,x1,[x2,#8]' \
           'stxp x0,x1,x2,[x3]' 'dc gzva,w0' \
           'msr pmintenset_el1,w1'; do
    printf '.arch armv8.5-a+memtag\n.text\n%s\n' "$bad" > "$work/a64-simd-atomic-invalid.s"
    if "$MINIAS" --target=aarch64 -o "$work/a64-simd-atomic-invalid.o" "$work/a64-simd-atomic-invalid.s" >/dev/null 2>&1; then
        echo "invalid GNU SIMD/atomic/sysreg operands accepted: $bad" >&2;exit 1
    fi
done
printf '.arch armv8-a\n.text\ndc gzva,x0\n' > "$work/a64-no-mte-dc.s"
if "$MINIAS" --target=aarch64 -o "$work/a64-no-mte-dc.o" "$work/a64-no-mte-dc.s" >/dev/null 2>&1; then
  echo 'MTE DC GZVA accepted without MTE ISA feature' >&2;exit 1
fi
echo 'MINIAS_A64_SIMD_ATOMIC_FAMILY=PASS native_GNU_21_instructions'
# GNU A64 SHA2+TRN2+UMOV+FP D-lane+MTE STZ2G exact bytes.
cat > "$work/a64-sha2-lane-mte.s" <<'ISA_BATCH_GNU'
.arch armv8-a+crypto
.text
sha256su0 v16.4s,v17.4s
sha256su0 v0.4s,v1.4s
sha256su1 v0.4s,v1.4s,v2.4s
trn2 v2.2d,v0.2d,v16.2d
trn2 v0.16b,v1.16b,v2.16b
umov w6,v0.b[0]
umov w0,v0.s[0]
umov x5,v7.d[1]
mov d19,v3.d[1]
mov d0,v1.d[0]
.arch armv8.5-a+memtag
stz2g x0,[x1]
stz2g x2,[x3,#32]!
stz2g x4,[x5,#-16]!
stz2g x6,[x7],#0
stz2g x6,[x7],#16
stz2g x0,[x1,#16]
ISA_BATCH_GNU
"$MINIAS" --target=aarch64 -o "$work/a64-sha2-lane-mini.o" "$work/a64-sha2-lane-mte.s"
"$AARCH64_AS" -o "$work/a64-sha2-lane-gnu.o" "$work/a64-sha2-lane-mte.s"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/a64-sha2-lane-mini.o" "$work/a64-sha2-lane-mini.bin"
"$AARCH64_OBJCOPY" -j .text -O binary "$work/a64-sha2-lane-gnu.o" "$work/a64-sha2-lane-gnu.bin"
cmp "$work/a64-sha2-lane-mini.bin" "$work/a64-sha2-lane-gnu.bin"
for bad in 'sha256su0 v0.8b,v1.8b' 'sha256su1 v0.4s,v1.2d,v2.4s' \
           'umov x0,v0.s[0]' 'umov w0,v0.d[0]' \
           'mov d0,v1.s[0]' 'stz2g w0,[x1]' \
           'stz2g x0,[x1],#17' 'stz2g x0,[x1,#4081]!'; do
    printf '.arch armv8.5-a+memtag\n.arch_extension crypto\n.text\n%s\n' "$bad" > "$work/a64-sha2-lane-invalid.s"
    if "$MINIAS" --target=aarch64 -o "$work/a64-sha2-lane-invalid.o" "$work/a64-sha2-lane-invalid.s" >/dev/null 2>&1; then
        echo "bad SHA2/UMOV/MTE operands accepted: $bad" >&2;exit 1
    fi
done
echo 'MINIAS_A64_SHA2_LANE_MTE=PASS real_GNU_16_instructions'
echo 'MINIAS_AARCH64_VSLICE=PASS ELF64=1 gnu_bytes=equal relocation_call26_jump26_abs64=1 ld=pass qemu=exit0'
