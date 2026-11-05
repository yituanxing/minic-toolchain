#include "target/loongarch64/asm_contract_internal.h"
#include "target/loongarch64/simd_asm.h"
#include "target/common/direct_linux_bug_asm.h"
#include "target/common/linux_jump_label_asm.h"
#include "target/loongarch64/frame_emit.h"
#include "target/loongarch64/memory_emit.h"
#include "target/common/lp64_scalar.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* Machine ASM is handed sealed Core SSA operands after GNU/C source
 * qualification. The source qualifiers below still enforce exact C ranks;
 * physical emitters must read only Core execution shape and storage width.
 * Sign mode: -1 = either, 0 = unsigned, 1 = signed. */
static const MinicCoreType *loong_asm_value_type(
    const MinicCoreFunction *fn, MinicCoreValueId value, bool pointee) {
    MinicCoreTypeId id;
    if (fn == NULL || fn->type_context == NULL || fn->values == NULL ||
        value >= fn->value_count) return NULL;
    id = fn->values[value].core_type_id;
    if (pointee && !minic_lp64_core_pointer_pointee(
            fn->type_context, id, &id)) return NULL;
    id = minic_core_type_unqualified(fn->type_context, id);
    return minic_core_type_get(fn->type_context, id);
}

static bool loong_asm_integer(const MinicCoreFunction *fn,
                              MinicCoreValueId value, bool pointee,
                              size_t bytes, int signed_mode) {
    const MinicCoreType *type = loong_asm_value_type(fn, value, pointee);
    return type != NULL && type->kind == MINIC_CORE_TYPE_INTEGER &&
        type->has_layout && !type->value.integer.is_bool &&
        type->value.integer.bit_width == type->size * 8U &&
        (bytes == 0U || type->size == bytes) &&
        (signed_mode < 0 || type->value.integer.is_signed == (signed_mode != 0));
}

static bool loong_asm_pointer(const MinicCoreFunction *fn,
                              MinicCoreValueId value, bool pointee) {
    const MinicCoreType *type = loong_asm_value_type(fn, value, pointee);
    return type != NULL && type->kind == MINIC_CORE_TYPE_POINTER &&
        type->has_layout && type->size == 8U;
}

/* Linux 6.6.143 arch/loongarch/include/asm/jump_label.h:
 * 1:nop (or b %l[l_yes]), then a 16-byte, eight-aligned __jump_table
 * [site PREL32, target PREL32, key PREL64]. No generic ASM passthrough is
 * safe: the C label must become a real Core block, and the key operand
 * must remain an ELF symbolic relocation, never a runtime GPR value. */
unsigned int loong_linux_jump_label_form(
    const char *text, size_t length) {
    return minic_multi_linux_jump_label_form(
        text, length,
        ".pushsection__jump_table,\"aw\".align3"
        ".long1b-.,%l[l_yes]-..quad%0-..popsection");
}




bool minic_loongarch64_linux_jump_label_asm_goto_core(
    const MinicCoreInlineAsm *source) {
    return minic_multi_linux_jump_label_asm_goto_core(
        source, loong_linux_jump_label_form);
}

bool minic_loongarch64_emit_linux_jump_label_asm_goto(
    FILE *out, const MinicCoreInlineAsm *source,
    size_t function_id, size_t function_blocks) {
    char key_relocation[129];
    const unsigned int form = source != NULL
        ? loong_linux_jump_label_form(source->template_text,
                                      source->template_length) : 0U;
    if (out == NULL || !form ||
        !minic_loongarch64_linux_jump_label_asm_goto_core(source) ||
        !minic_multi_linux_jump_relocation_text(
            &source->resolved_immediates[0],
            key_relocation, sizeof(key_relocation)) ||
        source->goto_target >= function_blocks) return false;
    if (form == 1U) {
        if (fputs("1: nop\n", out) == EOF) return false;
    } else if (fprintf(out, "1: b .Lmini_loong_f%zu_b%u\n",
                       function_id, (unsigned int)source->goto_target) < 0) {
        return false;
    }
    return fprintf(out,
        "  .pushsection __jump_table,\"aw\"\n"
        "  .balign 8\n"
        "  .long 1b - .\n"
        "  .long .Lmini_loong_f%zu_b%u - .\n"
        "  .quad %s - .\n"
        "  .popsection\n",
        function_id, (unsigned int)source->goto_target,
        key_relocation) >= 0;
}

/* Exact LoongArch64 kernel hardware zero-operand source forms.
 * ISA semantics, unlike compiler barriers, remain target-local. */
bool minic_loongarch64_qualified_opaque_asm(const MinicCoreInlineAsm *a) {
    static const char *const allowed[] = {
        "idle0", "tlbrd", "tlbclr", "tlbflush", "tlbsrch",
        "tlbwr", "tlbfill", "dbar0", "dbar0x700", "ibar0", "nop"
    };
    /* The original LoongArch Linux 6.6.143 CPU-hotplug and text-relocation
     * paths emit these fixed hardware instructions with trailing newlines.
     * The generic one-instruction whitelist deliberately rejects embedded
     * newlines; qualify these exact source spellings, including the ordered
     * IBAR/DBAR pair needed after executable text relocation.  No arbitrary
     * multi-instruction opaque ASM or operand interpolation is admitted. */
    static const char *const linux_newline_hardware[] = {
        "dbar 0\n\t", "idle 0\n\t", "nop\n\t",
        "ibar 0 \t\ndbar 0 \t\n"
    };
    char compact[64];
    size_t i, n = 0U;
    if (minic_loongarch64_linux_jump_label_asm_goto_core(a)) return true;
    if (minic_loongarch64_simd_qualify_opaque(a)) return true;
    if (minic_multi_direct_linux_bug_asm(
            a, "10000", "10001", "10002", "break1", false))
        return true;
    if (a == NULL || a->template_text == NULL || !a->is_volatile ||
        a->template_length == 0U || a->template_length >= sizeof(compact) ||
        a->is_goto || a->register_clobber_count != 0U)
        return false;
    for (i = 0U;
         i < sizeof(linux_newline_hardware) /
             sizeof(linux_newline_hardware[0]); ++i) {
        const size_t length = strlen(linux_newline_hardware[i]);
        /* Linux's CPU-hotplug DBAR and the IBAR/DBAR executable-text
         * relocation fence both carry a compiler memory barrier.  Match
         * their *exact* literal source templates, never a generic program. */
        if ((i == 0U || i == 3U || !a->has_memory_clobber) &&
            a->template_length == length &&
            memcmp(a->template_text, linux_newline_hardware[i], length) == 0)
            return true;
    }
    /* Original Linux cache.c emits one tab-indented IBAR followed by LF.
     * Accept that single literal opcode, never a newline-delimited program
     * through the general compact/whitelist matcher below. */
    if (a->template_length == sizeof("\tibar 0\n") - 1U &&
        memcmp(a->template_text, "\tibar 0\n",
               sizeof("\tibar 0\n") - 1U) == 0)
        return true;
    for (i = 0U; i < a->template_length; ++i) {
        const char c = a->template_text[i];
        if (c == ' ' || c == '\t') continue;
        if (c == '\n' || c == '\r' || c == ';' || c == '\0')
            return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(allowed) / sizeof(allowed[0]); ++i)
        if (strcmp(compact, allowed[i]) == 0) return true;
    return false;
}

/* Native ISA reservation; rejected CSR/AMO shapes cannot bypass through
 * generic scalar Core inline assembly. */




/* The original LoongArch Linux clock reader returns two *hardware* values.
 * They cannot be synthesized from compiler temporaries or a normal load.
 * The VDSO's _start reference, conversely, must remain PC-relative: a GOT
 * address would not describe the runtime VDSO image base. These two exact
 * GNU source contracts use the existing typed structured-ASM Core roles. */



LoongLinuxClockAsm loong_linux_clock_form(
    const char *text, size_t length) {
    static const char *const forms[] = {
        "rdtime.d%0,%1", "rdtime.d%[val],%[tid]",
        "la.pcrel%0,_start", "rdtime.d$zero,%0",
        "rdtime.d%0,$zero"
    };
    char compact[96];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return LOONG_CLOCK_NONE;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == ';' || c == '\0' || n + 1U >= sizeof(compact))
            return LOONG_CLOCK_NONE;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i)
        if (strcmp(compact, forms[i]) == 0)
            return (LoongLinuxClockAsm)(i + 1U);
    return LOONG_CLOCK_NONE;
}




static bool loong_linux_clock_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info,
    LoongLinuxClockAsm form) {
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const size_t count = form == LOONG_CLOCK_VDSO_BASE ||
                         form == LOONG_CLOCK_CPU_ID ||
                         form == LOONG_CLOCK_COUNTER_ONLY ? 1U : 2U;
    size_t i;
    if (!out || !info || form == LOONG_CLOCK_NONE ||
        info->is_goto || info->register_clobber_count ||
        info->has_memory_clobber != (form == LOONG_CLOCK_CPU_ID) ||
        ins->value.structured_inline_asm.operand_count != count) return false;
    for (i = 0U; i < count; ++i) {
        const bool word32 = form == LOONG_CLOCK_CPU_ID ||
            (form == LOONG_CLOCK_RDTIME_NUMERIC && i == 1U);
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].has_fixed_register_binding ||
            b[i].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
            !loong_asm_integer(fn, b[i].value, true,
                               word32 ? 4U : 8U, word32 ? 1 : 0))
            return false;
    }
    if (fputs(form == LOONG_CLOCK_VDSO_BASE
              ? "  la.pcrel $t0, _start\n"
              : form == LOONG_CLOCK_CPU_ID
                  ? "  rdtime.d $zero, $t0\n"
                  : form == LOONG_CLOCK_COUNTER_ONLY
                    ? "  rdtime.d $t0, $zero\n"
                  : "  rdtime.d $t0, $t1\n", out) == EOF) return false;
    for (i = 0U; i < count; ++i)
        if (!minic_loongarch64_emit_frame_mem(
                out, "ld.d", "$t2", minic_lp64_frame_value_offset(ctx->frame_plan, b[i].value),
                ctx->dynamic_frame) ||
            !minic_loongarch64_emit_asm_output_store(
                out, fn, b[i].value,
                (form == LOONG_CLOCK_RDTIME_NUMERIC && i == 1U) ||
                form == LOONG_CLOCK_CPU_ID ? 4U : 8U,
                i == 0U ? "$t0" : "$t1", "$t2", "$t3"))
            return false;
    return true;
}

/* Linux's VDSO fallback syscall is a privileged ABI effect, even though the
 * GNU template itself has no textual %operand placeholders. Bind exact
 * physical a7/a0/a1 operands here: treating them as generic scalar inputs
 * would make a convincing .s file that silently passed wrong syscall args. */
bool loong_linux_vdso_syscall_form(const char *text, size_t length) {
    char compact[32];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == ';' || c == '\0' || n + 1U >= sizeof(compact))
            return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    return strcmp(compact, "syscall0") == 0;
}

static bool loong_linux_vdso_clobbers(
    const MinicCoreInlineAsmRegisterClobber *regs, size_t count) {
    size_t i;
    if (!regs || count != 9U) return false;
    for (i = 0U; i < 9U; ++i) {
        const char *s = regs[i].name;
        const size_t n = regs[i].name_length;
        if (!s || n != 3U || s[0] != '$' || s[1] != 't' ||
            s[2] != (char)('0' + i)) return false;
    }
    return true;
}




static bool loong_linux_vdso_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    static const char *const registers[] = {"a0", "a7", "a0", "a1"};
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    size_t i;
    if (!out || !info || !info->is_volatile || info->is_goto ||
        !info->has_memory_clobber ||
        !loong_linux_vdso_syscall_form(info->template_text, info->template_length) ||
        !loong_linux_vdso_clobbers(
            info->register_clobbers, info->register_clobber_count) ||
        ins->value.structured_inline_asm.operand_count != 4U)
        return false;
    for (i = 0U; i < 4U; ++i) {
        const MinicCoreFixedRegisterBinding *fixed;
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].early_clobber || !b[i].has_fixed_register_binding ||
            b[i].fixed_register_binding_id >= fn->fixed_register_binding_count ||
            b[i].kind != (i == 0U
                ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE
                : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT)) return false;
        fixed = &fn->fixed_register_bindings[b[i].fixed_register_binding_id];
        if (!fixed->is_local || !fixed->register_name ||
            fixed->register_name_length != 2U ||
            memcmp(fixed->register_name, registers[i], 2U) != 0 ||
            (i == 0U && !loong_asm_integer(fn, b[i].value, true, 8U, 1)) ||
            (i == 1U && !loong_asm_integer(fn, b[i].value, false, 8U, 1)) ||
            (i == 2U && !loong_asm_pointer(fn, b[i].value, false) &&
             !loong_asm_integer(fn, b[i].value, false, 4U, 1)) ||
            (i == 3U && !loong_asm_pointer(fn, b[i].value, false))) return false;
    }
    /* Materialize the syscall-number and both argument registers from
     * Core SSA, independently of the compiler's scratch register choices. */
    if (!minic_loongarch64_emit_frame_mem(out, "ld.d", "$a7",
            minic_lp64_frame_value_offset(ctx->frame_plan, b[1].value), ctx->dynamic_frame) ||
        !minic_loongarch64_emit_frame_mem(out,
            minic_core_function_value_is_kind(fn, b[2].value, MINIC_CORE_TYPE_POINTER)
                ? "ld.d" : "ld.w", "$a0",
            minic_lp64_frame_value_offset(ctx->frame_plan, b[2].value), ctx->dynamic_frame) ||
        !minic_loongarch64_emit_frame_mem(out, "ld.d", "$a1",
            minic_lp64_frame_value_offset(ctx->frame_plan, b[3].value), ctx->dynamic_frame) ||
        fputs("  syscall 0\n", out) == EOF ||
        !minic_loongarch64_emit_frame_mem(out, "ld.d", "$t2",
            minic_lp64_frame_value_offset(ctx->frame_plan, b[0].value), ctx->dynamic_frame) ||
        fputs("  st.d $a0, $t2, 0\n", out) == EOF) return false;
    return true;
}

/* A legal LoongArch DBAR barrier is a hardware ordering operation, NOT a
 * compiler-only barrier. The original GNU I-constraint accepts a constant
 * unsigned 12-bit hint; hardware semantics are encoded by that hint.
 * Only one exact operand/template family is admitted, never arbitrary GNU
 * instruction-string passthrough. */
bool loongarch64_dbar_hint(int64_t hint) {
    return hint >= 0 && hint <= 2047;
}
bool loongarch64_dbar_template(const char *text, size_t length) {
    static const char prefix[] = "dbar %0";
    size_t i, n;
    if (text == NULL || length < sizeof(prefix) - 1U)
        return false;
    n = sizeof(prefix) - 1U;
    if (memcmp(text, prefix, n) != 0) return false;
    for (i = n; i < length; ++i) {
        if (text[i] != ' ' && text[i] != '\t')
            return false;
    }
    return true;
}

/* Linux's cache flush uses an immediate cache opcode and a GNU ZC memory
 * address. The memory operand provides the ADDRESS of the byte lvalue; the
 * hardware CACOP, not a byte load or an ordinary compiler barrier, performs
 * the cache operation. Refuse any additional instruction or modifier. */
