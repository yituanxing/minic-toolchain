#include "target/x86_64/machine_internal.h"
#include "target/x86_64/abi.h"
#include "target/x86_64/asm_contract.h"

#include <stdio.h>
#include <string.h>

/* Target-only fixed-register GNU asm emitter. Core operands are independent
 * scalar SSA values and address-backed lvalues, never compiler-private C
 * pseudo-registers. Only prequalified complete register contracts are emitted. */
static bool clobber_is(const MinicCoreInlineAsmRegisterClobber *c,
                       const char *name) {
    const size_t len = strlen(name);
    return c->name && c->name_length == len &&
           memcmp(c->name, name, len) == 0;
}

/* Print GNU numeric template references using x86 assigned physical regs.
 * Operand 7 is a genuine memory reference, never a value expression. */
static bool emit_mulacc_template(FILE *out, const MinicCoreInlineAsm *info) {
    size_t i;
    for (i = 0U; i < info->template_length; ++i) {
        if (info->template_text[i] != '%') {
            if (fputc(info->template_text[i], out) == EOF) return false;
            continue;
        }
        if (++i >= info->template_length) return false;
        switch (info->template_text[i]) {
        case '%': if (fputc('%', out) == EOF) return false; break;
        case '0': if (fputs("%r8", out) == EOF) return false; break;
        case '1': if (fputs("%r9", out) == EOF) return false; break;
        case '2': if (fputs("%rcx", out) == EOF) return false; break;
        case '7': if (fputs("(%r11)", out) == EOF) return false; break;
        default: return false;
        }
    }
    return fputc('\n', out) != EOF;
}


/* Register-only GNU "=r"/matching-digit group with memory clobber. The
 * template has no implicit registers, and Core's preflight restricts the
 * instructions to mov/add/adc/sub/sbb and %N registers. No per-BusyBox-function
 * rewrites: the same ABI operand assignment works for add, sub and mod. */
static bool emit_matched_register_template(
    FILE *out, const MinicCoreInlineAsm *info, size_t outputs) {
    static const char *const regs[] = {"%r8", "%r9", "%r10", "%rcx"};
    size_t i;
    for (i = 0U; i < info->template_length; ++i) {
        if (info->template_text[i] != '%') {
            if (fputc(info->template_text[i], out) == EOF) return false;
        } else {
            if (++i >= info->template_length ||
                info->template_text[i] < '0' ||
                (size_t)(info->template_text[i] - '0') >= outputs ||
                fputs(regs[(size_t)(info->template_text[i] - '0')], out) == EOF)
                return false;
        }
    }
    return fputc('\n', out) != EOF;
}

/* 64x64 unsigned multiply into RDX:RAX, then 128/64 unsigned divide.
 * Exact GNU "=a"/"a"/"rm"/"rm"/"rdx" operand ownership is checked again
 * against Core binding roles. No hidden register allocator policy escapes
 * the x86 machine emitter and the output still lands in its SSA lvalue. */
/* Core's structured ASM describes output address + scalar input, not
 * an instruction string to be forwarded. The machine rechecks the entire
 * x86 64-bit shape, chooses physical caller-scratch R10/R11/RAX, and emits
 * only verified nonzero-word BSF/BSR semantics. */
