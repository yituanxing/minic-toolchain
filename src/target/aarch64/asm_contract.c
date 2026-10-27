#include "target/aarch64/asm_contract.h"
#include "frontend/const_eval.h"
#include <string.h>
/* Linux 6.6 array_index_mask_nospec: compare two unsigned LP64 words,
 * consume NZCV immediately with SBC, store the all-zero/all-one mask. */
static bool mask_template(const char *text, size_t len) {
    static const char expected[] = "cmp%1,%2sbc%0,xzr,xzr";
    char norm[96];
    size_t i, n = 0U;
    if (!text || len >= sizeof(norm)) return false;
    for (i = 0U; i < len; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        if (n + 1U >= sizeof(norm)) return false;
        norm[n++] = c;
    }
    norm[n] = '\0';
    return strcmp(norm, expected) == 0;
}
/* Exact original Linux 6.6.143 MRS register read family. These privileged
 * system registers can be assembled for EL1; do not user-mode QEMU execute
 * them and never turn this into arbitrary template passthrough. */
static const char *aarch64_linux_mrs_register(const char *text, size_t len) {
    char normalized[96];
    size_t i, used = 0U;
    static const struct { const char *text; const char *reg; } allowed[] = {
        {"mrs%0,sp_el0", "sp_el0"},
        {"mrs%0,tpidr_el1", "tpidr_el1"},
        {"mrs%0,tpidr_el2", "tpidr_el2"},
        {"mrs%0,daif", "daif"}
    };
    if (text == NULL || len >= sizeof(normalized))
        return NULL;
    for (i = 0U; i < len; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            continue;
        if (used + 1U >= sizeof(normalized))
            return NULL;
        normalized[used++] = c;
    }
    normalized[used] = '\0';
    for (i = 0U; i < sizeof(allowed) / sizeof(allowed[0]); ++i)
        if (strcmp(allowed[i].text, normalized) == 0)
            return allowed[i].reg;
    return NULL;
}
/* The architectural DMB/DSB/ISB operations are hardware fences; they
 * cannot be silently downgraded into a compiler-only memory clobber.
 * The source and machine both use this target-owned exact template table. */
static const char *aarch64_barrier_template(const char *text, size_t len) {
    static const char *const allowed[] = {
        "dmb ish", "dmb ishld", "dmb ishst", "dmb sy",
        "dmb osh", "dmb oshld", "dmb oshst",
        "dsb sy", "dsb ish", "isb"
    };
    char normalized[64];
    size_t i, n = 0U;
    if (text == NULL || len == 0U || len >= sizeof(normalized))
        return NULL;
    for (i = 0U; i < len; ++i) {
        const char c = text[i];
        if (c == '\t' || c == '\r' || c == '\n')
            return NULL; /* no hidden second hardware instruction */
        if (n + 1U >= sizeof(normalized))
            return NULL;
        normalized[n++] = c;
    }
    while (n != 0U && normalized[n - 1U] == ' ') --n;
    normalized[n] = '\0';
    for (i = 0U; i < sizeof(allowed) / sizeof(allowed[0]); ++i)
        if (strcmp(normalized, allowed[i]) == 0)
            return allowed[i];
    return NULL;
}
/* Linux BRK BUG trap: the AArch64 GNU I constraint is an add/sub
 * immediate class, but BRK itself takes an imm16. Admit the exact
 * brk %0 template with a literal in the intersection (0..4095).
 * Never treat a GNU constraint as permission to pass through arbitrary ASM. */