bool loong_linux_cacop_form(const char *text, size_t length) {
    static const char exact[] = "cacop%0,%1";
    char compact[sizeof(exact)];
    size_t i, n = 0U;
    if (!text || !length) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == ';' || c == '\0' || n + 1U >= sizeof(compact))
            return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    return strcmp(compact, exact) == 0;
}





/* Original GNU no-operand DBAR is a literal hardware barrier. This is
 * independent from the existing "dbar %0" I-constraint immediate input.
 * Avoid treating arbitrary templates or unknown DBAR hints as executable. */
const char *loongarch64_literal_dbar(const char *text,size_t length){
    static const char *const allowed[]={"dbar 0","dbar 0x700"};
    char norm[48];size_t i,n=0U;
    if(!text||!length||length>=sizeof(norm))return NULL;
    for(i=0U;i<length;++i){
        const char c=text[i];
        if(c==9||c==10||c==13||c==';')return NULL;
        if(n+1U>=sizeof(norm))return NULL;
        norm[n++]=c;
    }
    while(n!=0U&&norm[n-1U]==' ')--n;
    norm[n]=0;
    for(i=0U;i<sizeof(allowed)/sizeof(allowed[0]);++i)
        if(strcmp(norm,allowed[i])==0)return allowed[i];
    return NULL;
}

/* Typed GNU LoongArch AMO store-only operations from Linux atomic.h.
 * No target-neutral Core opcode is added: the existing structured ASM roles
 * carry the memory address and scalar input, target-owned machine code
 * determines atomicity/ordering. This exact family cannot be dispatched
 * by the generic text-substitution ASM path. */
const struct MinicLoongAtomicTemplate loong_atomic[] = {
    {"amand_db.w$zero,%1,%0", "amand_db.w", 4U},
    {"amor_db.w$zero,%1,%0", "amor_db.w", 4U},
    {"amxor_db.w$zero,%1,%0", "amxor_db.w", 4U},
    {"amadd_db.w$zero,%1,%0", "amadd_db.w", 4U},
    {"amand_db.d$zero,%1,%0", "amand_db.d", 8U},
    {"amor_db.d$zero,%1,%0", "amor_db.d", 8U},
    {"amxor_db.d$zero,%1,%0", "amxor_db.d", 8U},
    {"amadd_db.d$zero,%1,%0", "amadd_db.d", 8U}
};
const struct MinicLoongAtomicTemplate *loong_atomic_template(
    const char *text, size_t length, bool fetch_old_value) {
    char compact[96];
    size_t i, used = 0U;
    if (text == NULL || length == 0U || length >= sizeof(compact))
        return NULL;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            continue;
        if (used + 1U >= sizeof(compact)) return NULL;
        compact[used++] = c;
    }
    compact[used] = '\0';
    for (i = 0U; i < sizeof(loong_atomic) / sizeof(loong_atomic[0]); ++i) {
        char expected[64];
        if (fetch_old_value) {
            const int count = snprintf(expected, sizeof(expected),
                                       "%s%%1,%%2,%%0", loong_atomic[i].instruction);
            if (count <= 0 || (size_t)count >= sizeof(expected)) return NULL;
            if (strcmp(compact, expected) == 0) return &loong_atomic[i];
        } else if (strcmp(compact, loong_atomic[i].text) == 0) {
            return &loong_atomic[i];
        }
    }
    return NULL;
}





/* Linux irqflags.h uses only CSR_CRMD (CSR index zero): a true EL1
 * privileged operation, never an ordinary C load, barrier or passthrough.
 * Match exact GNU named operands and normalized numeric Core templates.
 * The two forms share the existing Core address/scalar constant roles. */
unsigned int loong_csr_template(const char *text, size_t length) {
    static const char *const forms[] = {
        "csrrd%[val],%[reg]", "csrrd%0,%1",
        "csrxchg%[val],%[mask],%[reg]", "csrxchg%0,%1,%2"
    };
    char compact[96];
    size_t i, n = 0U;
    if (!text || length == 0U || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        char c = text[i];
        if (c == ';') return 0U;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i)
        if (strcmp(compact, forms[i]) == 0) return i < 2U ? 1U : 2U;
    return 0U;
}











/* lsdc_probe.c reads a hardware CPUCFG register through GNU register operands.
 * Keep the input register value (rather than substituting a fixed constant),
 * and explicitly lower the native instruction from typed structured roles. */
bool loong_linux_cpucfg_form(const char *text, size_t length) {
    static const char form[] = "cpucfg%0,%1";
    char compact[64];
    size_t i, size = 0U;
    if (!text || !length || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == '\0' || c == ';' || size + 1U >= sizeof(compact))
            return false;
        compact[size++] = c;
    }
    compact[size] = '\0';
    return strcmp(compact, form) == 0;
}




static bool loong_linux_cpucfg_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *operands =
        ins->value.structured_inline_asm.operands;
    if (!out || !fn || !ins || !info || !operands || !info->is_volatile ||
        info->is_goto || info->has_memory_clobber ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 2U ||
        !loong_linux_cpucfg_form(info->template_text, info->template_length) ||
        operands[0].operand_index != 0U || operands[1].operand_index != 1U ||
        operands[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
        operands[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        operands[1].early_clobber ||
        operands[0].has_fixed_register_binding ||
        operands[1].has_fixed_register_binding ||
        operands[0].value >= fn->value_count ||
        operands[1].value >= fn->value_count ||
        !loong_asm_integer(fn, operands[0].value, true, 4U, 0) ||
        !minic_core_function_value_is_kind(fn, operands[1].value, MINIC_CORE_TYPE_INTEGER))
        return false;
    return minic_loongarch64_emit_frame_mem(
               out, "ld.d", "$t1", minic_lp64_frame_value_offset(ctx->frame_plan, operands[1].value),
               ctx->dynamic_frame) &&
           fprintf(out, "  cpucfg $t0, $t1\n") >= 0 &&
           minic_loongarch64_emit_frame_mem(
               out, "ld.d", "$t2", minic_lp64_frame_value_offset(ctx->frame_plan, operands[0].value),
               ctx->dynamic_frame) &&
           minic_loongarch64_emit_asm_output_store(
               out, fn, operands[0].value,
               4U, "$t0", "$t2", "$t3");
}

/* Authentic GNU Linux 6.6.143 LoongArch set_pte global-bit buddy LL/SC.
 * One exact source/machine ISA template; no Core architecture switches. */
bool loong_pte_form(const char *text,size_t len) {
    static const char named[]=
        "1:ll.d%[tmp],%[buddy]bnez%[tmp],2for%[tmp],%[tmp],%[global]"
        "sc.d%[tmp],%[buddy]beqz%[tmp],1bnop2:dbar0x700";
    static const char numeric[]=
        "1:ll.d%1,%0bnez%1,2for%1,%1,%2"
        "sc.d%1,%0beqz%1,1bnop2:dbar0x700";
    char compact[240];
    size_t i,n=0U;
    if(!text||!len||len>=sizeof(compact))return false;
    for(i=0U;i<len;++i){
        char ch=text[i];
        if(ch==' '||ch=='\t'||ch=='\n'||ch=='\r')continue;
        if(n+1U>=sizeof(compact))return false;
        compact[n++]=ch;
    }
    compact[n]='\0';
    return strcmp(compact,named)==0||strcmp(compact,numeric)==0;
}


/* Linux arch_atomic{,64}_fetch_{and,or,xor,add}_relaxed:
 * GNU +ZB memory RMW, =&r old-value output, r input. The SAME hardware
 * AMO template table owns both store-only ($zero) and fetch old-value forms.
 * Core already transports 3 typed roles, so no new Core instruction. */




/* GNU Linux 6.6.143 cmpxchg.h: exact LL/SC hardware shapes. Keep
 * target-only constraint/type qualification separate from Common Core
 * operand address lowering; no C load/store emulation of atomics. */
unsigned loong_linux_cmpxchg_form(const char *text,size_t length) {
    static const char *const exact[]={
        "1:ll.w%0,%2#__cmpxchg_asmbne%0,%z3,2fmove$t0,%z4sc.w$t0,%1beqz$t0,1b2:dbar0x700",
        "1:ll.d%0,%2#__cmpxchg_asmbne%0,%z3,2fmove$t0,%z4sc.d$t0,%1beqz$t0,1b2:dbar0x700",
        "1:ll.w%0,%3and%1,%0,%z4bne%1,%z5,2fandn%1,%0,%z4or%1,%1,%z6sc.w%1,%2beqz%1,1bb3f2:dbar0x7003:"
    };
    char compact[256];size_t i,n=0U;
    if(!text||!length||length>=sizeof(compact))return 0U;
    for(i=0U;i<length;++i){
        const char c=text[i];
        if(c==' '||c=='\t'||c=='\r'||c=='\n')continue;
        if(c=='\0'||c==';'||n+1U>=sizeof(compact))return 0U;
        compact[n++]=c;
    }
    compact[n]='\0';
    for(i=0U;i<3U;++i)if(strcmp(compact,exact[i])==0)return (unsigned)i+1U;
    return 0U;
}






/* Original GNU Linux 6.6.143 __xchg() amswap_db.w/d returns the old
 * memory value in a GPR and writes a replacement atomically with a
 * hardware DB fence. Common Core carries the same typed structured
 * memory/read-write, register-output and scalar-input operands. */
unsigned loong_linux_xchg_form(const char *text,size_t length){
    static const char *const forms[]={
        /* arch/loongarch/include/asm/cmpxchg.h __xchg_asm */
        "amswap_db.w%1,%z2,%0","amswap_db.d%1,%z2,%0",
        /* arch/loongarch/include/asm/barrier.h __smp_store_mb.
         * The GNU named form has an unsigned-long scratch and NO explicit
         * clobber; Core supplies the implicit +ZB memory effect. */
        "amswap_db.w%[tmp],%[val],%[mem]",
        "amswap_db.d%[tmp],%[val],%[mem]",
        /* Checked numeric representation after Core name resolution. */
        "amswap_db.w%1,%2,%0","amswap_db.d%1,%2,%0",
        /* Linux relaxed xchg is an atomic swap without the DB fence. */
        "amswap.w%1,%z2,%0","amswap.d%1,%z2,%0",
        /* The local atomic fetch operations have the SAME Core operand
         * roles as xchg: +ZB memory, old GPR output, scalar input. Only
         * the hardware AMO operation differs; never emulate a RMW in C. */
        "amadd.w%1,%2,%0","amadd.d%1,%2,%0",
        "amand.w%1,%2,%0","amand.d%1,%2,%0",
        "amor.w%1,%2,%0","amor.d%1,%2,%0",
        "amxor.w%1,%2,%0","amxor.d%1,%2,%0"
    };
    char compact[96];size_t n=0U,i;
    if(!text||!length||length>=sizeof(compact))return 0U;
    for(i=0U;i<length;++i){
        const char c=text[i];
        if(c==' '||c=='\t'||c=='\r'||c=='\n')continue;
        if(c=='\0'||c==';'||n+1U>=sizeof(compact))return 0U;
        compact[n++]=c;
    }
    compact[n]='\0';
    for(i=0U;i<sizeof(forms)/sizeof(forms[0]);++i)
        if(strcmp(compact,forms[i])==0)return (unsigned)i+1U;
    return 0U;
}



/* Linux __xchg_small() does NOT use AMSWAP_DB: byte/halfword exchange
 * reserves the naturally aligned enclosing u32 and masks just the selected
 * subword. Match its exact GNU LL/SC template and three independent early
 * clobber / memory outputs. A change in the mask, branch, or LL/SC status
 * choreography is a different program and must be rejected. */
bool loong_linux_xchg_small_form(const char *text,size_t length){
    static const char exact[]=
        "1:ll.w%0,%3andn%1,%0,%z4or%1,%1,%z5sc.w%1,%2beqz%1,1b";
    char compact[160];size_t i,n=0U;
    if(!text||!length||length>=sizeof(compact))return false;
    for(i=0U;i<length;++i){
        const char ch=text[i];
        if(ch==' '||ch=='\t'||ch=='\r'||ch=='\n')continue;
        if(ch=='\0'||ch==';'||n+1U>=sizeof(compact))return false;
        compact[n++]=ch;
    }
    compact[n]='\0';
    return strcmp(compact,exact)==0;
}



/* Linux 6.6.143 arch/loongarch/include/asm/barrier.h:
 * array_index_mask_nospec has exactly these two physical instructions.
 * GCC GNU constraints =r, r, r are transported as Core operand roles.
 * The compare computes the unsigned flag; sub.d produces the 0/~0 mask.
 * No invented portable instruction or open-ended inline ASM passthrough. */
bool loong_mask_nospec_form(const char *text, size_t length) {
    static const char exact[] = "sltu%0,%1,%2sub.d%0,$zero,%0";
    char compact[128];
    size_t i, used = 0U;
    if (text == NULL || length == 0U || length >= sizeof(compact))
        return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            continue;
        if (c == ';' || c == '\0' || used + 1U >= sizeof(compact))
            return false;
        compact[used++] = c;
    }
    compact[used] = '\0';
    return strcmp(compact, exact) == 0;
}




static bool loong_mask_nospec_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const MinicCoreFunction *fn = ctx->fn;
    size_t i;
    if (!info->is_volatile || info->is_goto ||
        info->has_memory_clobber || info->register_clobber_count != 0U ||
        ins->value.structured_inline_asm.operand_count != 3U ||
        !loong_mask_nospec_form(info->template_text, info->template_length))
        return false;
    for (i = 0U; i < 3U; ++i) {
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].early_clobber || b[i].has_fixed_register_binding ||
            b[i].kind != (i == 0U
                ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
                : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
            return false;
        if (!loong_asm_integer(fn, b[i].value, i == 0U, 8U, 0))
            return false;
    }
    /* Frame helper owns large/static/dynamic offsets; $t3 is reserved for
     * address materialization. No temporary overlap with $t0/$t1/$t2. */
    return minic_loongarch64_emit_frame_mem(
               out, "ld.d", "$t0", minic_lp64_frame_value_offset(ctx->frame_plan, b[1].value),
               ctx->dynamic_frame) &&
           minic_loongarch64_emit_frame_mem(
               out, "ld.d", "$t1", minic_lp64_frame_value_offset(ctx->frame_plan, b[2].value),
               ctx->dynamic_frame) &&
           fputs("  sltu $t2, $t0, $t1\n"
                 "  sub.d $t2, $zero, $t2\n", out) != EOF &&
           minic_loongarch64_emit_frame_mem(
               out, "ld.d", "$t1", minic_lp64_frame_value_offset(ctx->frame_plan, b[0].value),
               ctx->dynamic_frame) &&
           minic_loongarch64_emit_asm_output_store(
               out, fn, b[0].value,
               8U, "$t2", "$t1", "$t3");
}

/* Original 6.6.143 LoongArch generic per-CPU and FPU-state hardware ASM.
 * All forms are exact qualified target contracts, NEVER a raw text escape.
 * The Core representation remains typed output-lvalue and scalar inputs.
 * $r21 is the kernel per-CPU base register, not a generic C pointer. */