static bool emit_bit_scan_u64(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    bool reverse = false, signed_result;
    MinicType output_type;
    size_t width;
    if (!out || !program || !target || !fn || !frame || !ins || !info ||
        info->register_clobber_count != 0U ||
        info->has_memory_clobber || info->is_goto ||
        ins->value.structured_inline_asm.operand_count != 2U ||
        b == NULL ||
        !minic_x86_64_asm_bit_scan_template(
            info->template_text, info->template_length, &reverse) ||
        b[0].operand_index != 0U || b[1].operand_index != 1U ||
        b[0].has_fixed_register_binding || b[1].has_fixed_register_binding ||
        b[0].early_clobber || b[1].early_clobber ||
        b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
        b[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        b[0].value >= fn->value_count || b[1].value >= fn->value_count ||
        !minic_type_pointee(fn->values[b[0].value].type, &output_type) ||
        !minic_type_equal(output_type, minic_type_unsigned_long()) ||
        !minic_type_equal(fn->values[b[1].value].type, minic_type_unsigned_long()) ||
        !minic_x86_64_scalar_width(
            program, target, output_type, &width, &signed_result) ||
        width != 8U)
        return false;
    return x86_value_load(out, frame, b[1].value, "%r10") &&
           fputs(reverse ? "  bsrq %r10, %rax\n" :
                          "  rep; bsfq %r10, %rax\n", out) != EOF &&
           x86_value_load(out, frame, b[0].value, "%r11") &&
           fputs("  movq %rax, (%r11)\n", out) != EOF;
}

/* Kernel fls/variable_ffs use a third GNU tied input ("0"(-1))
 * with 32-bit BSF/BSR; fls64 uses one +r initialized 32-bit destination
 * with a 64-bit BSRQ. Respect input zero fallback and REAL flags, never
 * replace the hardware bit scan with a host C builtin. */
static bool emit_linux_tied_bit_scan(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const size_t count = ins->value.structured_inline_asm.operand_count;
    const MinicX86BitScanForm form = minic_x86_64_asm_bit_scan_form(
        info->template_text, info->template_length);
    const bool is64 = form == MINIC_X86_BIT_SCAN_BSR64_READWRITE;
    const char *mnemonic = form == MINIC_X86_BIT_SCAN_BSF32_TIED ?
        "  bsfl %r10d, %eax\n" :
        form == MINIC_X86_BIT_SCAN_BSR32_TIED ?
        "  bsrl %r10d, %eax\n" : "  bsrq %r10, %rax\n";
    MinicType output_type;
    size_t width, i;
    bool sign;
    if (!out || !program || !target || !fn || !frame || !info || !b ||
        (count != 3U && count != 2U) ||
        (is64 && count != 2U) || (!is64 && count != 3U) ||
        info->is_goto || info->has_memory_clobber ||
        info->register_clobber_count || !info->template_text ||
        (form != MINIC_X86_BIT_SCAN_BSF32_TIED &&
         form != MINIC_X86_BIT_SCAN_BSR32_TIED &&
         form != MINIC_X86_BIT_SCAN_BSR64_READWRITE))
        return false;
    for (i = 0U; i < count; ++i)
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].has_fixed_register_binding || b[i].early_clobber ||
            b[i].kind != (i == 0U ?
                (is64 ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE :
                        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT) :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
            return false;
    if (!minic_type_pointee(fn->values[b[0].value].type, &output_type) ||
        !minic_type_equal(output_type, minic_type_int()) ||
        !minic_x86_64_scalar_width(program, target, output_type, &width, &sign) ||
        width != 4U ||
        !minic_x86_64_scalar_width(
            program, target, fn->values[b[1].value].type, &width, &sign) ||
        width != (is64 ? 8U : 4U))
        return false;
    if (count == 3U &&
        (!minic_type_equal(fn->values[b[2].value].type, minic_type_int()) ||
         !x86_value_load(out, frame, b[2].value, "%rax")))
        return false;
    if (is64) {
        if (!x86_value_load(out, frame, b[0].value, "%r11") ||
            fputs("  movl (%r11), %eax\n", out) == EOF)
            return false;
    }
    if (!x86_value_load(out, frame, b[1].value, "%r10") ||
        fputs(mnemonic, out) == EOF ||
        !x86_value_load(out, frame, b[0].value, "%r11"))
        return false;
    return fputs("  movl %eax, (%r11)\n", out) != EOF;
}

/* One x86 hardware bitops emitter owns all source-validated
 * LOCK/non-LOCK BT/BTS/BTR/BTC, carrier CF and SMP lock patch metadata.
 * No template text is forwarded to GAS; only safe ISA rows emit machine
 * opcodes. GNU 'm' inputs can be real memory effects for BTS/BTR/BTC. */
static bool emit_linux_bitops(
    FILE *out,const MinicC0Program *program,const MinicTargetInfo *target,
    const MinicCoreFunction *fn,const MinicX86Frame *frame,
    const MinicCoreInstruction *ins,const MinicCoreInlineAsm *info) {
    const MinicX86BitopsForm form=minic_x86_64_linux_bitops_form(
        info->template_text,info->template_length);
    const MinicX86BitopsDesc *d=minic_x86_64_bitops_description(form);
    const MinicCoreStructuredInlineAsmOperand *b=
        ins->value.structured_inline_asm.operands;
    const size_t count=ins->value.structured_inline_asm.operand_count;
    const bool byte_rmw=form==MINIC_X86_BITOPS_LOCK_OR_BYTE||
                        form==MINIC_X86_BITOPS_LOCK_AND_BYTE;
    const size_t memindex=d&&d->memory_output?0U:d&&d->has_flag?1U:0U;
    const size_t bitindex=d&&d->memory_output&&d->has_flag?2U:
                         d&&d->memory_output?1U:d&&d->has_flag?2U:1U;
    const size_t flagindex=d&&d->has_flag?(d->memory_output?1U:0U):SIZE_MAX;
    MinicType mem,flag;
    size_t width,i;
    bool sign;
    if(!out||!program||!target||!fn||!frame||!ins||!info||!b||!d||
       !info->has_memory_clobber||info->register_clobber_count||
       count!=(d->has_flag?3U:2U)||(d->is_locked&&!info->is_volatile))
        return false;
    for(i=0U;i<count;++i)
        if(b[i].operand_index!=i||b[i].value>=fn->value_count||
           b[i].early_clobber||b[i].has_fixed_register_binding||
           b[i].kind!=(i==memindex?
               (d->writes_memory?
                    MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
                    MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT):
               i==flagindex?
                    MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
                    MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
            return false;
    if(!minic_type_pointee(fn->values[b[memindex].value].type,&mem)||
       !minic_x86_64_scalar_width(program,target,mem,&width,&sign)||
       width!=(byte_rmw?1U:8U)||
       !minic_x86_64_scalar_width(program,target,
           fn->values[b[bitindex].value].type,&width,&sign)||
       width!=(byte_rmw?4U:8U)||
       (d->has_flag&&
        (!minic_type_pointee(fn->values[b[flagindex].value].type,&flag)||
         !minic_type_equal(flag,minic_type_bool()))))
        return false;
    if(!x86_value_load(out,frame,b[memindex].value,"%r11")||
       !x86_value_load(out,frame,b[bitindex].value,"%r10"))
        return false;
    if(d->is_locked&&
       fputs("  .pushsection .smp_locks,\"a\"\n"
             "  .balign 4\n"
             "  .long 671f - .\n"
             "  .popsection\n"
             "671:\n",out)==EOF)
        return false;
    if(fprintf(out,"  %s%s %s, (%%r11)\n",
               d->is_locked?"lock; ":"",d->opcode,
               byte_rmw?"%r10b":"%r10")<0)
        return false;
    if(!d->has_flag)return true;
    return fputs("  setc %al\n",out)!=EOF&&
           x86_value_load(out,frame,b[flagindex].value,"%r10")&&
           fputs("  movb %al, (%r10)\n",out)!=EOF;
}

/* Genuine x86 CMPXCHG: use AL/AX/EAX/RAX comparator, return ZF,
 * update old on failure, and emit SMP patch metadata when locked. */
static bool emit_linux_trycas(
    FILE *out,const MinicC0Program *program,const MinicTargetInfo *target,
    const MinicCoreFunction *fn,const MinicX86Frame *frame,
    const MinicCoreInstruction *ins,const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b=
        ins->value.structured_inline_asm.operands;
    const MinicX86TryCasDesc *d=minic_x86_64_trycas_desc(
        minic_x86_64_trycas_form(info->template_text,info->template_length));
    MinicType t;
    size_t i,width;
    bool sign;
    if(!out||!program||!target||!fn||!frame||!info||!b||!d||
       !info->is_volatile||info->is_goto||!info->has_memory_clobber||
       info->register_clobber_count||
       ins->value.structured_inline_asm.operand_count!=4U)
        return false;
    for(i=0U;i<4U;++i)
        if(b[i].operand_index!=i||b[i].value>=fn->value_count||
           b[i].early_clobber||b[i].has_fixed_register_binding||
           b[i].kind!=(i==0U?MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
                        i==1U?MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
                        i==2U?MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE:
                              MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
            return false;
    for(i=0U;i<3U;++i)
        if(!minic_type_pointee(fn->values[b[i].value].type,&t)||
           !minic_x86_64_scalar_width(program,target,t,&width,&sign)||
           width!=(i==0U?1U:d->width))
            return false;
    if(!minic_x86_64_scalar_width(program,target,
          fn->values[b[3].value].type,&width,&sign)||width!=d->width)
        return false;
    if(!x86_value_load(out,frame,b[2].value,"%r11")||
       fprintf(out,"  %s\n",d->load_old)<0||
       !x86_value_load(out,frame,b[3].value,"%r10")||
       !x86_value_load(out,frame,b[1].value,"%r11"))
        return false;
    if(d->locked&&
       fputs("  .pushsection .smp_locks,\"a\"\n"
             "  .balign 4\n"
             "  .long 671f - .\n"
             "  .popsection\n"
             "671:\n",out)==EOF)
        return false;
    return fprintf(out,"  %s%s %s, (%%r11)\n",
             d->locked?"lock; ":"",d->op,d->new_reg)>=0&&
           fputs("  sete %r8b\n",out)!=EOF&&
           x86_value_load(out,frame,b[2].value,"%r11")&&
           fprintf(out,"  %s\n",d->store_old)>=0&&
           x86_value_load(out,frame,b[0].value,"%r11")&&
           fputs("  movb %r8b, (%r11)\n",out)!=EOF;
}

/* One physical x86 owner for Linux unary and binary locked RMWcc.
 * The original .smp_locks PREL32 entry, hardware locked write and final
 * ZF/SF SETcc all remain machine-verified. No generic Core ISA new case. */
static bool emit_linux_atomic_cc(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const MinicX86AtomicCC form = minic_x86_64_atomic_cc_form(
        info->template_text, info->template_length);
    const MinicX86AtomicCCDesc *d = minic_x86_64_atomic_cc_description(form);
    const size_t n = d && d->has_value ? 3U : 2U;
    MinicType mem, flag;
    size_t width, i;
    bool sign;
    if (!out || !program || !target || !fn || !frame || !b || !info ||
        !d || !info->is_volatile || info->is_goto ||
        !info->has_memory_clobber || info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != n)
        return false;
    for (i = 0U; i < n; ++i)
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].has_fixed_register_binding || b[i].early_clobber ||
            b[i].kind != (i == 0U ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
                i == 1U ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                          MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
            return false;
    if (!minic_type_pointee(fn->values[b[0].value].type, &mem) ||
        !minic_type_pointee(fn->values[b[1].value].type, &flag) ||
        !minic_type_equal(flag, minic_type_bool()) ||
        !minic_x86_64_scalar_width(program, target, mem, &width, &sign) ||
        width != d->width ||
        (d->has_value &&
         (!minic_x86_64_scalar_width(
             program, target, fn->values[b[2].value].type, &width, &sign) ||
          width != d->width)))
        return false;
    if ((d->has_value && !x86_value_load(out, frame, b[2].value, "%r10")) ||
        !x86_value_load(out, frame, b[0].value, "%r11") ||
        fputs("  .pushsection .smp_locks,\"a\"\n"
              "  .balign 4\n"
              "  .long 671f - .\n"
              "  .popsection\n"
              "671:\n", out) == EOF)
        return false;
    if (d->has_value) {
        if (fprintf(out, "  lock; %s %s, (%%r11)\n",
                d->opcode, d->width == 4U ? "%r10d" : "%r10") < 0)
            return false;
    } else if (fprintf(out, "  lock; %s (%%r11)\n", d->opcode) < 0)
        return false;
    return fputs(d->flag_sign ? "  sets %al\n" : "  sete %al\n", out) != EOF &&
           x86_value_load(out, frame, b[1].value, "%r10") &&
           fputs("  movb %al, (%r10)\n", out) != EOF;
}

/* The only owner of this x86 hardware side effect: CMP sets CF, SBB
 * consumes it immediately to create the branchless speculative mask.
 * Core transports typed SSA values, never a pretend boolean mask. */
static bool emit_nospec_mask(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicType output_type;
    size_t width, index_width, output_width, i;
    bool sign, index_signed;
    if (!out || !program || !target || !fn || !frame || !ins || !info ||
        ins->value.structured_inline_asm.operand_count != 3U ||
        !b || !info->is_volatile || info->is_goto || info->has_memory_clobber ||
        info->register_clobber_count != 1U || !info->register_clobbers ||
        !clobber_is(&info->register_clobbers[0], "cc") ||
        !minic_x86_64_asm_nospec_template(info->template_text, info->template_length))
        return false;
    for (i = 0U; i < 3U; ++i)
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].has_fixed_register_binding || b[i].early_clobber ||
            b[i].kind != (i == 0U ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
            return false;
    if (!minic_type_pointee(fn->values[b[0].value].type, &output_type) ||
        !minic_type_equal(output_type, minic_type_unsigned_long()) ||
        !minic_x86_64_scalar_width(
            program, target, output_type, &output_width, &sign) ||
        output_width != 8U ||
        !minic_x86_64_scalar_width(
            program, target, fn->values[b[1].value].type, &width, &sign) ||
        !minic_x86_64_scalar_width(
            program, target, fn->values[b[2].value].type,
            &index_width, &index_signed) ||
        width != index_width || (width != 4U && width != 8U))
        return false;
    if (!x86_value_load(out, frame, b[1].value, "%r10") ||
        !x86_value_load(out, frame, b[2].value, "%r11"))
        return false;
    if (fputs(width == 8U ? "  cmpq %r10, %r11\n" :
                              "  cmpl %r10d, %r11d\n", out) == EOF ||
        fputs("  sbbq %r8, %r8\n", out) == EOF ||
        !x86_value_load(out, frame, b[0].value, "%r9"))
        return false;
    return fputs("  movq %r8, (%r9)\n", out) != EOF;
}

/* A GNU p argument holds the address of the per-cpu field, not the
 * field's contents. GS base is added by x86 hardware on the actual load.
 * The caller's normal C result lvalue is written only after the GS read;
 * neither a plain Core LOAD nor generic textual inline asm is equivalent. */
static bool emit_percpu_stable_u64(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicType type;
    size_t width;
    bool sign;
    if (!out || !program || !target || !fn || !frame || !ins || !info ||
        info->is_goto || info->has_memory_clobber ||
        info->register_clobber_count != 0U ||
        ins->value.structured_inline_asm.operand_count != 2U ||
        !minic_x86_64_asm_percpu_stable_template(
            info->template_text, info->template_length) ||
        b[0].operand_index != 0U || b[1].operand_index != 1U ||
        b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
        b[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        b[0].has_fixed_register_binding || b[1].has_fixed_register_binding ||
        b[0].early_clobber || b[1].early_clobber ||
        b[0].value >= fn->value_count || b[1].value >= fn->value_count ||
        !minic_type_pointee(fn->values[b[0].value].type, &type) ||
        !minic_x86_64_scalar_width(
            program, target, type, &width, &sign) || width != 8U ||
        !minic_type_is_integer(type) ||
        !minic_type_is_pointer(fn->values[b[1].value].type))
        return false;
    /* x86-64 SysV RAX/R10/R11 are caller-saved and Core SSA is spilled.
     * The original GNU %P operand can be materialized as an address in R10;
     * %gs:(R10) gives exactly GS-base + per-cpu symbol/field displacement. */
    return x86_value_load(out, frame, b[1].value, "%r10") &&
           fputs("  movq %gs:(%r10), %rax\n", out) != EOF &&
           x86_value_load(out, frame, b[0].value, "%r11") &&
           fputs("  movq %rax, (%r11)\n", out) != EOF;
}
static bool emit_muldiv_u64(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    size_t i, bytes;
    bool sign;
    if (ins->value.structured_inline_asm.operand_count != 4U ||
        info->register_clobber_count != 1U || !info->register_clobbers ||
        !clobber_is(&info->register_clobbers[0], "rdx") ||
        info->has_memory_clobber ||
        !minic_x86_64_asm_muldiv_template(info->template_text, info->template_length))
        return false;
    for (i = 0U; i < 4U; ++i) {
        MinicType type;
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].has_fixed_register_binding || b[i].early_clobber ||
            b[i].kind != (i == 0U ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
                                   : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
            return false;
        type = fn->values[b[i].value].type;
        if (i == 0U && !minic_type_pointee(type, &type)) return false;
        if (!minic_x86_64_scalar_width(program, target, type, &bytes, &sign) ||
            bytes != 8U || !minic_type_is_integer(type)) return false;
    }
    /* Select register alternatives for GNU rm, never RDX. */
    if (!x86_value_load(out, frame, b[2].value, "%r10") ||
        !x86_value_load(out, frame, b[3].value, "%r11") ||
        !x86_value_load(out, frame, b[1].value, "%rax") ||
        fputs("  mulq %r10\n  divq %r11\n", out) == EOF ||
        !x86_value_load(out, frame, b[0].value, "%r10") ||
        fprintf(out, "  movq %%rax, (%%r10)\n") < 0)
        return false;
    return true;
}

static bool emit_memory_clobber_matched(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    static const char *const regs[] = {"%r8", "%r9", "%r10", "%rcx"};
    const size_t count = ins->value.structured_inline_asm.operand_count;
    const size_t outputs = count == 7U ? 4U : count == 5U ? 3U :
                           count == 2U ? 1U : 0U;
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    size_t i, bytes;
    bool sign;
    if (outputs == 0U || !info->has_memory_clobber ||
        info->register_clobber_count != 0U ||
        !minic_x86_64_asm_matched_template(info->template_text,
                                          info->template_length, outputs))
        return false;
    for (i = 0U; i < count; ++i) {
        MinicType type;
        if (b[i].value >= fn->value_count || b[i].operand_index != i ||
            b[i].has_fixed_register_binding || b[i].early_clobber)
            return false;
        type = fn->values[b[i].value].type;
        if (i < outputs) {
            if (b[i].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
                !minic_type_pointee(type, &type)) return false;
        } else if (b[i].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT)
            return false;
        if (!minic_x86_64_scalar_width(program, target, type, &bytes, &sign) ||
            (bytes != 8U &&
             !(i >= outputs && bytes == 4U && minic_type_is_integer(type))))
            return false;
    }
    for (i = 0U; i < count-outputs; ++i)
        if (!x86_value_load(out, frame, b[outputs+i].value, regs[i]))
            return false;
    if (!emit_matched_register_template(out, info, outputs)) return false;
    for (i = 0U; i < outputs; ++i)
        if (!x86_value_load(out, frame, b[i].value, "%r11") ||
            fprintf(out, "  movq %s, (%%r11)\n", regs[i]) < 0)
            return false;
    return true;
}

bool minic_x86_64_emit_structured_asm(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicX86Frame *frame,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *info;
    const MinicCoreStructuredInlineAsmOperand *bindings;
    size_t i, bytes;
    bool sign, cpuid, mulacc;
    static const char *const cpuid_input_regs[4] = {
        "%rax", "%rbx", "%rcx", "%rdx"
    };
    static const char *const cpuid_output_regs[4] = {
        "%eax", "%ebx", "%ecx", "%edx"
    };
    static const char *const mulacc_regs[3] = {"%r8", "%r9", "%rcx"};

    if (!out || !program || !target || !fn || !frame || !ins ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->value.structured_inline_asm.inline_asm_id >= fn->inline_asm_count)
        return false;
    info = &fn->inline_asms[ins->value.structured_inline_asm.inline_asm_id];
    bindings = ins->value.structured_inline_asm.operands;
    if (!info->template_text)
        return false;
    if (minic_x86_64_trycas_form(
            info->template_text,info->template_length)!=MINIC_X86_TRYCAS_NONE)
        return emit_linux_trycas(out,program,target,fn,frame,ins,info);
    if (minic_x86_64_linux_bitops_form(
            info->template_text,info->template_length)!=
        MINIC_X86_BITOPS_NONE)
        return emit_linux_bitops(
            out,program,target,fn,frame,ins,info);
    if (minic_x86_64_atomic_cc_form(
            info->template_text,info->template_length)!=
        MINIC_X86_ATOMIC_CC_NONE)
        return emit_linux_atomic_cc(
            out,program,target,fn,frame,ins,info);
    /* Preserve the pre-existing memory-clobber family (including its
     * valid two-operand dec_mod shape). Only DISJOINT no-clobber,
     * register-output two-operand templates enter the BSF/BSR selector.
     * This dispatch is by semantic contract, never operand count alone. */
    if (info->has_memory_clobber)
        return info->is_volatile &&
               emit_memory_clobber_matched(
                   out, program, target, fn, frame, ins, info);
    if ((ins->value.structured_inline_asm.operand_count == 2U ||
         ins->value.structured_inline_asm.operand_count == 3U) &&
        (minic_x86_64_asm_bit_scan_form(
             info->template_text, info->template_length) ==
             MINIC_X86_BIT_SCAN_BSF32_TIED ||
         minic_x86_64_asm_bit_scan_form(
             info->template_text, info->template_length) ==
             MINIC_X86_BIT_SCAN_BSR32_TIED ||
         minic_x86_64_asm_bit_scan_form(
             info->template_text, info->template_length) ==
             MINIC_X86_BIT_SCAN_BSR64_READWRITE))
        return emit_linux_tied_bit_scan(
            out, program, target, fn, frame, ins, info);
    if (ins->value.structured_inline_asm.operand_count == 2U &&
        info->register_clobber_count == 0U) {
        if (minic_x86_64_asm_percpu_stable_template(
                info->template_text, info->template_length))
            return emit_percpu_stable_u64(
                out, program, target, fn, frame, ins, info);
        return emit_bit_scan_u64(out, program, target, fn, frame, ins, info);
    }
    if (ins->value.structured_inline_asm.operand_count == 3U)
        return emit_nospec_mask(out, program, target, fn, frame, ins, info);
    if (!info->is_volatile)
        return false;
    if (ins->value.structured_inline_asm.operand_count == 4U)
        return emit_muldiv_u64(out, program, target, fn, frame, ins, info);
    if (ins->value.structured_inline_asm.operand_count != 8U &&
        ins->value.structured_inline_asm.operand_count != 6U)
        return false;
    cpuid = info->template_length == 5U &&
        memcmp(info->template_text, "cpuid", 5U) == 0 &&
        info->register_clobber_count == 0U &&
        (ins->value.structured_inline_asm.operand_count == 6U ?
         info->has_memory_clobber : !info->has_memory_clobber);
    mulacc = ins->value.structured_inline_asm.operand_count == 8U &&
        info->register_clobber_count == 2U &&
        info->register_clobbers != NULL &&
        ((clobber_is(&info->register_clobbers[0], "cc") &&
          clobber_is(&info->register_clobbers[1], "dx")) ||
         (clobber_is(&info->register_clobbers[0], "dx") &&
          clobber_is(&info->register_clobbers[1], "cc"))) &&
        minic_x86_64_asm_mulacc_template(info->template_text,
                                         info->template_length);
    if (!cpuid && !mulacc) return false;

    /* Revalidate the target frame layout and register/value kinds. A future
     * generic structured Core producer cannot bypass this machine contract. */
    for (i = 0U;
         i < ins->value.structured_inline_asm.operand_count; ++i) {
        MinicType pointee;
        const MinicCoreValueId id = bindings[i].value;
        if (id >= fn->value_count || bindings[i].operand_index != i ||
            bindings[i].has_fixed_register_binding || bindings[i].early_clobber)
            return false;
        if (i < (cpuid ? 4U : 3U)) {
            if (bindings[i].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
                !minic_type_pointee(fn->values[id].type, &pointee) ||
                !minic_x86_64_scalar_width(program, target, pointee,
                                            &bytes, &sign) ||
                bytes != (cpuid ? 4U : 8U)) return false;
        } else if (mulacc && i == 7U) {
            if (bindings[i].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT ||
                !minic_type_pointee(fn->values[id].type, &pointee) ||
                !minic_x86_64_scalar_width(program, target, pointee,
                                            &bytes, &sign) || bytes != 8U)
                return false;
        } else {
            if (bindings[i].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
                !minic_x86_64_scalar_width(program, target, fn->values[id].type,
                                            &bytes, &sign) ||
                bytes != (cpuid ? 4U : 8U)) return false;
        }
    }
    if (cpuid) {
        /* RBX is callee-preserved even though CPUID overwrites EBX.
         * Keep it on the stack until all four output stores complete. */
        if (fputs("  pushq %rbx\n", out) == EOF) return false;
        for (i = 0U;
             i < ins->value.structured_inline_asm.operand_count - 4U; ++i)
            if (!x86_value_load(
                    out, frame, bindings[i+4U].value,
                    cpuid_input_regs[
                        ins->value.structured_inline_asm.operand_count == 6U &&
                        i == 1U ? 2U : i]))
                return false;
        if (fputs("  cpuid\n", out) == EOF) return false;
        for (i = 0U; i < 4U; ++i)
            if (!x86_value_load(out, frame, bindings[i].value, "%r10") ||
                fprintf(out, "  movl %s, (%%r10)\n",
                        cpuid_output_regs[i]) < 0) return false;
        return fputs("  popq %rbx\n", out) != EOF;
    }

    /* 64x64 -> 128 product in RDX:RAX, added into three 64-bit
     * accumulators with carry. Select the register side of GNU '=rm'.
     * Do not allocate RDX, the explicitly clobbered hardware register. */
    if (!x86_value_load(out, frame, bindings[3].value, "%r8") ||
        !x86_value_load(out, frame, bindings[4].value, "%r9") ||
        !x86_value_load(out, frame, bindings[5].value, "%rcx") ||
        !x86_value_load(out, frame, bindings[6].value, "%rax") ||
        !x86_value_load(out, frame, bindings[7].value, "%r11") ||
        !emit_mulacc_template(out, info)) return false;
    for (i = 0U; i < 3U; ++i)
        if (!x86_value_load(out, frame, bindings[i].value, "%r10") ||
            fprintf(out, "  movq %s, (%%r10)\n", mulacc_regs[i]) < 0)
            return false;
    return true;
}