static bool aarch64_linux_brk_template(const char *text, size_t length) {
    static const char exact[] = "brk%0";
    char compact[32];
    size_t i, n = 0U;
    if (text == NULL || length == 0U || length >= sizeof(compact)) return false;
    for (i = 0U; i < length; ++i) {
        char c = text[i];
        if (c == '\r' || c == '\n') return false;
        if (c == ' ' || c == '\t') continue;
        if (n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    return strcmp(compact, exact) == 0;
}
static bool aarch64_linux_brk_value(int64_t value) {
    return value >= 0 && value <= 4095;
}
/* Real Linux arm64 store-release GNU ASM: release ordering is hardware. */
static size_t aarch64_stlr_width(const char *s, size_t len) {
    static const char *const form[] = {
        NULL, "stlrb%w1,%0", "stlrh%w1,%0", NULL,
        "stlr%w1,%0", NULL, NULL, NULL, "stlr%x1,%0"
    };
    char norm[64]; size_t i, n = 0U;
    if (!s || !len || len >= sizeof(norm)) return 0U;
    for (i = 0U; i < len; ++i) {
        if (s[i] == '\n' || s[i] == '\r' || s[i] == ';') return 0U;
        if (s[i] == ' ' || s[i] == '\t') continue;
        if (n + 1U >= sizeof(norm)) return 0U;
        norm[n++] = s[i];
    }
    norm[n] = '\0';
    for (i = 1U; i <= 8U; ++i)
        if (form[i] && strcmp(norm, form[i]) == 0) return i;
    return 0U;
}
static bool aarch64_stlr_type(MinicType type, size_t width) {
    MinicType plain;
    if (!minic_type_unqualified(type, &plain) ||
        plain.base_kind != MINIC_TYPE_BASE_INT) return false;
    return (width == 1U && plain.integer_rank == MINIC_INTEGER_RANK_CHAR) ||
           (width == 2U && plain.integer_rank == MINIC_INTEGER_RANK_SHORT) ||
           (width == 4U && plain.integer_rank == MINIC_INTEGER_RANK_INT) ||
           (width == 8U && (plain.integer_rank == MINIC_INTEGER_RANK_LONG ||
                            plain.integer_rank == MINIC_INTEGER_RANK_LONG_LONG));
}
/* Original arm64 Linux 6.6 LL/SC atomic_{and,or,xor} and atomic64_
 * exactly as emitted from atomic_ll_sc.h; no generic instruction-string
 * passthrough. The six admitted templates have no hidden instruction slots. */
static unsigned int aarch64_llsc_op(const char *text, size_t len) {
    static const char *const forms[] = {
        "//atomic_andprfmpstl1strm,%21:ldxr%w0,%2and%w0,%w0,%w3stxr%w1,%w0,%2cbnz%w1,1b",
        "//atomic_orprfmpstl1strm,%21:ldxr%w0,%2orr%w0,%w0,%w3stxr%w1,%w0,%2cbnz%w1,1b",
        "//atomic_xorprfmpstl1strm,%21:ldxr%w0,%2eor%w0,%w0,%w3stxr%w1,%w0,%2cbnz%w1,1b",
        "//atomic64_andprfmpstl1strm,%21:ldxr%0,%2and%0,%0,%3stxr%w1,%0,%2cbnz%w1,1b",
        "//atomic64_orprfmpstl1strm,%21:ldxr%0,%2orr%0,%0,%3stxr%w1,%0,%2cbnz%w1,1b",
        "//atomic64_xorprfmpstl1strm,%21:ldxr%0,%2eor%0,%0,%3stxr%w1,%0,%2cbnz%w1,1b",
        /* GNU atomic{,64}_andnot: BIC(register) needs ordinary r input,
         * NOT the AArch64 L-immediate-or-register alternative. */
        "//atomic_andnotprfmpstl1strm,%21:ldxr%w0,%2bic%w0,%w0,%w3stxr%w1,%w0,%2cbnz%w1,1b",
        "//atomic64_andnotprfmpstl1strm,%21:ldxr%0,%2bic%0,%0,%3stxr%w1,%0,%2cbnz%w1,1b"
    };
    char compact[192]; size_t i, n = 0U;
    if (!text || !len || len >= sizeof(compact)) return 0U;
    for (i = 0U; i < len; ++i) {
        if (text[i] == ' ' || text[i] == '\t' ||
            text[i] == '\n' || text[i] == '\r') continue;
        if (n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = text[i];
    }
    compact[n] = '\0';
    for (i = 0U; i < sizeof(forms) / sizeof(forms[0]); ++i)
        if (strcmp(forms[i], compact) == 0) return (unsigned int)(i + 1U);
    return 0U;
}
/* Real AArch64 privileged DAIF interrupt-mask MRS/MSR pair from the
 * exact Linux sysreg/irqflags.h macros. Do not execute under user QEMU.
 * Only GNU GAS/objdump may inspect the encoded privileged instruction. */
/* The actual Linux 6.6 atomic{,64}_fetch_{and,or,xor,andnot} with ordering variants
 * contract has five GNU operands: old, new, store-status, +Q memory and
 * Kr/Lr value. The AArch64 ISA adapter owns the exact LL/SC opcode, acquire
 * and release suffixes and full-barrier ordering, not Common Core. */
static unsigned int aarch64_fetch_logic_form(const char *text, size_t len) {
    static const char *const suffix[4] = {
        "", "_relaxed", "_acquire", "_release"
    };
    static const char *const operation[4] = {
        "and", "or", "xor", "andnot"
    };
    static const char *const alu[4] = {
        "and", "orr", "eor", "bic"
    };
    static const char *const ld[4] = {
        "ldxr", "ldxr", "ldaxr", "ldxr"
    };
    static const char *const st[4] = {
        "stlxr", "stxr", "stxr", "stlxr"
    };
    char compact[240], expected[240];
    size_t i, n = 0U, bitness, mode, op;
    if (!text || !len || len >= sizeof(compact)) return 0U;
    for (i = 0U; i < len; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (n + 1U >= sizeof(compact)) return 0U;
        compact[n++] = c;
    }
    compact[n] = '\0';
    for (bitness = 0U; bitness < 2U; ++bitness) {
        const char *old = bitness ? "%0" : "%w0";
        const char *new_value = bitness ? "%1" : "%w1";
        const char *arg = bitness ? "%4" : "%w4";
        for (op = 0U; op < 4U; ++op) {
            for (mode = 0U; mode < 4U; ++mode) {
                const int written = snprintf(
                    expected, sizeof(expected),
                    "//atomic%s_fetch_%s%s"
                    "prfmpstl1strm,%%3"
                    "1:%s%s,%%3"
                    "%s%s,%s,%s"
                    "%s%%w2,%s,%%3"
                    "cbnz%%w2,1b%s",
                    bitness ? "64" : "", operation[op], suffix[mode],
                    ld[mode], old, alu[op], new_value, old, arg,
                    st[mode], new_value, mode == 0U ? "dmbish" : "");
                if (written > 0 && (size_t)written < sizeof(expected) &&
                    strcmp(compact, expected) == 0)
                    return (unsigned int)(bitness * 16U + op * 4U + mode + 1U);
            }
        }
    }
    return 0U;
}
/* Original Linux arch/arm64/include/asm/uaccess.h:
 * __uaccess_mask_ptr() removes bit 55 using a true AArch64 BIC logical
 * immediate. It is not a compiler-only barrier, pointer cast or C AND.
 * GNU source I is a *value* immediate, never a GPR input. */
static bool aarch64_linux_uaccess_bic_template(
    const char *text, size_t length) {
    static const char expected[] = "bic%0,%1,%2";
    char normalized[64];
    size_t i, n = 0U;
    if (!text || length == 0U || length >= sizeof(normalized)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == '\r' || c == '\n' || c == ';') return false;
        if (c == ' ' || c == '\t') continue;
        if (n + 1U >= sizeof(normalized)) return false;
        normalized[n++] = c;
    }
    normalized[n] = '\0';
    return strcmp(normalized, expected) == 0;
}
static bool aarch64_daif_msr_template(const char *text, size_t len) {
    static const char expected[] = "msrdaif,%x0";
    char compact[64]; size_t i, n = 0U;
    if (!text || !len || len >= sizeof(compact)) return false;
    for (i = 0U; i < len; ++i) {
        const char c = text[i];
        if (c == '\r' || c == '\n' || c == ';') return false;
        if (c == ' ' || c == '\t') continue;
        if (n + 1U >= sizeof(compact)) return false;
        compact[n++] = c;
    }
    compact[n] = '\0';
    return strcmp(compact, expected) == 0;
}
static bool constraint(const MinicInlineAsmOperand *op, const char *word) {
    size_t n = strlen(word);
    return op && op->constraint_text && op->constraint_length == n &&
           memcmp(op->constraint_text, word, n) == 0;
}
bool minic_aarch64_qualify_inline_asm(
    const MinicC0Program *program, const MinicInlineAsm *src,
    MinicTargetInlineAsmContract *contract) {
    const MinicExpression *expr;
    MinicType plain;
    size_t i;
    if (program && src && contract && src->is_volatile &&
        !src->is_goto && src->output_count == 0U && src->input_count == 0U &&
        src->clobber_count == 1U && src->register_clobber_count == 0U &&
        src->has_memory_clobber && src->label_count == 0U &&
        aarch64_barrier_template(src->template_text, src->template_length)) {
        contract->operand_count = 0U;
        return true;
    }
    if (program && src && contract && !src->is_goto &&
        src->output_count == 0U && src->input_count == 1U && src->inputs &&
        src->clobber_count == 0U && src->register_clobber_count == 0U &&
        !src->has_memory_clobber && src->label_count == 0U &&
        aarch64_linux_brk_template(src->template_text, src->template_length) &&
        constraint(&src->inputs[0], "I")) {
        expr = minic_c0_program_expression(program, src->inputs[0].expression);
        if (expr && expr->kind == MINIC_EXPRESSION_INTEGER &&
            aarch64_linux_brk_value(expr->value.integer_value)) {
            contract->operand_count = 1U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            return true;
        }
    }
    /* Existing Core roles: =Q address, rZ scalar input. ISA ownership stays
     * in this AArch64 callback, not in Frontend or generic Core scanning. */
    if (program && src && contract && src->is_volatile && !src->is_goto &&
        src->outputs && src->inputs && src->output_count == 1U &&
        src->input_count == 1U && src->clobber_count == 1U &&
        src->has_memory_clobber && src->register_clobber_count == 0U &&
        src->label_count == 0U &&
        constraint(&src->outputs[0], "=Q") &&
        constraint(&src->inputs[0], "rZ")) {
        const size_t width = aarch64_stlr_width(
            src->template_text, src->template_length);
        const MinicExpression *dst = minic_c0_program_expression(
            program, src->outputs[0].expression);
        const MinicExpression *value = minic_c0_program_expression(
            program, src->inputs[0].expression);
        if (width && dst && value &&
            dst->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(dst->type) &&
            aarch64_stlr_type(dst->type, width) &&
            aarch64_stlr_type(value->type, width)) {
            contract->operand_count = 2U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
            contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            return true;
        }
    }
    if (program && src && contract && src->is_volatile && !src->is_goto &&
        src->output_count == 3U && src->input_count == 1U &&
        src->outputs && src->inputs && src->label_count == 0U &&
        src->clobber_count == 0U && src->register_clobber_count == 0U &&
        !src->has_memory_clobber &&
        constraint(&src->outputs[0], "=&r") &&
        constraint(&src->outputs[1], "=&r") &&
        constraint(&src->outputs[2], "+Q") &&
        (constraint(&src->inputs[0], "Lr") ||
         constraint(&src->inputs[0], "r"))) {
        const unsigned int op = aarch64_llsc_op(
            src->template_text, src->template_length);
        const size_t width = (op >= 4U && op <= 6U) || op == 8U ? 8U : 4U;
        bool accepted = op != 0U &&
            ((op >= 7U && constraint(&src->inputs[0], "r")) ||
             (op < 7U && constraint(&src->inputs[0], "Lr")));
        for (i = 0U; i < 4U && accepted; ++i) {
            const MinicInlineAsmOperand *operand = i < 3U ?
                &src->outputs[i] : &src->inputs[0];
            const MinicExpression *arg =
                minic_c0_program_expression(program, operand->expression);
            if (!arg ||
                (i < 3U && (arg->value_category != MINIC_VALUE_LVALUE ||
                            minic_type_is_const(arg->type))) ||
                !aarch64_stlr_type(arg->type, i == 1U ? 8U : width))
                accepted = false;
        }
        if (accepted) {
            contract->operand_count = 4U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
            contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            return true;
        }
    }
    if (program && src && contract && src->is_volatile && !src->is_goto &&
        src->outputs && src->inputs && src->output_count == 4U &&
        src->input_count == 1U && src->label_count == 0U &&
        src->register_clobber_count == 0U &&
        constraint(&src->outputs[0], "=&r") &&
        constraint(&src->outputs[1], "=&r") &&
        constraint(&src->outputs[2], "=&r") &&
        constraint(&src->outputs[3], "+Q")) {
        const unsigned int form = aarch64_fetch_logic_form(
            src->template_text, src->template_length);
        const size_t width = form > 16U ? 8U : 4U;
        const unsigned int mode = form ? (form - 1U) % 4U : 0U;
        const unsigned int op = form ? ((form - 1U) % 16U) / 4U : 0U;
        bool eligible = form != 0U &&
            src->clobber_count == (mode == 1U ? 0U : 1U) &&
            src->has_memory_clobber == (mode != 1U) &&
            constraint(&src->inputs[0],
                       op == 3U ? "r" : width == 8U ? "Lr" : "Kr");
        for (i = 0U; i < 5U && eligible; ++i) {
            const MinicInlineAsmOperand *operand = i < 4U ?
                &src->outputs[i] : &src->inputs[0];
            const MinicExpression *value = minic_c0_program_expression(
                program, operand->expression);
            if (!value ||
                (i < 4U && (value->value_category != MINIC_VALUE_LVALUE ||
                            minic_type_is_const(value->type))) ||
                !aarch64_stlr_type(
                    value->type, i == 2U ? 8U : width))
                eligible = false;
        }
        if (eligible) {
            contract->operand_count = 5U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[3] = MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
            contract->kinds[4] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            return true;
        }
    }
    if (program && src && contract && src->is_volatile && !src->is_goto &&
        src->output_count == 0U && src->input_count == 1U && src->inputs &&
        src->clobber_count == 0U && src->register_clobber_count == 0U &&
        !src->has_memory_clobber && src->label_count == 0U &&
        aarch64_daif_msr_template(src->template_text, src->template_length) &&
        constraint(&src->inputs[0], "rZ")) {
        expr = minic_c0_program_expression(program, src->inputs[0].expression);
        if (expr && minic_type_unqualified(expr->type, &plain) &&
            /* The *original* Linux sysreg.h write_sysreg() materializes
             * u64 (__u64 = unsigned long long), whereas the interrupt
             * caller's flags and AAPCS64 GPR are unsigned long/64-bit.
             * They have equal machine width, not identical C type. */
            (minic_type_equal(plain, minic_type_unsigned_long()) ||
             minic_type_equal(plain, minic_type_unsigned_long_long()))) {
            contract->operand_count = 1U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            return true;
        }
    }
    if (program && src && contract && src->is_volatile &&
        !src->is_goto && src->outputs && src->output_count == 1U &&
        src->input_count == 0U && src->clobber_count == 0U &&
        src->register_clobber_count == 0U && !src->has_memory_clobber &&
        src->label_count == 0U &&
        aarch64_linux_mrs_register(src->template_text, src->template_length) &&
        strcmp(aarch64_linux_mrs_register(
                   src->template_text, src->template_length), "daif") == 0 &&
        constraint(&src->outputs[0], "=r")) {
        expr = minic_c0_program_expression(program, src->outputs[0].expression);
        if (expr && expr->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(expr->type) &&
            minic_type_unqualified(expr->type, &plain) &&
            minic_type_equal(plain, minic_type_unsigned_long())) {
            contract->operand_count = 1U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            return true;
        }
    }
    if (program && src && contract &&
        !src->is_goto && !src->is_volatile &&
        src->outputs && src->output_count == 1U &&
        src->input_count == 0U && src->clobber_count == 0U &&
        src->register_clobber_count == 0U && !src->has_memory_clobber &&
        aarch64_linux_mrs_register(src->template_text, src->template_length) &&
        /* DAIF IRQ mask read is a volatile Linux sysreg operation; do not
         * expand the historical nonvolatile EL1 CPU-offset MRS allowlist. */
        strcmp(aarch64_linux_mrs_register(
                   src->template_text, src->template_length), "daif") != 0 &&
        constraint(&src->outputs[0], "=r")) {
        expr = minic_c0_program_expression(program, src->outputs[0].expression);
        if (expr && minic_type_unqualified(expr->type, &plain) &&
            minic_type_equal(plain, minic_type_unsigned_long()) &&
            expr->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(expr->type)) {
            contract->operand_count = 1U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            return true;
        }
    }
    if (program && src && contract && src->is_volatile && !src->is_goto &&
        src->outputs && src->inputs && src->output_count == 1U &&
        src->input_count == 2U && src->clobber_count == 0U &&
        src->register_clobber_count == 0U && !src->has_memory_clobber &&
        src->label_count == 0U &&
        aarch64_linux_uaccess_bic_template(
            src->template_text, src->template_length) &&
        constraint(&src->outputs[0], "=r") &&
        constraint(&src->inputs[0], "r") &&
        constraint(&src->inputs[1], "i")) {
        const MinicExpression *out = minic_c0_program_expression(
            program, src->outputs[0].expression);
        const MinicExpression *arg = minic_c0_program_expression(
            program, src->inputs[0].expression);
        const MinicExpression *mask = minic_c0_program_expression(
            program, src->inputs[1].expression);
        MinicConstValue constant;
        const MinicTargetInfo *target = minic_target_info_for_architecture(
            MINIC_TARGET_ARCH_AARCH64);
        if (out && arg && mask &&
            out->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(out->type) &&
            minic_type_is_pointer(out->type) &&
            minic_type_is_pointer(arg->type) &&
            minic_type_is_integer(mask->type) &&
            minic_const_eval_integer(program, target,
                                    src->inputs[1].expression, &constant) &&
            constant.bits == (UINT64_C(1) << 55)) {
            contract->operand_count = 3U;
            contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
            contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            return true;
        }
    }
    if (!program || !src || !contract || !src->is_volatile ||
        src->is_goto || !src->outputs || !src->inputs ||
        src->output_count != 1U || src->input_count != 2U ||
        src->clobber_count != 1U || src->register_clobber_count != 1U ||
        src->has_memory_clobber || !src->register_clobbers ||
        !src->register_clobbers[0].name ||
        src->register_clobbers[0].name_length != 2U ||
        memcmp(src->register_clobbers[0].name, "cc", 2U) != 0 ||
        !mask_template(src->template_text, src->template_length) ||
        !constraint(&src->outputs[0], "=r") ||
        !constraint(&src->inputs[0], "r") ||
        !constraint(&src->inputs[1], "Ir"))
        return false;
    for (i = 0U; i < 3U; ++i) {
        const MinicInlineAsmOperand *op = i == 0U
            ? &src->outputs[0] : &src->inputs[i-1U];
        expr = minic_c0_program_expression(program, op->expression);
        if (!expr || !minic_type_unqualified(expr->type, &plain) ||
            !minic_type_equal(plain, minic_type_unsigned_long()) ||
            (i == 0U && (expr->value_category != MINIC_VALUE_LVALUE ||
                         minic_type_is_const(expr->type))))
            return false;
    }
    contract->operand_count = 3U;
    contract->kinds[0] = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
    contract->kinds[1] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    contract->kinds[2] = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}
bool minic_aarch64_emit_qualified_structured_asm(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn;
    const MinicCoreInlineAsm *info;
    const MinicCoreStructuredInlineAsmOperand *b;
    size_t i;
    if (!out || !ctx || !ctx->fn || !ins || ctx->dynamic_frame ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        !minic_type_is_void(ins->type))
        return false;
    fn = ctx->fn;
    if (ins->value.structured_inline_asm.inline_asm_id >= fn->inline_asm_count)
        return false;
    info = &fn->inline_asms[ins->value.structured_inline_asm.inline_asm_id];
    if (ins->value.structured_inline_asm.operand_count == 0U) {
        const char *barrier = aarch64_barrier_template(
            info->template_text, info->template_length);
        if (!barrier || !info->is_volatile || info->is_goto ||
            !info->has_memory_clobber || info->register_clobber_count != 0U)
            return false;
        return fprintf(out, "  %s\n", barrier) >= 0;
    }
    if (ins->value.structured_inline_asm.operand_count == 1U) {
        const MinicCoreStructuredInlineAsmOperand *operand =
            ins->value.structured_inline_asm.operands;
        const char *reg = aarch64_linux_mrs_register(
            info->template_text, info->template_length);
        MinicType pointee;
        if (operand != NULL &&
            operand[0].kind == MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT &&
            aarch64_daif_msr_template(info->template_text, info->template_length)) {
            if (!info->is_volatile || info->is_goto ||
                info->has_memory_clobber || info->register_clobber_count ||
                operand[0].operand_index != 0U ||
                operand[0].has_fixed_register_binding || operand[0].early_clobber ||
                operand[0].value >= fn->value_count ||
                operand[0].value > 4095U / 8U ||
                (!minic_type_equal(fn->values[operand[0].value].type,
                                   minic_type_unsigned_long()) &&
                 !minic_type_equal(fn->values[operand[0].value].type,
                                   minic_type_unsigned_long_long())))
                return false;
            return fprintf(out, "  ldr x9, [sp, #%zu]\n"
                                "  msr daif, x9\n",
                           (size_t)operand[0].value * 8U) >= 0;
        }
        if (operand != NULL &&
            operand[0].kind == MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) {
            const MinicCoreInstruction *definition;
            MinicCoreInstructionId def_id;
            int64_t imm;
            if (!aarch64_linux_brk_template(
                    info->template_text, info->template_length) ||
                info->is_goto || info->has_memory_clobber ||
                info->register_clobber_count != 0U ||
                operand[0].operand_index != 0U ||
                operand[0].has_fixed_register_binding ||
                operand[0].early_clobber ||
                operand[0].value >= fn->value_count)
                return false;
            def_id = fn->values[operand[0].value].definition;
            if (def_id >= fn->instruction_count) return false;
            definition = &fn->instructions[def_id];
            if (definition->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT ||
                definition->result != operand[0].value)
                return false;
            imm = definition->value.integer_value;
            if (!aarch64_linux_brk_value(imm)) return false;
            /* Never execute this in QEMU-user validation: the original
             * Linux BUG instruction intentionally enters a trap. */
            return fprintf(out, "  brk #%lld\n", (long long)imm) >= 0;
        }
        if (reg == NULL || info->is_goto ||
            info->has_memory_clobber || info->register_clobber_count != 0U ||
            operand == NULL || operand[0].operand_index != 0U ||
            operand[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
            operand[0].early_clobber || operand[0].has_fixed_register_binding ||
            operand[0].value >= fn->value_count ||
            operand[0].value > (size_t)4095U / 8U ||
            !minic_type_pointee(fn->values[operand[0].value].type, &pointee) ||
            !minic_type_equal(pointee, minic_type_unsigned_long()))
            return false;
        /* The output SSA operand is an address. The backend reads the real
         * privileged register into an internal GPR and stores the exact LP64
         * word back to the existing Core object. No external symbol/alias. */
        return fprintf(out, "  mrs x9, %s\n"
                            "  ldr x10, [sp, #%zu]\n"
                            "  str x9, [x10]\n",
                       reg, (size_t)operand[0].value * 8U) >= 0;
    }
    if (ins->value.structured_inline_asm.operand_count == 2U) {
        b = ins->value.structured_inline_asm.operands;
        static const char *const op[] = {
            NULL, "stlrb w9, [x10]", "stlrh w9, [x10]", NULL,
            "stlr w9, [x10]", NULL, NULL, NULL, "stlr x9, [x10]"
        };
        const size_t width = aarch64_stlr_width(
            info->template_text, info->template_length);
        MinicType pointee;
        if (!width || !info->is_volatile || info->is_goto ||
            !info->has_memory_clobber || info->register_clobber_count ||
            b[0].operand_index != 0U ||
            b[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT ||
            b[0].has_fixed_register_binding || b[0].early_clobber ||
            b[1].operand_index != 1U ||
            b[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
            b[1].has_fixed_register_binding || b[1].early_clobber ||
            b[0].value >= fn->value_count || b[1].value >= fn->value_count ||
            b[0].value > 4095U / 8U || b[1].value > 4095U / 8U ||
            !minic_type_pointee(fn->values[b[0].value].type, &pointee) ||
            !aarch64_stlr_type(pointee, width) ||
            !aarch64_stlr_type(fn->values[b[1].value].type, width))
            return false;
        /* LP64 Core SSA slots SP-relative; x9/x10 are AAPCS64 caller-saved.
         * STLR/B/H provides release ordering, never a compiler barrier. */
        return fprintf(out, "  ldr x10, [sp, #%zu]\n"
                            "  ldr x9, [sp, #%zu]\n"
                            "  %s\n",
                       (size_t)b[0].value * 8U,
                       (size_t)b[1].value * 8U, op[width]) >= 0;
    }
    if (ins->value.structured_inline_asm.operand_count == 5U) {
        const unsigned int form = aarch64_fetch_logic_form(
            info->template_text, info->template_length);
        const size_t width = form > 16U ? 8U : 4U;
        const unsigned int mode = form ? (form - 1U) % 4U : 0U;
        const unsigned int logic_op = form ? ((form - 1U) % 16U) / 4U : 0U;
        static const char *const alu[] = {"and", "orr", "eor", "bic"};
        static const char *const ld[] = {"ldxr", "ldxr", "ldaxr", "ldxr"};
        static const char *const st[] = {"stlxr", "stxr", "stxr", "stlxr"};
        const MinicCoreStructuredInlineAsmOperand *ops =
            ins->value.structured_inline_asm.operands;
        MinicType pointee;
        /* Core adds the memory effect to *every* checked +Q RMW role,
         * including GNU's syntactically unclobbered _relaxed variant.
         * Exact source clobber shape was validated by the target callback,
         * whereas the normalized Core effect must remain conservative. */
        if (!form || !info->is_volatile || info->is_goto ||
            info->register_clobber_count || !info->has_memory_clobber)
            return false;
        for (i = 0U; i < 5U; ++i) {
            const MinicCoreStructuredInlineAsmOperand *op = &ops[i];
            if (op->operand_index != i || op->early_clobber ||
                op->has_fixed_register_binding ||
                op->value >= fn->value_count ||
                op->value > 4095U / 8U ||
                op->kind != (i == 4U ?
                    MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT :
                    i == 3U ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
                    MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT))
                return false;
            if (i < 4U) {
                if (!minic_type_pointee(fn->values[op->value].type, &pointee) ||
                    !aarch64_stlr_type(pointee, i == 2U ? 8U : width))
                    return false;
            } else if (!aarch64_stlr_type(
                fn->values[op->value].type, width)) {
                return false;
            }
        }
        /* Five checked Core SSA operands, one atomic read-modify-write.
         * Preserve LDXR old value in x9, compute new in x13, publish through
         * STXR and write C result/temporary locals after successful retry.
         * Acquire/release use hardware LDAXR/STLXR, full uses DMB ISH. */
        return fprintf(out,
            "  ldr x10, [sp, #%zu]\n"
            "  ldr x11, [sp, #%zu]\n"
            "  prfm pstl1strm, [x10]\n"
            "1:\n"
            "  %s %s9, [x10]\n"
            "  %s %s13, %s9, %s11\n"
            "  %s w12, %s13, [x10]\n"
            "  cbnz w12, 1b\n"
            "%s"
            "  ldr x14, [sp, #%zu]\n"
            "  str %s9, [x14]\n"
            "  ldr x14, [sp, #%zu]\n"
            "  str %s13, [x14]\n"
            "  ldr x14, [sp, #%zu]\n"
            "  str x12, [x14]\n",
            (size_t)ops[3].value * 8U,
            (size_t)ops[4].value * 8U,
            ld[mode], width == 8U ? "x" : "w",
            alu[logic_op], width == 8U ? "x" : "w",
            width == 8U ? "x" : "w",
            width == 8U ? "x" : "w",
            st[mode], width == 8U ? "x" : "w",
            mode == 0U ? "  dmb ish\n" : "",
            (size_t)ops[0].value * 8U, width == 8U ? "x" : "w",
            (size_t)ops[1].value * 8U, width == 8U ? "x" : "w",
            (size_t)ops[2].value * 8U) >= 0;
    }
    if (ins->value.structured_inline_asm.operand_count == 4U) {
        static const char *const alu[] = {"and", "orr", "eor"};
        const unsigned int form = aarch64_llsc_op(
            info->template_text, info->template_length);
        const size_t width = (form >= 4U && form <= 6U) || form == 8U ? 8U : 4U;
        MinicType pointee;
        if (!form || !info->is_volatile || info->is_goto ||
            !info->has_memory_clobber || info->register_clobber_count)
            return false;
        b = ins->value.structured_inline_asm.operands;
        for (i = 0U; i < 4U; ++i) {
            if (b[i].operand_index != i || b[i].early_clobber ||
                b[i].has_fixed_register_binding ||
                b[i].value >= fn->value_count ||
                b[i].value > 4095U / 8U ||
                b[i].kind != (i == 3U ?
                    MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT :
                    i == 2U ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
                    MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT))
                return false;
            if (i == 3U) {
                if (!aarch64_stlr_type(fn->values[b[i].value].type, width))
                    return false;
            } else {
                if (!minic_type_pointee(fn->values[b[i].value].type, &pointee) ||
                    !aarch64_stlr_type(pointee, i == 1U ? 8U : width))
                    return false;
            }
        }
        /* Exact Linux atomic LL/SC loop with real hardware RMW effects.
         * Result and status outputs write their Core address-backed C locals;
         * memory operand is updated by STXR, not a fabricated ordinary store.
         * x9..x13 are AAPCS64 caller-saved scratch; numeric GAS label is local. */
        return fprintf(out,
            "  ldr x10, [sp, #%zu]\n"
            "  ldr x11, [sp, #%zu]\n"
            "  prfm pstl1strm, [x10]\n"
            "1:\n"
            "  ldxr %s9, [x10]\n"
            "  %s %s9, %s9, %s11\n"
            "  stxr w12, %s9, [x10]\n"
            "  cbnz w12, 1b\n"
            "  ldr x13, [sp, #%zu]\n"
            "  str %s9, [x13]\n"
            "  ldr x13, [sp, #%zu]\n"
            "  str x12, [x13]\n",
            (size_t)b[2].value * 8U,
            (size_t)b[3].value * 8U,
            width == 8U ? "x" : "w",
            form >= 7U ? "bic" : alu[(form - 1U) % 3U],
            width == 8U ? "x" : "w",
            width == 8U ? "x" : "w",
            width == 8U ? "x" : "w",
            width == 8U ? "x" : "w",
            (size_t)b[0].value * 8U,
            width == 8U ? "x" : "w",
            (size_t)b[1].value * 8U) >= 0;
    }
    if (ins->value.structured_inline_asm.operand_count == 3U &&
        aarch64_linux_uaccess_bic_template(
            info->template_text, info->template_length)) {
        const MinicCoreStructuredInlineAsmOperand *ops =
            ins->value.structured_inline_asm.operands;
        const MinicCoreInstruction *imm;
        MinicCoreInstructionId def;
        MinicType pointee;
        if (!info->is_volatile || info->is_goto ||
            info->has_memory_clobber || info->register_clobber_count)
            return false;
        for (i = 0U; i < 3U; ++i) {
            if (ops[i].operand_index != i ||
                ops[i].kind != (i == 0U ?
                    MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                    MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
                ops[i].has_fixed_register_binding || ops[i].early_clobber ||
                ops[i].value >= fn->value_count ||
                ops[i].value > 4095U / 8U)
                return false;
        }
        if (!minic_type_pointee(fn->values[ops[0].value].type, &pointee) ||
            !minic_type_is_pointer(pointee) ||
            !minic_type_is_pointer(fn->values[ops[1].value].type))
            return false;
        def = fn->values[ops[2].value].definition;
        if (def >= fn->instruction_count) return false;
        imm = &fn->instructions[def];
        if (imm->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT ||
            imm->result != ops[2].value ||
            (uint64_t)imm->value.integer_value != (UINT64_C(1) << 55))
            return false;
        /* Hardware BIC clears the single TBI/sign-extension tag bit 55
         * as GNU specifies. Stack SSA output is a C pointer object. */
        return fprintf(out,
            "  ldr x10, [sp, #%zu]\n"
            "  bic x9, x10, #0x80000000000000\n"
            "  ldr x11, [sp, #%zu]\n"
            "  str x9, [x11]\n",
            (size_t)ops[1].value * 8U,
            (size_t)ops[0].value * 8U) >= 0;
    }
    if (ins->value.structured_inline_asm.operand_count != 3U)
        return false;
    if (!info->is_volatile || info->is_goto || info->has_memory_clobber ||
        info->register_clobber_count != 1U || !info->register_clobbers ||
        !info->register_clobbers[0].name ||
        info->register_clobbers[0].name_length != 2U ||
        memcmp(info->register_clobbers[0].name, "cc", 2U) != 0 ||
        !mask_template(info->template_text, info->template_length))
        return false;
    b = ins->value.structured_inline_asm.operands;
    for (i = 0U; i < 3U; ++i) {
        MinicType ty;
        if (b[i].operand_index != i || b[i].value >= fn->value_count ||
            b[i].has_fixed_register_binding || b[i].early_clobber ||
            b[i].kind != (i == 0U ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
            b[i].value > (size_t)4095U / 8U)
            return false;
        ty = fn->values[b[i].value].type;
        if (i == 0U && !minic_type_pointee(ty, &ty)) return false;
        if (!minic_type_equal(ty, minic_type_unsigned_long()))
            return false;
    }
    /* GNU Ir admits a register alternative: x11 holds the size. x9/x10/x11
     * are caller-saved scratch and all live C values are in existing SSA
     * stack slots. AArch64 NZCV from CMP survives until the following SBC.
     * No direct raw-template passthrough or unsafe physical clobber alias. */
    return fprintf(out,
        "  ldr x10, [sp, #%zu]\n"
        "  ldr x11, [sp, #%zu]\n"
        "  cmp x10, x11\n"
        "  sbc x9, xzr, xzr\n"
        "  ldr x10, [sp, #%zu]\n"
        "  str x9, [x10]\n",
        (size_t)b[1].value * 8U,
        (size_t)b[2].value * 8U,
        (size_t)b[0].value * 8U) >= 0;
}