MinicLoongPerCpuIndexedForm loong_percpu_indexed_form(
    const char *text, size_t length) {
    static const char *const words[] = {
        "ldx.b%[ret],$r21,%[ptr]", "ldx.h%[ret],$r21,%[ptr]",
        "ldx.w%[ret],$r21,%[ptr]", "ldx.d%[ret],$r21,%[ptr]",
        "stx.b%[val],$r21,%[ptr]", "stx.h%[val],$r21,%[ptr]",
        "stx.w%[val],$r21,%[ptr]", "stx.d%[val],$r21,%[ptr]"
    };
    char compact[96];
    size_t i, n = 0U;
    if (!text || length == 0U || length >= sizeof(compact)) return LOONG_PERCPU_NONE;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == ';' || c == '\0' || n + 1U >= sizeof(compact))
            return LOONG_PERCPU_NONE;
        compact[n++] = c;
    }
    compact[n] = '\0';
    /* Shared Core canonicalizes GNU named slots to %0/%1, but the
     * target must also qualify ORIGINAL named source templates. Both
     * forms name exactly the same two typed operand roles. */
    for (i = 0U; i < sizeof(words)/sizeof(words[0]); ++i) {
        char numeric[48];
        const char *const name = i < 4U ?
            (i == 0U ? "ldx.b" : i == 1U ? "ldx.h" :
             i == 2U ? "ldx.w" : "ldx.d") :
            (i == 4U ? "stx.b" : i == 5U ? "stx.h" :
             i == 6U ? "stx.w" : "stx.d");
        if (strcmp(compact, words[i]) == 0) return (MinicLoongPerCpuIndexedForm)(i + 1U);
        if (snprintf(numeric,sizeof(numeric), "%s%%0,$r21,%%1",name) > 0 &&
            strcmp(compact,numeric) == 0)
            return (MinicLoongPerCpuIndexedForm)(i + 1U);
    }
    return LOONG_PERCPU_NONE;
}


static bool loong_percpu_indexed_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicLoongPerCpuIndexedForm form =
        loong_percpu_indexed_form(info->template_text, info->template_length);
    const bool read = form >= LOONG_PERCPU_READ_B && form <= LOONG_PERCPU_READ_D;
    static const char *const mnemonics[] = {
        "ldx.b", "ldx.h", "ldx.w", "ldx.d",
        "stx.b", "stx.h", "stx.w", "stx.d"
    };
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    size_t i;
    if (!out || !ctx || !fn || !info || form == LOONG_PERCPU_NONE ||
        !info->is_volatile || info->is_goto ||
        !info->has_memory_clobber || info->register_clobber_count != 0U ||
        ins->value.structured_inline_asm.operand_count != 2U) return false;
    for (i = 0U; i < 2U; ++i)
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            (b[i].early_clobber && !(read && i == 0U)) ||
            b[i].has_fixed_register_binding ||
            b[i].kind != (read && i == 0U
                ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
                : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
            return false;
    if (!loong_asm_integer(fn, b[0].value, read, 8U, 0))
        return false;
    if (!minic_core_function_value_is_kind(fn, b[1].value, MINIC_CORE_TYPE_POINTER))
        return false;
    if (!minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t1", minic_lp64_frame_value_offset(ctx->frame_plan, b[1].value), ctx->dynamic_frame))
        return false;
    if (read)
        return fprintf(out, "  %s $t0, $r21, $t1\n",
                       mnemonics[(size_t)form-1U]) >= 0 &&
               minic_loongarch64_emit_frame_mem(
                   out, "ld.d", "$t1", minic_lp64_frame_value_offset(ctx->frame_plan, b[0].value),
                   ctx->dynamic_frame) &&
               minic_loongarch64_emit_asm_output_store(
                   out, fn, b[0].value,
                   8U, "$t0", "$t1", "$t3");
    return minic_loongarch64_emit_frame_mem(
               out, "ld.d", "$t0", minic_lp64_frame_value_offset(ctx->frame_plan, b[0].value),ctx->dynamic_frame) &&
           fprintf(out, "  %s $t0, $r21, $t1\n",
                   mnemonics[(size_t)form-1U]) >= 0;
}

/* Exact GNU per-CPU am{add,and,or}.w/.d: the first output is OLD
 * memory, second +ZB output is updated memory, third argument is VAL.
 * Unlike _db forms this is intentionally non-barrier and GNU declares
 * no memory clobber. A spelling or operand-order mismatch must reject. */
static const struct { const char *compact, *mnemonic; size_t width; }
loong_percpu_amo[] = {
    {"amadd.w%[ret],%[val],%[ptr]", "amadd.w", 4U},
    {"amadd.d%[ret],%[val],%[ptr]", "amadd.d", 8U},
    {"amand.w%[ret],%[val],%[ptr]", "amand.w", 4U},
    {"amand.d%[ret],%[val],%[ptr]", "amand.d", 8U},
    {"amor.w%[ret],%[val],%[ptr]", "amor.w", 4U},
    {"amor.d%[ret],%[val],%[ptr]", "amor.d", 8U}
};
size_t loong_percpu_amo_width(size_t form) {
    if (form >= sizeof(loong_percpu_amo) / sizeof(loong_percpu_amo[0]))
        return 0U;
    return loong_percpu_amo[form].width;
}
size_t loong_percpu_amo_form(const char *text, size_t length) {
    char compact[96];
    size_t i,n=0U;
    if (!text || !length || length >= sizeof(compact)) return SIZE_MAX;
    for(i=0U;i<length;++i) {
        const char c=text[i];
        if(c==' '||c=='\n'||c=='\r'||c=='\t')continue;
        if(c==';'||c=='\0'||n+1U>=sizeof(compact))return SIZE_MAX;
        compact[n++]=c;
    }
    compact[n]='\0';
    for (i=0U;i<sizeof(loong_percpu_amo)/sizeof(loong_percpu_amo[0]);++i) {
        char numeric[64];
        if(strcmp(compact,loong_percpu_amo[i].compact)==0)return i;
        if(snprintf(numeric,sizeof(numeric),"%s%%0,%%2,%%1",
                    loong_percpu_amo[i].mnemonic)>0 &&
           strcmp(compact,numeric)==0)return i;
    }
    return SIZE_MAX;
}




static bool loong_percpu_amo_emit(
    FILE *out,const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins,const MinicCoreInlineAsm *info) {
    const size_t form=loong_percpu_amo_form(
        info->template_text,info->template_length);
    const MinicCoreFunction *fn=ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *b=
        ins->value.structured_inline_asm.operands;
    size_t i;
    /* The ORIGINAL GNU source has NO compiler-wide memory clobber, only
     * the +ZB memory operand. Shared Core deliberately upgrades the ABI-
     * neutral structured effect to has_memory_clobber=true whenever any
     * operand is MEMORY_READWRITE. This is semantic memory-effect metadata,
     * not evidence that source added an extra clobber. Source admission
     * already rejected an explicit \"memory\" clobber or other mutations. */
    if(!out||!ctx||!fn||!info||form==SIZE_MAX||
       !info->is_volatile||info->is_goto||
       !info->has_memory_clobber||info->register_clobber_count!=0U||
       ins->value.structured_inline_asm.operand_count!=3U)
        return false;
    for(i=0U;i<3U;++i)
        if(b[i].operand_index!=i||b[i].value>=fn->value_count||
           (b[i].early_clobber && i!=0U)||
           b[i].has_fixed_register_binding||
           b[i].kind!=(i==0U?MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
                        i==1U?MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
                              MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
            return false;
    if(!loong_asm_integer(fn,b[0].value,true,8U,-1)||
       !loong_asm_integer(fn,b[1].value,true,loong_percpu_amo[form].width,-1)||
       !loong_asm_integer(fn,b[2].value,false,8U,-1))
        return false;
    return minic_loongarch64_emit_frame_mem(
             out,"ld.d","$t1",minic_lp64_frame_value_offset(ctx->frame_plan, b[1].value),ctx->dynamic_frame)&&
           minic_loongarch64_emit_frame_mem(
             out,"ld.d","$t0",minic_lp64_frame_value_offset(ctx->frame_plan, b[2].value),ctx->dynamic_frame)&&
           fprintf(out,"  %s $t2, $t0, $t1\n",
                   loong_percpu_amo[form].mnemonic)>=0&&
           minic_loongarch64_emit_frame_mem(
             out,"ld.d","$t1",minic_lp64_frame_value_offset(ctx->frame_plan, b[0].value),ctx->dynamic_frame)&&
           minic_loongarch64_emit_asm_output_store(
             out,fn,b[0].value,
             8U,"$t2","$t1","$t3");
}
bool loong_fcsr0_write_form(const char *text,size_t len){
    static const char exact[]="movgr2fcsr$fcsr0,%0";
    char norm[64];size_t i,n=0U;
    if(!text||len==0U||len>=sizeof(norm))return false;
    for(i=0U;i<len;++i) {
        const char c=text[i];
        if(c==' '||c=='\n'||c=='\r'||c=='\t')continue;
        if(c==';'||c=='\0'||n+1U>=sizeof(norm))return false;
        norm[n++]=c;
    }
    norm[n]='\0';
    return strcmp(norm,exact)==0;
}


static bool loong_fcsr0_write_emit(
    FILE *out,const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins,const MinicCoreInlineAsm *info) {
    const MinicCoreFunction *fn=ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *b=
        ins->value.structured_inline_asm.operands;
    const MinicCoreType *plain;
    if(!out||!ctx||!fn||!info||!info->is_volatile||info->is_goto||
       info->has_memory_clobber||info->register_clobber_count!=0U||
       !loong_fcsr0_write_form(info->template_text,info->template_length)||
       ins->value.structured_inline_asm.operand_count!=1U||
       b[0].operand_index!=0U||b[0].value>=fn->value_count||
       b[0].kind!=MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT||
       b[0].early_clobber||b[0].has_fixed_register_binding||
       !(plain=loong_asm_value_type(fn,b[0].value,false))||
       plain->kind!=MINIC_CORE_TYPE_INTEGER||plain->value.integer.is_bool||
       (plain->size!=4U&&plain->size!=8U))
        return false;
    return minic_loongarch64_emit_frame_mem(
             out,plain->size==8U?"ld.d":"ld.w","$t0",
             minic_lp64_frame_value_offset(ctx->frame_plan, b[0].value),
             ctx->dynamic_frame)&&
           fputs("  movgr2fcsr $fcsr0, $t0\n",out)!=EOF;
}

/* Linux unaligned-trap emulation selects one of 32 physical FPRs at
 * runtime. The GNU transfer is a 64-bit bit-preserving FP/GPR move, not
 * numeric floating-point conversion; recognize all F0..F31 in both
 * directions with one register-indexed target contract. */
unsigned int loong_linux_fpr_transfer_form(
    const char *text, size_t length) {
    char compact[96], expected[96];
    size_t i, n = 0U, reg;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || c == ';' || n + 1U >= sizeof(compact))
            return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (reg = 0U; reg < 32U; ++reg) {
        int written = snprintf(expected, sizeof(expected),
            "movfr2gr.d%%0,$f%zu", reg);
        if (written > 0 && (size_t)written < sizeof(expected) &&
            strcmp(compact, expected) == 0) return (unsigned int)reg + 1U;
        written = snprintf(expected, sizeof(expected),
            "movgr2fr.d$f%zu,%%0", reg);
        if (written > 0 && (size_t)written < sizeof(expected) &&
            strcmp(compact, expected) == 0) return (unsigned int)reg + 33U;
    }
    return 0U;
}




static bool loong_linux_fpr_transfer_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreFunction *fn;
    const MinicCoreStructuredInlineAsmOperand *b;
    const unsigned int form = info ? loong_linux_fpr_transfer_form(
        info->template_text, info->template_length) : 0U;
    const bool read = form <= 32U;
    const unsigned int reg = (form - 1U) % 32U;
    if (!out || !ctx || !ctx->fn || !ins || !info || !form ||
        !info->is_volatile || info->is_goto || info->has_memory_clobber ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 1U)
        return false;
    fn = ctx->fn;
    b = ins->value.structured_inline_asm.operands;
    if (!b || b[0].operand_index || b[0].early_clobber ||
        b[0].has_fixed_register_binding || b[0].value >= fn->value_count ||
        b[0].kind != (read ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
        return false;
    if (read) {
        if (!loong_asm_integer(fn, b[0].value, true, 8U, 0))
            return false;
        return fprintf(out, "  movfr2gr.d $t0, $f%u\n", reg) >= 0 &&
            minic_loongarch64_emit_frame_mem(out, "ld.d", "$t1",
                minic_lp64_frame_value_offset(ctx->frame_plan, b[0].value), ctx->dynamic_frame) &&
            minic_loongarch64_emit_asm_output_store(
                out, fn, b[0].value,
                8U, "$t0", "$t1", "$t3");
    }
    return loong_asm_integer(fn, b[0].value, false, 8U, 0) &&
        minic_loongarch64_emit_frame_mem(out, "ld.d", "$t0",
            minic_lp64_frame_value_offset(ctx->frame_plan, b[0].value), ctx->dynamic_frame) &&
        fprintf(out, "  movgr2fr.d $f%u, $t0\n", reg) >= 0;
}

/* Linux LoongArch __get_user/__put_user has a hardware faulting load/store
 * coupled to an exception-table register fixup. The generated .equ/.irp
 * register map is part of the ABI, not optional decoration. Match its full
 * GNU source form and then emit with known, target-owned physical registers.
 * Unrecognized templates remain unsupported instead of being passed through
 * as an untracked opaque machine side effect. */
/* Architectural INVTLB has three GNU operand shapes: ASID+VA, VA-only,
 * and ASID-only. Both zero-register variants are real TLB operations, not
 * CPU fences. Keep the immediate opcode range and Linux-defined encodings
 * in this target instead of allowing an arbitrary textual instruction. */
unsigned int loong_linux_invtlb_form(const char *text, size_t length) {
    static const char *const forms[] = {
        "invtlb%0,%1,%2", "invtlb%0,$zero,%1",
        "invtlb%0,%1,$zero", "invtlb%0,$zero,$zero"
    };
    char compact[90];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i)
        if (strcmp(compact, forms[i]) == 0) return (unsigned int)i + 1U;
    return 0U;
}

static bool loong_linux_invtlb_opcode(int64_t value) {
    return (value >= 0 && value <= 6) ||
           (value >= 9 && value <= 14) ||
           (value >= 16 && value <= 22);
}




static bool loong_linux_invtlb_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info,
    unsigned int form) {
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const MinicCoreInstruction *def;
    MinicCoreInstructionId definition;
    size_t i;
    if (!out || !ctx || !fn || !info || !form || form > 4U ||
        !info->is_volatile || info->is_goto ||
        !info->has_memory_clobber || info->register_clobber_count != 0U ||
        ins->value.structured_inline_asm.operand_count !=
            (form == 1U ? 3U : form == 4U ? 1U : 2U)) return false;
    for (i = 0U; i < ins->value.structured_inline_asm.operand_count; ++i)
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
            b[i].has_fixed_register_binding || b[i].early_clobber)
            return false;
    definition = fn->values[b[0].value].definition;
    if (definition >= fn->instruction_count) return false;
    def = &fn->instructions[definition];
    if (def->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT ||
        def->result != b[0].value ||
        !loong_linux_invtlb_opcode(def->value.integer_value)) return false;
    if (form == 1U || form == 3U) {
        if (!minic_loongarch64_emit_frame_mem(
                out, "ld.d", "$t0", minic_lp64_frame_value_offset(ctx->frame_plan, b[1].value),
                ctx->dynamic_frame)) return false;
    } else if (fputs("  or $t0, $zero, $zero\n", out) == EOF) return false;
    if (form == 1U || form == 2U) {
        if (!minic_loongarch64_emit_frame_mem(
                out, "ld.d", "$t1",
                minic_lp64_frame_value_offset(ctx->frame_plan, b[form == 1U ? 2U : 1U].value),
                ctx->dynamic_frame)) return false;
    } else if (fputs("  or $t1, $zero, $zero\n", out) == EOF) return false;
    return fprintf(out, "  invtlb %lld, $t0, $t1\n",
                   (long long)def->value.integer_value) >= 0;
}

