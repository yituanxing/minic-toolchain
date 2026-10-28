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
    mrs x0, sp_el0
    mrs x2, sp_el0
    mrs x7, tpidr_el0
    mrs x0, tpidr_el1
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
    prfm pstl1strm, [x2]
    prfm pldl1keep, [x3, #16]
    prfm plil2strm, [x4, #24]
    .inst 0xd503201f, 0xd65f03c0
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
           'stp x29, x30, [sp, -520]!' 'mov x0, #65536'; do
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
                  'brk #65536' 'mov w0, -65537' \
                  'subs x0, w0, x1' 'adds x0, x1, #4097' \
                  'cset sp, eq' 'cinc w0, x1, eq' \
                  'cset x0, bogus' \
                  'csel x0, w1, x2, eq' \
                  'csel sp, x1, x2, eq' \
                  'csinc x0, x1, x2, bad' \
                  'mrs w0, sp_el0' 'msr tpidrro_el0, x0' \
                  'mrs x0, fake_el0' 'dmb #16' 'dmb invalid' \
                  'lsl x0, x1, #64' \
                  'lsr w0, w1, #-1' \
                  'lsl x0, w1, #1' \
                  'lsr w0, w1, x2' \
                  'wx0 .req bogus' \
                  'ccmp x0, w1, #1, eq' \
                  'ccmp x0, #32, #1, eq' \
                  'ccmp x0, #-1, #1, eq' \
                  'ccmp x0, #1, #16, eq' \
                  'ccmn x0, #1, #1, bogus' \
                  'mrs w0, tpidr_el1' 'msr daif, w0' \
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
                  'yield #1' \
                  'hint #.Lnever_defined' \
                  '.equ .Lbad_hint,128; hint #.Lbad_hint' \
                  'bad_label: nop; hint #bad_label' \
                  'dsb #16' 'dsb bogus' 'isb ish' 'isb #16' \
                  'prfm pstl4strm, [x2]' 'prfm pstl1strm, [w2]' \
                  '.inst 0x100000000' \
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
                  'hint bad' 'ret x31' 'mov x2, #65536' 'b external_function + 2' \
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

# Local symbol differences are absolute even in a different output
# section. GNU's .byte/.long/.quad are checked from the same source.
cat >"$work/local-diff.s" <<'A64_DIFF'
.arch armv8-a
.text
.Ldelta_start:
    nop
.Ldelta_end:
    nop
.data
.local delta_fields
delta_fields:
    .byte .Ldelta_end-.Ldelta_start
    .long .Ldelta_end-.Ldelta_start
    .quad .Ldelta_end-.Ldelta_start
A64_DIFF
"$MINIAS" --target=aarch64 -o "$work/diff-mini.o" "$work/local-diff.s"
"$AARCH64_AS" -o "$work/diff-gnu.o" "$work/local-diff.s"
"$AARCH64_OBJCOPY" -j .data -O binary "$work/diff-mini.o" "$work/diff-mini.bin"
"$AARCH64_OBJCOPY" -j .data -O binary "$work/diff-gnu.o" "$work/diff-gnu.bin"
cmp "$work/diff-mini.bin" "$work/diff-gnu.bin"
echo 'MINIAS_COMMON_SYMBOL_DIFF=PASS local_cross_section_byte_long_quad=gnu_equal'

echo 'MINIAS_AARCH64_VSLICE=PASS ELF64=1 gnu_bytes=equal relocation_call26_jump26_abs64=1 ld=pass qemu=exit0'