/* The 32/64-bit Linux fetch-add-unless primitives are a single LL/SC
 * semantic family. Their memory output is marked =ZB by GNU, although the
 * hardware both reads and writes that address. Core must retain the actual
 * RMW effect; the target owns branch, reservation and barrier sequencing. */
unsigned int loong_linux_add_unless_form(
    const char *text, size_t length) {
    static const char *const forms[] = {
        "0:ll.w%[p],%[c]beq%[p],%[u],1fadd.w%[rc],%[p],%[a]sc.w%[rc],%[c]beqz%[rc],0bb2f1:dbar0x7002:",
        "0:ll.d%[p],%[c]beq%[p],%[u],1fadd.d%[rc],%[p],%[a]sc.d%[rc],%[c]beqz%[rc],0bb2f1:dbar0x7002:",
        "0:ll.w%0,%2beq%0,%4,1fadd.w%1,%0,%3sc.w%1,%2beqz%1,0bb2f1:dbar0x7002:",
        "0:ll.d%0,%2beq%0,%4,1fadd.d%1,%0,%3sc.d%1,%2beqz%1,0bb2f1:dbar0x7002:"
    };
    char compact[260];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return 0U;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i)
        if (strcmp(compact, forms[i]) == 0) return (unsigned int)(i % 2U) + 1U;
    return 0U;
}




static bool loong_linux_add_unless_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info,
    unsigned int form) {
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const size_t width = form == 1U ? 4U : 8U;
    const char *suffix = form == 1U ? "w" : "d";
    size_t i;
    if (!out || !ctx || !fn || !info || (form != 1U && form != 2U) ||
        !info->is_volatile || info->is_goto ||
        info->register_clobber_count != 0U ||
        ins->value.structured_inline_asm.operand_count != 5U) return false;
    for (i = 0U; i < 5U; ++i) {
        const MinicCoreStructuredInlineAsmOperandKind expected = i == 2U
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE
            : i < 3U ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
                     : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].kind != expected || b[i].has_fixed_register_binding ||
            !loong_asm_integer(fn, b[i].value, i < 3U, width, -1))
            return false;
    }
    if (!minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t2", minic_lp64_frame_value_offset(ctx->frame_plan, b[2].value),
            ctx->dynamic_frame) ||
        !minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t4", minic_lp64_frame_value_offset(ctx->frame_plan, b[3].value),
            ctx->dynamic_frame) ||
        !minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t5", minic_lp64_frame_value_offset(ctx->frame_plan, b[4].value),
            ctx->dynamic_frame) ||
        fprintf(out,
            "  or $t1, $zero, $zero\n"
            "0: ll.%s $t0, $t2, 0\n"
            "  beq $t0, $t5, 1f\n"
            "  add.%s $t1, $t0, $t4\n"
            "  sc.%s $t1, $t2, 0\n"
            "  beqz $t1, 0b\n"
            "  b 2f\n"
            "1: dbar 0x700\n"
            "2:\n", suffix, suffix, suffix) < 0 ||
        !minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t7", minic_lp64_frame_value_offset(ctx->frame_plan, b[0].value),
            ctx->dynamic_frame) ||
        fprintf(out, "  st.%s $t0, $t7, 0\n", suffix) < 0 ||
        !minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t7", minic_lp64_frame_value_offset(ctx->frame_plan, b[1].value),
            ctx->dynamic_frame) ||
        fprintf(out, "  st.%s $t1, $t7, 0\n", suffix) < 0) return false;
    return true;
}

/* Linux atomic{,64}_sub_if_positive: the immediate and register variants
 * share the same reservation loop and negative-result exit. The original
 * GNU input I holds the negated decrement (ADD); r holds the positive
 * decrement (SUB). Preserve that distinction in a target-owned descriptor. */

bool loong_linux_sub_positive_form(
    const char *text, size_t length, LoongLinuxSubPositive *result) {
    static const struct {
        const char *compact;
        size_t width;
        bool add_negated;
    } forms[] = {
        {"1:ll.w%1,%2#atomic_sub_if_positiveaddi.w%0,%1,%3move%1,%0bltz%0,2fsc.w%1,%2beqz%1,1b2:dbar0x700",4U,true},
        {"1:ll.w%1,%2#atomic_sub_if_positivesub.w%0,%1,%3move%1,%0bltz%0,2fsc.w%1,%2beqz%1,1b2:dbar0x700",4U,false},
        {"1:ll.d%1,%2#atomic64_sub_if_positiveaddi.d%0,%1,%3move%1,%0bltz%0,2fsc.d%1,%2beqz%1,1b2:dbar0x700",8U,true},
        {"1:ll.d%1,%2#atomic64_sub_if_positivesub.d%0,%1,%3move%1,%0bltz%0,2fsc.d%1,%2beqz%1,1b2:dbar0x700",8U,false}
    };
    char compact[260];
    size_t i, n = 0U;
    if (!text || !length || !result || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i)
        if (strcmp(compact, forms[i].compact) == 0) {
            result->width = forms[i].width;
            result->add_negated = forms[i].add_negated;
            return true;
        }
    return false;
}




static bool loong_linux_sub_positive_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info,
    const LoongLinuxSubPositive *form) {
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const char *suffix = form && form->width == 4U ? "w" : "d";
    size_t i;
    if (!out || !ctx || !fn || !info || !form ||
        (form->width != 4U && form->width != 8U) ||
        !info->is_volatile || info->is_goto ||
        info->register_clobber_count != 0U ||
        ins->value.structured_inline_asm.operand_count != 4U) return false;
    for (i = 0U; i < 4U; ++i) {
        const MinicCoreStructuredInlineAsmOperandKind expected = i == 2U
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE
            : i == 3U ? MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT
                      : MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].kind != expected || b[i].has_fixed_register_binding ||
            !loong_asm_integer(fn, b[i].value, i < 3U, form->width, -1))
            return false;
    }
    if (!minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t4", minic_lp64_frame_value_offset(ctx->frame_plan, b[2].value),
            ctx->dynamic_frame) ||
        !minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t5", minic_lp64_frame_value_offset(ctx->frame_plan, b[3].value),
            ctx->dynamic_frame) ||
        fprintf(out,
            "1: ll.%s $t0, $t4, 0\n"
            "  %s.%s $t1, $t0, $t5\n"
            "  or $t2, $t1, $zero\n"
            "  bltz $t1, 2f\n"
            "  sc.%s $t2, $t4, 0\n"
            "  beqz $t2, 1b\n"
            "2: dbar 0x700\n", suffix,
            form->add_negated ? "add" : "sub", suffix, suffix) < 0 ||
        !minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t7", minic_lp64_frame_value_offset(ctx->frame_plan, b[0].value),
            ctx->dynamic_frame) ||
        fprintf(out, "  st.%s $t1, $t7, 0\n", suffix) < 0 ||
        !minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t7", minic_lp64_frame_value_offset(ctx->frame_plan, b[1].value),
            ctx->dynamic_frame) ||
        fprintf(out, "  st.%s $t2, $t7, 0\n", suffix) < 0) return false;
    return true;
}


/* This register numbering is the Linux LoongArch exception-table ABI. It is
 * shared by user access and futex atomic LL/SC, so recognize it once rather
 * than relaxing each operation to an unrestricted `.irp` assembler escape. */
static const char loong_linux_exception_gpr_map[] =
    ".equ.L__gpr_num_zero,0"
    ".irpnum,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31"
    ".equ.L__gpr_num_$r\\num,\\num"
    ".endr"
    ".equ.L__gpr_num_$ra,1"
    ".equ.L__gpr_num_$tp,2"
    ".equ.L__gpr_num_$sp,3"
    ".irpnum,0,1,2,3,4,5,6,7"
    ".equ.L__gpr_num_$a\\num,4+\\num"
    ".endr"
    ".irpnum,0,1,2,3,4,5,6,7,8"
    ".equ.L__gpr_num_$t\\num,12+\\num"
    ".endr"
    ".equ.L__gpr_num_$s9,22"
    ".equ.L__gpr_num_$fp,22"
    ".irpnum,0,1,2,3,4,5,6,7,8"
    ".equ.L__gpr_num_$s\\num,23+\\num"
    ".endr";


/* All five futex operations use one precise GNU template family. In
 * particular, .short (2) is the LoongArch EX_TYPE_UACCESS_ERR_ZERO exception
 * handler, not a passive note. Both LL and SC have distinct fault entries;
 * their fixup register must hold -EFAULT on failure. */
LoongLinuxFutexRmw loong_linux_futex_rmw_form(
    const char *text, size_t length) {
    static const char *const operations[] = {
        "move$t0,%z5", "add.w$t0,%1,%z5", "or$t0,%1,%z5",
        "and$t0,%1,%z5", "xor$t0,%1,%z5"
    };
    static const char first[] = "1:ll.w%1,%4#__futex_atomic_op";
    static const char loop[] = "2:sc.w$t0,%2beqz$t0,1b3:";
    static const char exception_prefix[] =
        ".pushsection__ex_table,\"a\".balign4";
    static const char exception_suffix[] =
        ".long((3b)-.).short(2)"
        ".short((((.L__gpr_num_%0)<<0)|((.L__gpr_num_zero)<<5)))"
        ".popsection";
    char compact[3600];
    size_t n = 0U, i, form;
    const char *cursor;
    if (!text || length == 0U || length >= sizeof(compact))
        return LOONG_FUTEX_NONE;
    for (i = 0U; i < length; ++i) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
        if (ch == '\0' || n + 1U >= sizeof(compact)) return LOONG_FUTEX_NONE;
        compact[n++] = ch;
    }
    compact[n] = '\0';
    if (strncmp(compact, first, sizeof(first) - 1U) != 0)
        return LOONG_FUTEX_NONE;
    for (form = 0U; form < sizeof(operations) / sizeof(operations[0]); ++form) {
        const size_t op_len = strlen(operations[form]);
        cursor = compact + sizeof(first) - 1U;
        if (strncmp(cursor, operations[form], op_len) != 0) continue;
        cursor += op_len;
        if (strncmp(cursor, loop, sizeof(loop) - 1U) != 0) continue;
        cursor += sizeof(loop) - 1U;
        for (i = 0U; i < 2U; ++i) {
            const size_t map_len = sizeof(loong_linux_exception_gpr_map) - 1U;
            if (strncmp(cursor, loong_linux_exception_gpr_map, map_len) != 0)
                break;
            cursor += map_len;
            if (strncmp(cursor, exception_prefix,
                        sizeof(exception_prefix) - 1U) != 0) break;
            cursor += sizeof(exception_prefix) - 1U;
            if (strncmp(cursor, i == 0U ? ".long((1b)-.)" : ".long((2b)-.)",
                        sizeof(".long((1b)-.)") - 1U) != 0) break;
            cursor += sizeof(".long((1b)-.)") - 1U;
            if (strncmp(cursor, exception_suffix,
                        sizeof(exception_suffix) - 1U) != 0) break;
            cursor += sizeof(exception_suffix) - 1U;
        }
        if (i == 2U && *cursor == '\0')
            return (LoongLinuxFutexRmw)(form + 1U);
    }
    return LOONG_FUTEX_NONE;
}

/* The original futex compare/exchange uses the same fault-table encoding as
 * futex RMW, but a distinct comparison branch and three GNU outputs. Do not
 * admit arbitrary .irp / .short assembler text under this template. */
bool loong_linux_futex_cmpxchg_form(const char *text, size_t length) {
    static const char prefix[] =
        "#futex_atomic_cmpxchg_inatomic"
        "1:ll.w%1,%3bne%1,%z4,3fmove$t0,%z5"
        "2:sc.w$t0,%2beqz$t0,1b3:dbar0x700";
    static const char section[] = ".pushsection__ex_table,\"a\".balign4";
    static const char suffix[] =
        ".long((3b)-.).short(2)"
        ".short((((.L__gpr_num_%0)<<0)|((.L__gpr_num_zero)<<5)))"
        ".popsection";
    char compact[3600];
    const char *cursor;
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    if (strncmp(compact, prefix, sizeof(prefix) - 1U) != 0) return false;
    cursor = compact + sizeof(prefix) - 1U;
    for (i = 0U; i < 2U; ++i) {
        const size_t map_len = sizeof(loong_linux_exception_gpr_map) - 1U;
        const char *site = i == 0U ? ".long((1b)-.)" : ".long((2b)-.)";
        if (strncmp(cursor, loong_linux_exception_gpr_map, map_len) != 0)
            return false;
        cursor += map_len;
        if (strncmp(cursor, section, sizeof(section) - 1U) != 0)
            return false;
        cursor += sizeof(section) - 1U;
        if (strncmp(cursor, site, strlen(site)) != 0) return false;
        cursor += strlen(site);
        if (strncmp(cursor, suffix, sizeof(suffix) - 1U) != 0) return false;
        cursor += sizeof(suffix) - 1U;
    }
    return *cursor == '\0';
}







bool loong_linux_uaccess_form(
    const char *text, size_t length, LoongLinuxUaccess *result) {
    static const struct {
        const char *prefix;
        bool load;
        unsigned int width;
        const char *opcode;
    } forms[] = {
        {"1:ld.b%1,%22:", true, 1U, "ld.b"},
        {"1:ld.h%1,%22:", true, 2U, "ld.h"},
        {"1:ld.w%1,%22:", true, 4U, "ld.w"},
        {"1:ld.d%1,%22:", true, 8U, "ld.d"},
        {"1:st.b%z2,%1#__put_user_asm2:", false, 1U, "st.b"},
        {"1:st.h%z2,%1#__put_user_asm2:", false, 2U, "st.h"},
        {"1:st.w%z2,%1#__put_user_asm2:", false, 4U, "st.w"},
        {"1:st.d%z2,%1#__put_user_asm2:", false, 8U, "st.d"}
    };
    static const char common_tail[] =
        ".pushsection__ex_table,\"a\""
        ".balign4"
        ".long((1b)-.)"
        ".long((2b)-.)"
        ".short(2)";
    static const char load_tail[] =
        ".short((((.L__gpr_num_%0)<<0)|((.L__gpr_num_%1)<<5)))"
        ".popsection";
    static const char store_tail[] =
        ".short((((.L__gpr_num_%0)<<0)|((.L__gpr_num_zero)<<5)))"
        ".popsection";
    char compact[1500];
    size_t i, n = 0U;

    if (!text || !length || length >= sizeof(compact) || !result) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '\0' || n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i) {
        const size_t prefix = strlen(forms[i].prefix);
        const size_t map_len = sizeof(loong_linux_exception_gpr_map) - 1U;
        const size_t middle = sizeof(common_tail) - 1U;
        const char *tail = forms[i].load ? load_tail : store_tail;
        const size_t suffix = strlen(tail);
        if (n != prefix + map_len + middle + suffix ||
            memcmp(compact, forms[i].prefix, prefix) != 0 ||
            memcmp(compact + prefix, loong_linux_exception_gpr_map, map_len) != 0 ||
            memcmp(compact + prefix + map_len, common_tail, middle) != 0 ||
            memcmp(compact + prefix + map_len + middle, tail, suffix) != 0)
            continue;
        result->load = forms[i].load;
        result->width = forms[i].width;
        result->opcode = forms[i].opcode;
        return true;
    }
    return false;
}

/* The faulting ld/st opcode names the *user memory width*, while the GNU
 * output/input register may use a wider C carrier (e.g. an unsigned long
 * for ld.b). The compiler writeback must use that carrier's actual width.
 * Do not infer this from the target GPR or blindly st.d every =r output. */



/* Linux __get_user/__put_user deliberately supplies its GNU "m" operand as
 * (*(struct __large_struct *)ptr).  This is an address/dependency operand:
 * its artificial 100-long record must not be mistaken for the hardware
 * ld/st width.  Keep exact scalar-width checking for genuine scalar memory
 * operands, while admitting only the named Linux sentinel record. */



/* Core intentionally drops C record spellings. Source admission above is
 * the only authority for the GNU __large_struct name. The machine must
 * independently validate its ABI shape (one 100-element unsigned long
 * array, 800 bytes) and transport the pointer to the memory operand from
 * Core, not reopen the frontend Program's record-name table. */
static bool loong_uaccess_core_memory_width_matches(
    const MinicCoreFunction *fn, MinicCoreValueId address,
    unsigned int width) {
    const MinicCoreTypeContext *types;
    const MinicCoreType *pointer, *memory, *array, *element;
    MinicCoreTypeId id;
    if (!fn || !fn->type_context || address >= fn->value_count)
        return false;
    types = fn->type_context;
    id = minic_core_type_unqualified(types, fn->values[address].core_type_id);
    pointer = minic_core_type_get(types, id);
    if (!pointer || pointer->kind != MINIC_CORE_TYPE_POINTER) return false;
    id = minic_core_type_unqualified(types, pointer->value.pointer.pointee);
    memory = minic_core_type_get(types, id);
    if (!memory) return false;
    if (memory->kind == MINIC_CORE_TYPE_RECORD) {
        if (!memory->has_layout || !memory->value.record.is_complete ||
            memory->size != 800U || memory->value.record.field_count != 1U ||
            memory->value.record.fields == NULL ||
            memory->value.record.fields[0].byte_offset != 0U ||
            memory->value.record.fields[0].is_bit_field) return false;
        id = minic_core_type_unqualified(
            types, memory->value.record.fields[0].type_id);
        array = minic_core_type_get(types, id);
        if (!array || array->kind != MINIC_CORE_TYPE_ARRAY ||
            !array->value.array.is_complete ||
            array->value.array.count != 100U) return false;
        id = minic_core_type_unqualified(types, array->value.array.element);
        element = minic_core_type_get(types, id);
        return element && element->kind == MINIC_CORE_TYPE_INTEGER &&
            element->value.integer.bit_width == 64U &&
            !element->value.integer.is_signed && element->size == 8U;
    }
    if (memory->kind == MINIC_CORE_TYPE_POINTER) return width == 8U;
    return memory->kind == MINIC_CORE_TYPE_INTEGER &&
        memory->value.integer.bit_width == width * 8U &&
        (width == 1U || width == 2U || width == 4U || width == 8U);
}




static bool loong_linux_uaccess_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info,
    const LoongLinuxUaccess *form) {
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const MinicCoreType *data_type;
    size_t i, data_width;

    if (!out || !ctx || !fn || !info || !form || !info->is_volatile ||
        info->is_goto || info->register_clobber_count != 0U ||
        ins->value.structured_inline_asm.operand_count != 3U ||
        !loong_linux_uaccess_form(info->template_text,
                                 info->template_length, &(LoongLinuxUaccess){0}))
        return false;
    for (i = 0U; i < 3U; ++i) {
        const MinicCoreStructuredInlineAsmOperandKind kind = i == 0U
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE
            : i == 1U
                ? (form->load ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
                              : MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT)
                : (form->load ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT
                              : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT);
        if (b[i].operand_index != i || b[i].kind != kind ||
            b[i].value >= fn->value_count || b[i].early_clobber ||
            b[i].has_fixed_register_binding) return false;
    }
    if (!loong_asm_integer(fn, b[0].value, true, 4U, 1)) return false;
    data_type = loong_asm_value_type(fn, b[form->load ? 1U : 2U].value,
                                     form->load);
    if (data_type == NULL || !data_type->has_layout ||
        (data_type->kind != MINIC_CORE_TYPE_POINTER &&
         (data_type->kind != MINIC_CORE_TYPE_INTEGER ||
          data_type->value.integer.is_bool))) return false;
    data_width = data_type->size;
    if ((data_type->kind == MINIC_CORE_TYPE_INTEGER &&
         data_type->value.integer.bit_width != data_width * 8U) ||
        (data_type->kind == MINIC_CORE_TYPE_POINTER && data_width != 8U))
        return false;
    if (
        !loong_uaccess_core_memory_width_matches(
            fn, b[form->load ? 2U : 1U].value, form->width) ||
        data_width < form->width ||
        (data_type->kind == MINIC_CORE_TYPE_POINTER && form->width != 8U))
        return false;

    /* $t0 (physical r12) is the error register and $t1 (r13) is the
     * faulting-load result. GNU's symbolic register-number .equ table is
     * replaced with those exact physical indexes only after we have
     * selected them. $t4 carries the user address; $t7 the C lvalue
     * address. The shared frame helper handles large and dynamic frames. */
    if (!minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t1", minic_lp64_frame_value_offset(ctx->frame_plan, b[0].value),
            ctx->dynamic_frame) ||
        !minic_loongarch64_emit_asm_input_scalar(
            out, fn, b[0].value, 4U) ||
        !minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t4", minic_lp64_frame_value_offset(ctx->frame_plan, b[form->load ? 2U : 1U].value),
            ctx->dynamic_frame)) return false;
    if (!form->load &&
        !minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t2", minic_lp64_frame_value_offset(ctx->frame_plan, b[2].value),
            ctx->dynamic_frame)) return false;
    if (fprintf(out,
            "1: %s %s, $t4, 0\n"
            "2:\n"
            "  .pushsection __ex_table, \"a\"\n"
            "  .balign 4\n"
            "  .long (1b - .)\n"
            "  .long (2b - .)\n"
            "  .short 2\n"
            "  .short ((12 << 0) | (%u << 5))\n"
            "  .popsection\n",
            form->opcode, form->load ? "$t1" : "$t2",
            form->load ? 13U : 0U) < 0 ||
        !minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t7", minic_lp64_frame_value_offset(ctx->frame_plan, b[0].value),
            ctx->dynamic_frame) ||
        !minic_loongarch64_emit_asm_output_store(
            out, fn, b[0].value,
            4U, "$t0", "$t7", "$t3")) return false;
    if (form->load &&
        (!minic_loongarch64_emit_frame_mem(
             out, "ld.d", "$t7", minic_lp64_frame_value_offset(ctx->frame_plan, b[1].value),
             ctx->dynamic_frame) ||
         !minic_loongarch64_emit_asm_output_store(
             out, fn, b[1].value,
             data_width, "$t1", "$t7", "$t3"))) return false;
    return true;
}

static bool loong_linux_futex_rmw_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info,
    LoongLinuxFutexRmw operation) {
    static const char *const opcodes[] = {
        NULL, "move", "add.w", "or", "and", "xor"
    };
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const MinicCoreInstruction *initial;
    size_t i;
    if (!out || !ctx || !fn || !ins || !info ||
        operation < LOONG_FUTEX_SET || operation > LOONG_FUTEX_XOR ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count != 1U || !info->register_clobbers ||
        info->register_clobbers[0].name_length != 2U ||
        memcmp(info->register_clobbers[0].name, "t0", 2U) != 0 ||
        ins->value.structured_inline_asm.operand_count != 6U ||
        loong_linux_futex_rmw_form(info->template_text, info->template_length) !=
            operation) return false;
    for (i = 0U; i < 6U; ++i) {
        const MinicCoreStructuredInlineAsmOperandKind expected = i <= 1U
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
            : i == 2U ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT
            : i == 4U ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT
            : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        if (b[i].value >= fn->value_count || b[i].operand_index != i ||
            b[i].kind != expected || b[i].has_fixed_register_binding ||
            b[i].early_clobber) return false;
    }
    for (i = 0U; i < 3U; ++i) {
        if (!loong_asm_integer(fn, b[i].value, true, 4U,
                               i == 2U ? 0 : 1)) return false;
    }
    if (!loong_asm_integer(fn, b[4].value, true, 4U, 0) ||
        !loong_asm_integer(fn, b[3].value, false, 4U, 1) ||
        !minic_core_function_value_is_kind(fn, b[5].value, MINIC_CORE_TYPE_INTEGER) ||
        fn->values[b[3].value].definition >= fn->instruction_count)
        return false;
    initial = &fn->instructions[fn->values[b[3].value].definition];
    /* GNU matching constraint "0" initializes the error register to zero;
     * it is NOT a free register allocation choice. Fault handler type 2
     * writes -EFAULT only to the physical error register in extable data. */
    if (initial->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT ||
        initial->value.integer_value != 0) return false;
    if (!minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t4", minic_lp64_frame_value_offset(ctx->frame_plan, b[4].value),
            ctx->dynamic_frame) ||
        !minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t5", minic_lp64_frame_value_offset(ctx->frame_plan, b[2].value),
            ctx->dynamic_frame) ||
        !minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t2", minic_lp64_frame_value_offset(ctx->frame_plan, b[5].value),
            ctx->dynamic_frame) ||
        /* Frame offset legalization may consume $t3 as a scratch. The
         * fault-status register must survive that helper after the loop. */
        fputs("  addi.w $t6, $zero, 0\n", out) == EOF ||
        fputs("1:\n  ll.w $t1, $t4, 0\n", out) == EOF)
        return false;
    /* SC writes a success flag into its source register; materialize the
     * desired new value afresh on every retry. ADD.W is 32-bit modulo
     * arithmetic, while logical ops use LoongArch's 64-bit GPR forms. */
    if (operation == LOONG_FUTEX_SET) {
        if (fputs("  move $t0, $t2\n", out) == EOF) return false;
    } else if (fprintf(out, "  %s $t0, $t1, $t2\n",
                       opcodes[(size_t)operation]) < 0) return false;
    if (fputs(
            "2:\n"
            "  sc.w $t0, $t5, 0\n"
            "  beqz $t0, 1b\n"
            "3:\n"
            "  .pushsection __ex_table, \"a\"\n"
            "  .balign 4\n"
            "  .long (1b - .)\n"
            "  .long (3b - .)\n"
            "  .short 2\n"
            "  .short ((18 << 0) | (0 << 5))\n"
            "  .long (2b - .)\n"
            "  .long (3b - .)\n"
            "  .short 2\n"
            "  .short ((18 << 0) | (0 << 5))\n"
            "  .popsection\n", out) == EOF ||
        !minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t7", minic_lp64_frame_value_offset(ctx->frame_plan, b[0].value),
            ctx->dynamic_frame) ||
        fputs("  st.w $t6, $t7, 0\n", out) == EOF ||
        !minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t7", minic_lp64_frame_value_offset(ctx->frame_plan, b[1].value),
            ctx->dynamic_frame) ||
        fputs("  st.w $t1, $t7, 0\n", out) == EOF) return false;
    return true;
}

static bool loong_linux_futex_cmpxchg_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    static const MinicCoreStructuredInlineAsmOperandKind kinds[] = {
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE,
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT
    };
    size_t i;
    if (!out || !ctx || !ins || !info || !info->is_volatile || info->is_goto ||
        !loong_linux_futex_cmpxchg_form(info->template_text, info->template_length) ||
        !info->has_memory_clobber || info->register_clobber_count != 1U ||
        !info->register_clobbers ||
        info->register_clobbers[0].name_length != 2U ||
        memcmp(info->register_clobbers[0].name, "t0", 2U) != 0 ||
        ins->value.structured_inline_asm.operand_count != 6U) return false;
    for (i = 0U; i < 6U; ++i) {
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].kind != kinds[i] || b[i].has_fixed_register_binding ||
            (i != 1U && b[i].early_clobber)) return false;
        if (!loong_asm_integer(fn, b[i].value, i < 4U, 4U,
                               i == 0U ? 1 : 0)) return false;
    }
    if (!minic_loongarch64_emit_frame_mem(out, "ld.d", "$t4",
            minic_lp64_frame_value_offset(ctx->frame_plan, b[3].value), ctx->dynamic_frame) ||
        !minic_loongarch64_emit_frame_mem(out, "ld.d", "$t5",
            minic_lp64_frame_value_offset(ctx->frame_plan, b[2].value), ctx->dynamic_frame) ||
        !minic_loongarch64_emit_frame_mem(out, "ld.wu", "$t2",
            minic_lp64_frame_value_offset(ctx->frame_plan, b[4].value), ctx->dynamic_frame) ||
        !minic_loongarch64_emit_frame_mem(out, "ld.wu", "$t8",
            minic_lp64_frame_value_offset(ctx->frame_plan, b[5].value), ctx->dynamic_frame) ||
        /* FIXME(MC-045): operand 0 is GNU +r and may enter nonzero; source
         * qualification does not prove an initial zero. Its old value is
         * currently dropped on the no-fault/no-store path. */
        fputs("  addi.w $t6, $zero, 0\n"
              "1: ll.w $t1, $t4, 0\n"
              "  bne $t1, $t2, 3f\n"
              "  move $t0, $t8\n"
              "2: sc.w $t0, $t5, 0\n"
              "  beqz $t0, 1b\n"
              "3: dbar 0x700\n"
              "  .pushsection __ex_table, \"a\"\n"
              "  .balign 4\n"
              "  .long (1b - .)\n"
              "  .long (3b - .)\n"
              "  .short 2\n"
              "  .short ((18 << 0) | (0 << 5))\n"
              "  .long (2b - .)\n"
              "  .long (3b - .)\n"
              "  .short 2\n"
              "  .short ((18 << 0) | (0 << 5))\n"
              "  .popsection\n", out) == EOF ||
        !minic_loongarch64_emit_frame_mem(out, "ld.d", "$t7",
            minic_lp64_frame_value_offset(ctx->frame_plan, b[0].value), ctx->dynamic_frame) ||
        fputs("  st.w $t6, $t7, 0\n", out) == EOF ||
        !minic_loongarch64_emit_frame_mem(out, "ld.d", "$t7",
            minic_lp64_frame_value_offset(ctx->frame_plan, b[1].value), ctx->dynamic_frame) ||
        fputs("  st.w $t1, $t7, 0\n", out) == EOF) return false;
    return true;
}

/* Linux 6.6 arch/loongarch/kernel/traps.c: the IRQ entry helper switches
 * to the per-CPU interrupt stack for one C call and returns to its original
 * frame afterwards. This is not a general assembly-text passthrough: the
 * exact five-instruction program, two named inputs and complete clobber
 * vector together form one privileged calling-convention contract.
 *
 * The original GCC clobbers $s0, so GCC saves/restores that callee-saved
 * register in the containing function. MiniC's ordinary fixed frame does
 * not reserve $s0: our lowering explicitly saves both $s0 and $ra around
 * the nested BL, independent of the ordinary C-call scanner. Also, no
 * stack-frame access is allowed between changing SP and restoring it. */
bool loong_linux_irq_switch_template(
    const char *text, size_t length, bool numeric) {
    static const char source_form[] =
        "move$s0,$spmove$sp,%[stk]move$a0,%[regs]"
        "blhandle_loongarch_irqmove$sp,$s0";
    static const char core_form[] =
        "move$s0,$spmove$sp,%0move$a0,%1"
        "blhandle_loongarch_irqmove$sp,$s0";
    const char *const expected = numeric ? core_form : source_form;
    char compact[256];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == '\n' || c == '\r' || c == '\t' || c == ' ')
            continue;
        if (c == '\0' || c == ';' || n + 1U >= sizeof(compact))
            return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    return strcmp(compact, expected) == 0;
}

bool loong_linux_irq_switch_clobber(
    const char *name, size_t length, size_t index) {
    char kind;
    unsigned int number;
    if (!name || length != 3U || name[0] != '$' || index >= 18U)
        return false;
    if (index < 8U) {
        kind = 'a';
        number = (unsigned int)index;
    } else if (index == 8U) {
        kind = 's';
        number = 0U;
    } else {
        kind = 't';
        number = (unsigned int)(index - 9U);
    }
    return name[1] == kind && name[2] == (char)('0' + number);
}




static bool loong_linux_irq_switch_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreFunction *fn;
    const MinicCoreStructuredInlineAsmOperand *operands;
    size_t i;
    if (!out || !ctx || !ctx->fn || !ins || !info) return false;
    fn = ctx->fn;
    operands = ins->value.structured_inline_asm.operands;
    if (!info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        ctx->dynamic_frame || !loong_linux_irq_switch_template(
            info->template_text, info->template_length, true) ||
        info->register_clobber_count != 18U || !info->register_clobbers ||
        ins->value.structured_inline_asm.operand_count != 2U || !operands)
        return false;
    for (i = 0U; i < 18U; ++i)
        if (!loong_linux_irq_switch_clobber(
                info->register_clobbers[i].name,
                info->register_clobbers[i].name_length, i))
            return false;
    for (i = 0U; i < 2U; ++i) {
        const MinicCoreStructuredInlineAsmOperand *op = &operands[i];
        if (op->operand_index != i ||
            op->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
            op->early_clobber || op->has_fixed_register_binding ||
            op->value >= fn->value_count ||
            ((i == 0U && !loong_asm_integer(fn, op->value, false, 8U, 0)) ||
             (i == 1U &&
              !minic_core_function_value_is_kind(fn, op->value, MINIC_CORE_TYPE_POINTER))))
            return false;
        if (!minic_loongarch64_emit_frame_mem(
                out, "ld.d", i == 0U ? "$t0" : "$t1",
                minic_lp64_frame_value_offset(ctx->frame_plan, op->value), false))
            return false;
    }
    /* Local 16-byte ABI-aligned save frame is anchored by $s0 across BL.
     * Caller-save argument/temp clobbers need no other preservation; all
     * Core virtual values live in frame slots, not scratch registers. */
    return fputs(
        "  addi.d $sp, $sp, -16\n"
        "  st.d $s0, $sp, 0\n"
        "  st.d $ra, $sp, 8\n"
        "  move $s0, $sp\n"
        "  move $sp, $t0\n"
        "  move $a0, $t1\n"
        "  bl handle_loongarch_irq\n"
        "  move $sp, $s0\n"
        "  ld.d $ra, $sp, 8\n"
        "  ld.d $s0, $sp, 0\n"
        "  addi.d $sp, $sp, 16\n", out) != EOF;
}

/* Linux prepare_frametrace() dumps the physical register bank into the
 * incoming pt_regs, with an unusual $ra/CSR_ERA PC capture in the middle.
 * It MUST stay a target machine effect, including the temporary $ra write
 * and restore. The template recognizer reconstructs all 30 register stores
 * and their exact offsets, so a dropped register, different stride or
 * inserted executable opcode cannot be silently compiled. */
bool loong_linux_frametrace_template(
    const char *text, size_t length) {
    char expected[2048];
    char normalized[2048];
    size_t n = 0U, i, j;
    int count;
    if (!text || !length || length >= sizeof(normalized)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            continue;
        if (c == '\0' || c == ';' || n + 1U >= sizeof(normalized))
            return false;
        normalized[n++] = c;
    }
    normalized[n] = '\0';
    count = snprintf(expected, sizeof(expected),
        "st.d$r1,%%1,8*1pcaddi$ra,0st.d$ra,%%0ld.d$ra,%%1,8");
    if (count < 0 || (size_t)count >= sizeof(expected)) return false;
    j = (size_t)count;
    for (i = 2U; i <= 31U; ++i) {
        count = snprintf(expected + j, sizeof(expected) - j,
                         "st.d$r%zu,%%1,8*%zu", i, i);
        if (count < 0 || (size_t)count >= sizeof(expected) - j)
            return false;
        j += (size_t)count;
    }
    return strcmp(normalized, expected) == 0;
}




static bool loong_linux_frametrace_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreFunction *fn;
    const MinicCoreStructuredInlineAsmOperand *b;
    size_t i;
    if (!out || !ctx || !ctx->fn || !ins || !info) return false;
    fn = ctx->fn;
    b = ins->value.structured_inline_asm.operands;
    if (!info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count ||
        !loong_linux_frametrace_template(
            info->template_text, info->template_length) ||
        ins->value.structured_inline_asm.operand_count != 2U || !b ||
        b[0].operand_index != 0U ||
        b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT ||
        b[1].operand_index != 1U ||
        b[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT)
        return false;
    for (i = 0U; i < 2U; ++i)
        if (b[i].early_clobber || b[i].has_fixed_register_binding ||
            b[i].value >= fn->value_count)
            return false;
    if (!loong_asm_integer(fn, b[0].value, true, 8U, 0) ||
        !minic_core_function_value_is_kind(fn, b[1].value, MINIC_CORE_TYPE_POINTER) ||
        !minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t0", minic_lp64_frame_value_offset(ctx->frame_plan, b[1].value),
            ctx->dynamic_frame) ||
        !minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t1", minic_lp64_frame_value_offset(ctx->frame_plan, b[0].value),
            ctx->dynamic_frame) ||
        fputs("  st.d $r1, $t0, 8\n"
              "  pcaddi $ra, 0\n"
              "  st.d $ra, $t1, 0\n"
              "  ld.d $ra, $t0, 8\n", out) == EOF)
        return false;
    for (i = 2U; i <= 31U; ++i)
        if (fprintf(out, "  st.d $r%zu, $t0, %zu\n", i, i * 8U) < 0)
            return false;
    return true;
}

/* prepare_frametrace's six direct CSR snapshots use literal register
 * indices, not the irqflags CSR_CRMD named GNU immediate-zero template.
 * Read the real privileged CSR before storing each corresponding pt_regs
 * member; preserve the source's exact set of indices and output width. */
unsigned int loong_linux_frametrace_csr_form(
    const char *text, size_t length) {
    static const struct { const char *text; unsigned int csr; } forms[] = {
        {"csrrd%0,0x7", 7U},   /* CSR_BADV */
        {"csrrd%0,0x0", 0U},   /* CSR_CRMD */
        {"csrrd%0,0x1", 1U},   /* CSR_PRMD */
        {"csrrd%0,0x2", 2U},   /* CSR_EUEN */
        {"csrrd%0,0x4", 4U},   /* CSR_ECFG */
        {"csrrd%0,0x5", 5U}    /* CSR_ESTAT */
    };
    char compact[64];
    size_t i, n = 0U;
    if (!text || !length || length >= sizeof(compact)) return UINT32_MAX;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (c == '\0' || c == ';' || n + 1U >= sizeof(compact))
            return UINT32_MAX;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms)/sizeof(forms[0]); ++i)
        if (strcmp(compact, forms[i].text) == 0) return forms[i].csr;
    return UINT32_MAX;
}




static bool loong_linux_frametrace_csr_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreFunction *fn;
    const MinicCoreStructuredInlineAsmOperand *b;
    const MinicCoreType *pointee;
    const unsigned int csr = loong_linux_frametrace_csr_form(
        info->template_text, info->template_length);
    if (!out || !ctx || !ctx->fn || !ins || !info || csr == UINT32_MAX)
        return false;
    fn = ctx->fn;
    b = ins->value.structured_inline_asm.operands;
    if (!info->is_volatile || info->is_goto || info->has_memory_clobber ||
        info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count != 1U || !b ||
        b[0].operand_index != 0U ||
        b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
        b[0].early_clobber || b[0].has_fixed_register_binding ||
        b[0].value >= fn->value_count ||
        !(pointee = loong_asm_value_type(fn, b[0].value, true)) ||
        !(loong_asm_integer(fn, b[0].value, true, 8U, 0) ||
          loong_asm_integer(fn, b[0].value, true, 4U, 0)))
        return false;
    return fprintf(out, "  csrrd $t0, %u\n", csr) >= 0 &&
        minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$t1", minic_lp64_frame_value_offset(ctx->frame_plan, b[0].value),
            ctx->dynamic_frame) &&
        minic_loongarch64_emit_asm_output_store(
            out, fn, b[0].value,
            pointee->size,
            "$t0", "$t1", "$t3");
}

/* The exact-template Linux forms whose source and Core contracts share one
 * target-local descriptor table are defined below, after their machine
 * emitters. Keep a forward declaration so source admission consumes that
 * very same registry rather than reimplementing its selection order. */








static bool minic_loongarch64_emit_amo(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const struct MinicLoongAtomicTemplate *atom;
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    if (ins->value.structured_inline_asm.operand_count != 2U ||
        !info->is_volatile || info->is_goto ||
        info->register_clobber_count != 0U ||
        !info->has_memory_clobber)
        return false;
    atom = loong_atomic_template(info->template_text, info->template_length, false);
    if (atom == NULL || b[0].operand_index != 0U ||
        b[1].operand_index != 1U ||
        b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE ||
        b[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        b[0].early_clobber || b[1].early_clobber ||
        b[0].has_fixed_register_binding || b[1].has_fixed_register_binding ||
        b[0].value >= fn->value_count || b[1].value >= fn->value_count ||
        !loong_asm_integer(fn, b[0].value, true, atom->width, 1) ||
        !loong_asm_integer(fn, b[1].value, false, atom->width, 1))
        return false;
    /* Existing LP64D machine frame transport and scratch policy; the
     * LoongArch atomic *DB instruction performs both the RMW and hardware
     * memory ordering. Never lower +ZB to C load/compute/store. */
    return minic_loongarch64_emit_frame_mem(
               out, "ld.d", "$t1", minic_lp64_frame_value_offset(ctx->frame_plan, b[0].value), ctx->dynamic_frame) &&
           minic_loongarch64_emit_frame_mem(
               out, atom->width == 8U ? "ld.d" : "ld.w",
               "$t0", minic_lp64_frame_value_offset(ctx->frame_plan, b[1].value), ctx->dynamic_frame) &&
           fprintf(out, "  %s $zero, $t0, $t1\n",
                   atom->instruction) >= 0;
}

static bool minic_loongarch64_emit_csr(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const unsigned int form = loong_csr_template(
        info->template_text, info->template_length);
    size_t width = 0U;
    const MinicCoreType *pointee;
    const MinicCoreInstruction *imm;
    MinicCoreInstructionId def;
    size_t imm_index, i;
    if (!form || !info->is_volatile || info->is_goto ||
        !info->has_memory_clobber || info->register_clobber_count ||
        ins->value.structured_inline_asm.operand_count !=
            (form == 1U ? 2U : 3U)) return false;
    for (i = 0U; i < ins->value.structured_inline_asm.operand_count; ++i) {
        /* Linux arch_local_irq_save binds its unsigned mask as GNU local
         * `register u32 mask asm("t0")`. The actual instruction below loads
         * that operand into PHYSICAL $t0 before CSRXCHG. Core now preserves
         * the local hardreg name instead of discarding it. Only this exact
         * matching binding is safe; a different fixed register cannot be
         * silently reallocated to $t0. All other CSR operands must remain
         * ordinary spill-backed values or addresses. */
        if (b[i].has_fixed_register_binding) {
            const MinicCoreFixedRegisterBinding *fixed;
            if (form != 2U || i != 1U ||
                b[i].fixed_register_binding_id >= fn->fixed_register_binding_count)
                return false;
            fixed = &fn->fixed_register_bindings[b[i].fixed_register_binding_id];
            if (!fixed->is_local || !fixed->register_name ||
                fixed->register_name_length != 2U ||
                memcmp(fixed->register_name, "t0", 2U) != 0 ||
                !loong_asm_integer(fn, b[i].value, false, 4U, 0))
                return false;
        }
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].early_clobber ||
            b[i].value > MINIC_LARGE_FRAME_MAX / 8U)
            return false;
    }
    if (b[0].kind != (form == 1U ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE) ||
        !(pointee = loong_asm_value_type(fn, b[0].value, true)) ||
        !(loong_asm_integer(fn, b[0].value, true, 4U, 0) ||
          loong_asm_integer(fn, b[0].value, true, 8U, 0)) ||
        (form == 1U && pointee->size != 4U)) return false;
    width = pointee->size;
    imm_index = form == 1U ? 1U : 2U;
    if (b[imm_index].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT)
        return false;
    def = fn->values[b[imm_index].value].definition;
    if (def >= fn->instruction_count) return false;
    imm = &fn->instructions[def];
    if (imm->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT ||
        imm->result != b[imm_index].value || imm->value.integer_value != 0)
        return false;
    if (form == 2U && (b[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        !loong_asm_integer(fn, b[1].value, false, 4U, 0))) return false;
    /* $t1 carries saved/restored CRMD. Gather the +r C lvalue before
     * loading the mask into $t0; the byte fallback owns $t2-$t5 scratch.
     * Then store through the same typed output transport as Core STORE.
     * User-mode QEMU cannot execute privileged CSR operations. */
    if (form == 2U &&
        (!minic_loongarch64_emit_frame_mem(
             out, "ld.d", "$t1", minic_lp64_frame_value_offset(ctx->frame_plan, b[0].value),
             ctx->dynamic_frame) ||
         !minic_loongarch64_emit_asm_input_scalar(
             out, fn, b[0].value, width) ||
         fputs("  move $t1, $t0\n", out) == EOF ||
         !minic_loongarch64_emit_frame_mem(
             out, "ld.w", "$t0", minic_lp64_frame_value_offset(ctx->frame_plan, b[1].value),
             ctx->dynamic_frame) ||
         fputs("  csrxchg $t1, $t0, 0\n", out) == EOF))
        return false;
    if (form == 1U && fputs("  csrrd $t1, 0\n", out) == EOF)
        return false;
    return minic_loongarch64_emit_frame_mem(
               out, "ld.d", "$t2", minic_lp64_frame_value_offset(ctx->frame_plan, b[0].value),
               ctx->dynamic_frame) &&
           minic_loongarch64_emit_asm_output_store(
               out, fn, b[0].value,
               width, "$t1", "$t2", "$t3");
}
/* Result register of LoongArch AMO is the OLD pre-update memory value.
 * No CAS emulation, ordinary Core load/store, or guessed ISA permission. */
static bool minic_loongarch64_emit_amo_fetch(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const struct MinicLoongAtomicTemplate *atom;
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    if (ins->value.structured_inline_asm.operand_count != 3U ||
        !info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count != 0U) return false;
    atom = loong_atomic_template(info->template_text, info->template_length, true);
    if (!atom) return false;
    if (b[0].operand_index != 0U || b[1].operand_index != 1U ||
        b[2].operand_index != 2U ||
        b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE ||
        b[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
        b[2].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        b[0].early_clobber || b[1].early_clobber || b[2].early_clobber ||
        b[0].has_fixed_register_binding || b[1].has_fixed_register_binding ||
        b[2].has_fixed_register_binding ||
        b[0].value >= fn->value_count || b[1].value >= fn->value_count ||
        b[2].value >= fn->value_count ||
        !loong_asm_integer(fn, b[0].value, true, atom->width, 1) ||
        !loong_asm_integer(fn, b[1].value, true, atom->width, 1) ||
        !loong_asm_integer(fn, b[2].value, false, atom->width, 1))
        return false;
    /* Target frame access uses $t3 scratch for large offsets. $t2 remains
     * stable across later frame accesses and holds hardware AMO old-value. */
    return minic_loongarch64_emit_frame_mem(
               out, "ld.d", "$t1", minic_lp64_frame_value_offset(ctx->frame_plan, b[0].value),
               ctx->dynamic_frame) &&
           minic_loongarch64_emit_frame_mem(
               out, atom->width == 8U ? "ld.d" : "ld.w", "$t0",
               minic_lp64_frame_value_offset(ctx->frame_plan, b[2].value), ctx->dynamic_frame) &&
           fprintf(out, "  %s $t2, $t0, $t1\n", atom->instruction) >= 0 &&
           minic_loongarch64_emit_frame_mem(
               out, "ld.d", "$t1", minic_lp64_frame_value_offset(ctx->frame_plan, b[1].value),
               ctx->dynamic_frame) &&
           minic_loongarch64_emit_asm_output_store(
               out, fn, b[1].value,
               atom->width, "$t2", "$t1", "$t3");
}


/* Full original Linux LL/SC retry sequence and mandatory dbar 0x700.
 * Frame helper uses $t3 for large offsets; load all SSA operands BEFORE
 * loop. $t4 is the real LL/SC scratch and SC success result. */
static bool loong_pte_emit(
    FILE *out,const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins,const MinicCoreInlineAsm *info) {
    const MinicCoreFunction *fn=ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *b=
        ins->value.structured_inline_asm.operands;
    size_t i;
    if(!out||!ctx||!fn||!b||!info||!info->is_volatile||info->is_goto||
       !info->has_memory_clobber||info->register_clobber_count||
       !loong_pte_form(info->template_text,info->template_length)||
       ins->value.structured_inline_asm.operand_count!=3U)
        return false;
    for(i=0U;i<3U;++i){
        if(b[i].operand_index!=i||b[i].value>=fn->value_count||
           b[i].has_fixed_register_binding||b[i].early_clobber||
           b[i].kind!=(i==0U?
             MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
             i==1U?MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
                   MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
            return false;
        if(!loong_asm_integer(fn,b[i].value,i<2U,8U,0))return false;
    }
    return minic_loongarch64_emit_frame_mem(
             out,"ld.d","$t2",minic_lp64_frame_value_offset(ctx->frame_plan, b[1].value),ctx->dynamic_frame)&&
           minic_loongarch64_emit_frame_mem(
             out,"ld.d","$t0",minic_lp64_frame_value_offset(ctx->frame_plan, b[2].value),ctx->dynamic_frame)&&
           minic_loongarch64_emit_frame_mem(
             out,"ld.d","$t1",minic_lp64_frame_value_offset(ctx->frame_plan, b[0].value),ctx->dynamic_frame)&&
           fputs("1:\n"
                 "  ll.d $t4, $t1, 0\n"
                 "  bnez $t4, 2f\n"
                 "  or $t4, $t4, $t0\n"
                 "  sc.d $t4, $t1, 0\n"
                 "  beqz $t4, 1b\n"
                 "  nop\n"
                 "2:\n"
                 "  dbar 0x700\n",out)!=EOF&&
           minic_loongarch64_emit_asm_output_store(
             out,fn,b[1].value,
             8U,"$t4","$t2","$t3");
}

/* The exact Linux LL/SC compare-and-swap register choreography.
 * Frame helper may clobber $t3; keep live physical state in $t0-$t2,
 * $t4-$t8. Return both early-clobbered register outputs and the
 * memory output without converting volatile reads into scalar C stores. */
static bool loong_linux_cmpxchg_emit(FILE *out,
    const MinicDirectLP64BlockContext *ctx,const MinicCoreInstruction *ins,
    const MinicCoreInlineAsm *info) {
    const MinicCoreFunction *fn=ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *b=
        ins->value.structured_inline_asm.operands;
    const unsigned form=loong_linux_cmpxchg_form(
        info->template_text,info->template_length);
    const size_t n=form==3U?7U:5U,width=form==2U?8U:4U;
    const char *load=width==8U?"ld.d":"ld.w";
    const char *store=width==8U?"st.d":"st.w";
    size_t k;
    if(!out||!ctx||!fn||!info||!form||!info->is_volatile||
       info->is_goto||!info->has_memory_clobber||
       info->register_clobber_count!=(form==3U?0U:1U)||
       ins->value.structured_inline_asm.operand_count!=n)return false;
    for(k=0U;k<n;++k){
        const bool mem=form==3U?(k==2U||k==3U):(k==1U||k==2U);
        const bool output=form==3U?k<3U:k<2U;
        if(b[k].operand_index!=k||b[k].value>=fn->value_count||
           b[k].has_fixed_register_binding||
           b[k].kind!=(mem?(output?
                 MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:
                 MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT):
                 output?MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
                        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
            return false;
        if(!loong_asm_integer(fn,b[k].value,output||mem,width,-1)&&
           !(form==1U&&k==4U&&
             loong_asm_integer(fn,b[k].value,false,8U,-1)))return false;
    }
    if(form==3U){
        /* out0 old32, out1 temp, out2 destination; input3 source,
         * input4 mask, input5 expected and input6 replacement. */
        if(!minic_loongarch64_emit_frame_mem(out,"ld.d","$t4",
              minic_lp64_frame_value_offset(ctx->frame_plan, b[3].value),ctx->dynamic_frame)||
           !minic_loongarch64_emit_frame_mem(out,"ld.d","$t7",
              minic_lp64_frame_value_offset(ctx->frame_plan, b[2].value),ctx->dynamic_frame)||
           !minic_loongarch64_emit_frame_mem(out,load,"$t0",
              minic_lp64_frame_value_offset(ctx->frame_plan, b[4].value),ctx->dynamic_frame)||
           !minic_loongarch64_emit_frame_mem(out,load,"$t1",
              minic_lp64_frame_value_offset(ctx->frame_plan, b[5].value),ctx->dynamic_frame)||
           !minic_loongarch64_emit_frame_mem(out,load,"$t2",
              minic_lp64_frame_value_offset(ctx->frame_plan, b[6].value),ctx->dynamic_frame)||
           fputs("1:\n  ll.w $t5, $t4, 0\n"
                 "  and $t6, $t5, $t0\n"
                 "  bne $t6, $t1, 2f\n"
                 "  andn $t6, $t5, $t0\n"
                 "  or $t6, $t6, $t2\n"
                 "  sc.w $t6, $t7, 0\n"
                 "  beqz $t6, 1b\n"
                 "  b 3f\n"
                 "2:\n  dbar 0x700\n3:\n",out)==EOF)return false;
        for(k=0U;k<2U;++k)
            if(!minic_loongarch64_emit_frame_mem(out,"ld.d","$t7",
                  minic_lp64_frame_value_offset(ctx->frame_plan, b[k].value),ctx->dynamic_frame)||
               fprintf(out,"  st.w %s, $t7, 0\n",
                       k==0U?"$t5":"$t6")<0)return false;
        return true;
    }
    if(!minic_loongarch64_emit_frame_mem(out,"ld.d","$t4",
          minic_lp64_frame_value_offset(ctx->frame_plan, b[2].value),ctx->dynamic_frame)||
       !minic_loongarch64_emit_frame_mem(out,"ld.d","$t7",
          minic_lp64_frame_value_offset(ctx->frame_plan, b[1].value),ctx->dynamic_frame)||
       !minic_loongarch64_emit_frame_mem(out,load,"$t1",
          minic_lp64_frame_value_offset(ctx->frame_plan, b[3].value),ctx->dynamic_frame)||
       !minic_loongarch64_emit_frame_mem(out,load,"$t2",
          minic_lp64_frame_value_offset(ctx->frame_plan, b[4].value),ctx->dynamic_frame)||
       fprintf(out,"1:\n  ll.%c $t5, $t4, 0\n"
                   "  bne $t5, $t1, 2f\n"
                   "  or $t0, $t2, $zero\n"
                   "  sc.%c $t0, $t7, 0\n"
                   "  beqz $t0, 1b\n"
                   "2:\n  dbar 0x700\n",
                   width==8U?'d':'w',width==8U?'d':'w')<0||
       !minic_loongarch64_emit_frame_mem(out,"ld.d","$t7",
          minic_lp64_frame_value_offset(ctx->frame_plan, b[0].value),ctx->dynamic_frame)||
       fprintf(out,"  %s $t5, $t7, 0\n",store)<0)return false;
    return true;
}


/* Emit original hardware AMDB: old is observable, not optimized away;
 * the instruction itself enforces GNU Linux DB memory ordering. */
static bool loong_linux_xchg_emit(FILE *out,
    const MinicDirectLP64BlockContext *ctx,const MinicCoreInstruction *ins,
    const MinicCoreInlineAsm *info){
    const unsigned form=loong_linux_xchg_form(
        info->template_text,info->template_length);
    const size_t width=form&&form%2U==0U?8U:4U;
    const bool named=form>=3U&&form<=6U;
    const MinicCoreFunction *fn=ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *b=
        ins->value.structured_inline_asm.operands;
    size_t i;
    if(!out||!ctx||!fn||!info||!form||!info->is_volatile||
       info->is_goto||!info->has_memory_clobber||
       info->register_clobber_count||
       ins->value.structured_inline_asm.operand_count!=3U)return false;
    for(i=0U;i<3U;++i){
        if(b[i].operand_index!=i||b[i].value>=fn->value_count||
           b[i].has_fixed_register_binding||
           b[i].kind!=(i==0U?
               MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
               i==1U?MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
                     MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT))
            return false;
        if(!loong_asm_integer(fn,b[i].value,i<2U,
                              i==1U&&named?8U:width,-1))return false;
    }
    return minic_loongarch64_emit_frame_mem(
             out,"ld.d","$t1",minic_lp64_frame_value_offset(ctx->frame_plan, b[0].value),ctx->dynamic_frame)&&
           minic_loongarch64_emit_frame_mem(
             out,width==8U?"ld.d":"ld.w","$t0",
             minic_lp64_frame_value_offset(ctx->frame_plan, b[2].value),ctx->dynamic_frame)&&
           fprintf(out,"  %s.%c $t2, $t0, $t1\n",
                   form>=15U?"amxor":form>=13U?"amor":
                   form>=11U?"amand":form>=9U?"amadd":
                   form>=7U?"amswap":"amswap_db",
                   width==8U?'d':'w')>=0&&
           minic_loongarch64_emit_frame_mem(
             out,"ld.d","$t1",minic_lp64_frame_value_offset(ctx->frame_plan, b[1].value),ctx->dynamic_frame)&&
           minic_loongarch64_emit_asm_output_store(
             out,fn,b[1].value,
             width==8U||named?8U:4U,"$t2","$t1","$t3");
}

/* Keep both original GNU memory operands distinct in Core. The LL reads
 * input[3], while SC commits through output[2]; no untyped replacement
 * and no unsynchronized C load/store is permitted. */
static bool loong_linux_xchg_small_emit(FILE *out,
    const MinicDirectLP64BlockContext *ctx,const MinicCoreInstruction *ins,
    const MinicCoreInlineAsm *info){
    const MinicCoreFunction *fn=ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *b=
        ins->value.structured_inline_asm.operands;
    size_t i;
    if(!out||!ctx||!fn||!info||!loong_linux_xchg_small_form(
           info->template_text,info->template_length)||!info->is_volatile||
       info->is_goto||!info->has_memory_clobber||
       info->register_clobber_count||
       ins->value.structured_inline_asm.operand_count!=6U)return false;
    for(i=0U;i<6U;++i){
        const bool output=i<3U;
        const bool memory=i==2U||i==3U;
        if(b[i].operand_index!=i||b[i].value>=fn->value_count||
           b[i].has_fixed_register_binding||
           b[i].kind!=(memory?
               (output?MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:
                       MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT):
               (output?MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
                       MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT)))
            return false;
        if(!loong_asm_integer(fn,b[i].value,output||memory,4U,-1))
            return false;
    }
    if(!minic_loongarch64_emit_frame_mem(out,"ld.d","$t4",
          minic_lp64_frame_value_offset(ctx->frame_plan, b[3].value),ctx->dynamic_frame)||
       !minic_loongarch64_emit_frame_mem(out,"ld.d","$t7",
          minic_lp64_frame_value_offset(ctx->frame_plan, b[2].value),ctx->dynamic_frame)||
       !minic_loongarch64_emit_frame_mem(out,"ld.w","$t0",
          minic_lp64_frame_value_offset(ctx->frame_plan, b[4].value),ctx->dynamic_frame)||
       !minic_loongarch64_emit_frame_mem(out,"ld.w","$t1",
          minic_lp64_frame_value_offset(ctx->frame_plan, b[5].value),ctx->dynamic_frame)||
       fputs("1:\n"
             "  ll.w $t5, $t4, 0\n"
             "  andn $t6, $t5, $t0\n"
             "  or $t6, $t6, $t1\n"
             "  sc.w $t6, $t7, 0\n"
             "  beqz $t6, 1b\n",out)==EOF)return false;
    for(i=0U;i<2U;++i){
        if(!minic_loongarch64_emit_frame_mem(out,"ld.d","$t7",
              minic_lp64_frame_value_offset(ctx->frame_plan, b[i].value),ctx->dynamic_frame)||
           fprintf(out,"  st.w %s, $t7, 0\n",
                   i==0U?"$t5":"$t6")<0)return false;
    }
    return true;
}

static bool loong_linux_cacop_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *info) {
    const MinicCoreFunction *fn = ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    const MinicCoreInstruction *def;
    MinicCoreInstructionId definition;
    int64_t code;
    if (!info->is_volatile || info->is_goto || !info->has_memory_clobber ||
        info->register_clobber_count != 0U ||
        ins->value.structured_inline_asm.operand_count != 2U ||
        b[0].operand_index != 0U || b[1].operand_index != 1U ||
        b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        b[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT ||
        b[0].early_clobber || b[1].early_clobber ||
        b[0].has_fixed_register_binding || b[1].has_fixed_register_binding ||
        b[0].value >= fn->value_count || b[1].value >= fn->value_count ||
        !minic_core_function_value_is_kind(fn, b[0].value, MINIC_CORE_TYPE_INTEGER) ||
        !loong_asm_integer(fn, b[1].value, true, 1U, 0))
        return false;
    definition = fn->values[b[0].value].definition;
    if (definition >= fn->instruction_count) return false;
    def = &fn->instructions[definition];
    if (def->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT ||
        def->result != b[0].value)
        return false;
    code = def->value.integer_value;
    /* The frozen Linux flush_cache_line selects six D-cache operations:
     * (leaf | 0x08), leaf in 0..5. Never allow dynamic opcodes or a generic
     * arbitrary-cache-operation assembler escape. */
    if (code < 8 || code > 13) return false;
    return minic_loongarch64_emit_frame_mem(
               out, "ld.d", "$t0", minic_lp64_frame_value_offset(ctx->frame_plan, b[1].value),
               ctx->dynamic_frame) &&
           fprintf(out, "  cacop %lld, $t0, 0\n", (long long)code) >= 0;
}

/* Exact Linux instruction-family admission must have the same precedence
 * and owner as machine emission. These descriptors group only forms with
 * independent template matching and uniform typed Core emitter signatures.
 * Forms carrying a parsed instruction payload (clock, AMO, futex RMW, etc.)
 * remain in the explicitly typed paths below until that payload is stored
 * by a persistent machine-ASM plan. */
typedef bool (*LoongLinuxTemplateMatch)(const char *, size_t);
typedef bool (*LoongLinuxStructuredEmit)(
    FILE *, const MinicDirectLP64BlockContext *,
    const MinicCoreInstruction *, const MinicCoreInlineAsm *);

typedef struct LoongLinuxStructuredForm {
    LoongLinuxTemplateMatch matches;
    LoongLinuxStructuredEmit emit;
} LoongLinuxStructuredForm;

static bool loong_match_frametrace_csr(const char *text, size_t length) {
    return loong_linux_frametrace_csr_form(text, length) != UINT32_MAX;
}

static bool loong_match_fpr_transfer(const char *text, size_t length) {
    return loong_linux_fpr_transfer_form(text, length) != 0U;
}

static bool loong_match_percpu_indexed(const char *text, size_t length) {
    return loong_percpu_indexed_form(text, length) != LOONG_PERCPU_NONE;
}

static bool loong_match_cmpxchg(const char *text, size_t length) {
    return loong_linux_cmpxchg_form(text, length) != 0U;
}

static bool loong_match_xchg(const char *text, size_t length) {
    return loong_linux_xchg_form(text, length) != 0U;
}

/* One registry couples source type qualification to exact-template machine
 * ownership. Crucially, the emitter never interprets unknown text as one of
 * these families merely because its operand count resembles a known form. */
static const LoongLinuxStructuredForm loong_linux_structured_forms[] = {
    {loong_linux_cacop_form, loong_linux_cacop_emit},
    {loong_linux_vdso_syscall_form, loong_linux_vdso_emit},
    {loong_linux_frametrace_template, loong_linux_frametrace_emit},
    {loong_match_frametrace_csr, loong_linux_frametrace_csr_emit},
    {loong_linux_cpucfg_form, loong_linux_cpucfg_emit},
    {loong_match_fpr_transfer, loong_linux_fpr_transfer_emit},
    {loong_linux_futex_cmpxchg_form, loong_linux_futex_cmpxchg_emit},
    {loong_fcsr0_write_form, loong_fcsr0_write_emit},
    {loong_match_percpu_indexed, loong_percpu_indexed_emit},
    {loong_match_cmpxchg, loong_linux_cmpxchg_emit},
    {loong_match_xchg, loong_linux_xchg_emit},
    {loong_linux_xchg_small_form, loong_linux_xchg_small_emit},
    {loong_mask_nospec_form, loong_mask_nospec_emit},
    {loong_pte_form, loong_pte_emit}
};
_Static_assert(sizeof(loong_linux_structured_forms) /
                   sizeof(loong_linux_structured_forms[0]) ==
                   MINIC_LOONG_MACHINE_ASM_CLOCK - 1U,
               "Loong typed scalar IDs must not overlap descriptor ordinals");

static const LoongLinuxStructuredForm *loong_linux_form_for_template(
    const char *text, size_t length) {
    size_t i;
    if (text == NULL) return NULL;
    for (i = 0U; i < sizeof(loong_linux_structured_forms) /
                         sizeof(loong_linux_structured_forms[0]); ++i)
        if (loong_linux_structured_forms[i].matches(text, length))
            return &loong_linux_structured_forms[i];
    return NULL;
}




bool minic_loongarch64_descriptor_template_requires_typed_id(
    const char *text, size_t length) {
    return loong_linux_form_for_template(text, length) != NULL;
}




bool minic_loongarch64_typed_descriptor_matches_template(
    uint32_t id, uint32_t variant, const char *text, size_t length) {
    size_t index;
    if (id == 0U || variant != 0U || text == NULL)
        return false;
    index = (size_t)(id - 1U);
    return index < sizeof(loong_linux_structured_forms) /
                       sizeof(loong_linux_structured_forms[0]) &&
           loong_linux_structured_forms[index].matches(text, length);
}

bool minic_loongarch64_typed_scalar_form_matches_template(
    uint32_t id, uint32_t variant, const char *text, size_t length) {
    LoongLinuxSubPositive sub;
    LoongLinuxUaccess uaccess;
    const struct MinicLoongAtomicTemplate *atom;
    const char *literal;
    size_t percpu;
    if (text == NULL || variant == 0U) return false;
    switch ((MinicLoongLinuxMachineAsmForm)id) {
    case MINIC_LOONG_MACHINE_ASM_CLOCK:
        return variant == (uint32_t)loong_linux_clock_form(text, length);
    case MINIC_LOONG_MACHINE_ASM_IRQ_SWITCH:
        return variant == 1U && loong_linux_irq_switch_template(
            text, length, true);
    case MINIC_LOONG_MACHINE_ASM_INVTLB:
        return variant == loong_linux_invtlb_form(text, length);
    case MINIC_LOONG_MACHINE_ASM_ADD_UNLESS:
        return variant == loong_linux_add_unless_form(text, length);
    case MINIC_LOONG_MACHINE_ASM_FUTEX_RMW:
        return variant == (uint32_t)loong_linux_futex_rmw_form(text, length);
    case MINIC_LOONG_MACHINE_ASM_LITERAL_DBAR:
        literal = loongarch64_literal_dbar(text, length);
        return literal != NULL && variant ==
            (strcmp(literal, "dbar 0") == 0 ? 1U : 2U);
    case MINIC_LOONG_MACHINE_ASM_HINT_DBAR:
        return variant == 1U && loongarch64_dbar_template(text, length);
    case MINIC_LOONG_MACHINE_ASM_SUB_POSITIVE:
        return loong_linux_sub_positive_form(text, length, &sub) &&
            variant == (sub.width == 8U ? 2U : 0U) +
                       (sub.add_negated ? 1U : 2U);
    case MINIC_LOONG_MACHINE_ASM_UACCESS:
        return loong_linux_uaccess_form(text, length, &uaccess) &&
            variant == (uaccess.load ? 0U : 4U) +
                (uint32_t)(uaccess.width == 1U ? 1U :
                    uaccess.width == 2U ? 2U : uaccess.width == 4U ? 3U : 4U);
    case MINIC_LOONG_MACHINE_ASM_PERCPU_AMO:
        percpu = loong_percpu_amo_form(text, length);
        return percpu != SIZE_MAX && variant == (uint32_t)percpu + 1U;
    case MINIC_LOONG_MACHINE_ASM_CSR:
        return variant == loong_csr_template(text, length);
    case MINIC_LOONG_MACHINE_ASM_AMO_STORE:
    case MINIC_LOONG_MACHINE_ASM_AMO_FETCH:
        atom = loong_atomic_template(text, length,
                    id == MINIC_LOONG_MACHINE_ASM_AMO_FETCH);
        return atom != NULL &&
            variant == (uint32_t)(atom - loong_atomic) + 1U;
    default:
        return false;
    }
}

static bool loong_linux_scalar_template_requires_typed_id(
    const char *text, size_t length) {
    return loong_linux_clock_form(text, length) != LOONG_CLOCK_NONE ||
        loong_linux_irq_switch_template(text, length, true) ||
        loong_linux_invtlb_form(text, length) != 0U ||
        loong_linux_add_unless_form(text, length) != 0U ||
        loong_linux_futex_rmw_form(text, length) != LOONG_FUTEX_NONE ||
        loongarch64_literal_dbar(text, length) != NULL ||
        loongarch64_dbar_template(text, length) ||
        loong_linux_sub_positive_form(text, length,
                                     &(LoongLinuxSubPositive){0}) ||
        loong_linux_uaccess_form(text, length, &(LoongLinuxUaccess){0}) ||
        loong_percpu_amo_form(text, length) != SIZE_MAX ||
        loong_csr_template(text, length) != 0U ||
        loong_atomic_template(text, length, false) != NULL ||
        loong_atomic_template(text, length, true) != NULL;
}

static bool loong_linux_descriptor_emit(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *instruction, const MinicCoreInlineAsm *source) {
    const LoongLinuxStructuredForm *form;
    size_t index;
    if (source == NULL || source->template_text == NULL ||
        source->target_machine_form_id == 0U)
        return false;
    /* Nonzero typed IDs are dispatched before all unrelated machine paths.
     * Invalid IDs and mismatched immutable templates cannot fall through. */
    index = (size_t)(source->target_machine_form_id - 1U);
    if (!minic_loongarch64_typed_descriptor_matches_template(
            source->target_machine_form_id,
            source->target_machine_form_variant,
            source->template_text, source->template_length))
        return false;
    form = &loong_linux_structured_forms[index];
    return form->emit(out, ctx, instruction, source);
}

bool minic_loongarch64_emit_qualified_structured_asm(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *info;
    if (out == NULL || ctx == NULL || ctx->fn == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        !minic_core_function_type_is_kind(ctx->fn, ins->core_type_id, MINIC_CORE_TYPE_VOID) ||
        ins->value.structured_inline_asm.inline_asm_id >= ctx->fn->inline_asm_count)
        return false;
    info = &ctx->fn->inline_asms[ins->value.structured_inline_asm.inline_asm_id];
    /* Typed descriptor ownership takes priority over SIMD and the remaining
     * legacy emitters. A forged ID must fail closed, never fall through. */
    if (info->target_machine_form_id != 0U) {
        const uint32_t id = info->target_machine_form_id;
        const uint32_t variant = info->target_machine_form_variant;
        if (id <= sizeof(loong_linux_structured_forms) /
                  sizeof(loong_linux_structured_forms[0]))
            return loong_linux_descriptor_emit(out, ctx, ins, info);
        if (id == MINIC_LOONG_MACHINE_ASM_SIMD)
            return minic_loongarch64_simd_emit_structured(out, ctx, ins, info);
        if (!minic_loongarch64_typed_scalar_form_matches_template(
                id, variant, info->template_text, info->template_length))
            return false;
        switch ((MinicLoongLinuxMachineAsmForm)id) {
        case MINIC_LOONG_MACHINE_ASM_CLOCK:
            return loong_linux_clock_emit(out, ctx, ins, info,
                (LoongLinuxClockAsm)variant);
        case MINIC_LOONG_MACHINE_ASM_IRQ_SWITCH:
            return loong_linux_irq_switch_emit(out, ctx, ins, info);
        case MINIC_LOONG_MACHINE_ASM_INVTLB:
            return loong_linux_invtlb_emit(out, ctx, ins, info, variant);
        case MINIC_LOONG_MACHINE_ASM_ADD_UNLESS:
            return loong_linux_add_unless_emit(out, ctx, ins, info, variant);
        case MINIC_LOONG_MACHINE_ASM_FUTEX_RMW:
            return loong_linux_futex_rmw_emit(out, ctx, ins, info,
                (LoongLinuxFutexRmw)variant);
        case MINIC_LOONG_MACHINE_ASM_LITERAL_DBAR: {
            const char *literal = loongarch64_literal_dbar(
                info->template_text, info->template_length);
            if (literal == NULL || !info->is_volatile || info->is_goto ||
                !info->has_memory_clobber || info->register_clobber_count ||
                ins->value.structured_inline_asm.operand_count != 0U)
                return false;
            return fprintf(out, "  %s\n", literal) >= 0;
        }
        case MINIC_LOONG_MACHINE_ASM_HINT_DBAR:
            return minic_loongarch64_emit_dbar(out, ctx, ins);
        case MINIC_LOONG_MACHINE_ASM_SUB_POSITIVE: {
            LoongLinuxSubPositive form;
            return loong_linux_sub_positive_form(
                info->template_text, info->template_length, &form) &&
                loong_linux_sub_positive_emit(out, ctx, ins, info, &form);
        }
        case MINIC_LOONG_MACHINE_ASM_UACCESS: {
            LoongLinuxUaccess form;
            return loong_linux_uaccess_form(
                info->template_text, info->template_length, &form) &&
                loong_linux_uaccess_emit(out, ctx, ins, info, &form);
        }
        case MINIC_LOONG_MACHINE_ASM_PERCPU_AMO:
            return loong_percpu_amo_emit(out, ctx, ins, info);
        case MINIC_LOONG_MACHINE_ASM_CSR:
            return minic_loongarch64_emit_csr(out, ctx, ins, info);
        case MINIC_LOONG_MACHINE_ASM_AMO_STORE:
            return minic_loongarch64_emit_amo(out, ctx, ins, info);
        case MINIC_LOONG_MACHINE_ASM_AMO_FETCH:
            return minic_loongarch64_emit_amo_fetch(out, ctx, ins, info);
        default:
            return false;
        }
    }
    /* The old table-owned exact text must not gain a new path merely because
     * a synthetic Core producer erased the typed ID. This is a *rejection*
     * guard, never machine-side family classification or emission. */
    if (minic_loongarch64_descriptor_template_requires_typed_id(
            info->template_text, info->template_length) ||
        loong_linux_scalar_template_requires_typed_id(
            info->template_text, info->template_length) ||
        minic_loongarch64_simd_structured_template_requires_typed_id(
            info->template_text, info->template_length))
        return false;
    /* Every native GNU LoongArch scalar family is now source-qualified and
     * post-normalization sealed. No operand-count/text-only fallback can
     * authorize hardware instructions in a synthetic Core producer. */
    return false;
}
bool minic_loongarch64_emit_dbar(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn;
    const MinicCoreInlineAsm *info;
    const MinicCoreStructuredInlineAsmOperand *b;
    const MinicCoreInstruction *def;
    MinicCoreInstructionId definition;
    int64_t hint;
    if (out == NULL || ctx == NULL || ctx->fn == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        !minic_core_function_type_is_kind(ctx->fn, ins->core_type_id, MINIC_CORE_TYPE_VOID) ||
        ins->value.structured_inline_asm.operand_count != 1U ||
        ins->value.structured_inline_asm.inline_asm_id >= ctx->fn->inline_asm_count)
        return false;
    fn = ctx->fn;
    info = &fn->inline_asms[ins->value.structured_inline_asm.inline_asm_id];
    b = &ins->value.structured_inline_asm.operands[0];
    if (!info->is_volatile || info->is_goto ||
        info->register_clobber_count != 0U ||
        !info->has_memory_clobber ||
        !loongarch64_dbar_template(info->template_text, info->template_length) ||
        b->operand_index != 0U ||
        b->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        b->has_fixed_register_binding || b->early_clobber ||
        b->value >= fn->value_count)
        return false;
    definition = fn->values[b->value].definition;
    if (definition >= fn->instruction_count)
        return false;
    def = &fn->instructions[definition];
    if (def->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT ||
        def->result != b->value)
        return false;
    hint = def->value.integer_value;
    if (!loongarch64_dbar_hint(hint)) return false;
    /* DBAR executes in the user/kernel ISA, and carries hardware ordering.
     * No stack spill is needed because the operand is an exact constant. */
    return fprintf(out, "  dbar %lld\n", (long long)hint) >= 0;
}
