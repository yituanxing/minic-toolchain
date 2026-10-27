#include "target/x86_64/asm_contract.h"
#include <string.h>

/* Target-owned GNU register/constraint admission: no Core lowering state,
 * no SSA mutation. Additional x86 forms extend this owner, not core_lower. */
static bool target_constraint_is(const MinicInlineAsmOperand *op, const char *s) {
    const size_t n = s ? strlen(s) : 0U;
    return op && s && op->constraint_text && op->constraint_length == n &&
           memcmp(op->constraint_text, s, n) == 0;
}

static bool c_is(const MinicInlineAsmOperand *op, const char *s) {
    return target_constraint_is(op, s);
}
static bool clobber_is(const MinicInlineAsmRegisterClobber *op, const char *s) {
    const size_t n = strlen(s);
    return op->name && op->name_length == n && memcmp(op->name, s, n) == 0;
}
/* Whitespace is irrelevant for these four specific instructions, whose
 * operand constraints and exact instruction sequence are checked together. */
bool minic_x86_64_asm_mulacc_template(const char *template_text,
                                       size_t template_length) {
    static const char expected[] = "mulq%7addq%%rax,%0adcq%%rdx,%1adcq$0,%2";
    char text[128];
    size_t i, j = 0U;
    if (template_length >= sizeof(text)) return false;
    for (i = 0U; i < template_length; ++i) {
        const char c = template_text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (j >= sizeof(text)-1U) return false;
        text[j++] = c;
    }
    text[j] = '\0';
    return strcmp(text, expected) == 0;
}

/* Original Linux mul_u64_u64_div_u64:
 *   asm("mulq %2; divq %3" : "=a"(q)
 *       : "a"(a), "rm"(mul), "rm"(div) : "rdx");
 * Admit one exact machine sequence, never generic executable template
 * passthrough with unchecked physical clobbers. The machine emitter chooses
 * the register alternative for both rm inputs and keeps RDX out of inputs. */
bool minic_x86_64_asm_muldiv_template(const char *text, size_t length) {
    static const char expected[] = "mulq%2;divq%3";
    char normalized[64];
    size_t i, j = 0U;
    if (!text || length >= sizeof(normalized)) return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (j + 1U >= sizeof(normalized)) return false;
        normalized[j++] = c;
    }
    normalized[j] = '\0';
    return strcmp(normalized, expected) == 0;
}

/* Exact x86 Linux word-sized GNU BSF/BSR spelling. The REP prefix on
 * Linux __ffs is deliberate: it becomes TZCNT where available, and
 * preserves BSF bit-index semantics for nonzero inputs on older CPUs.
 * An all-zero input has no defined output; callers must not depend on it. */
bool minic_x86_64_asm_bit_scan_template(
    const char *text, size_t length, bool *is_reverse) {
    char normalized[64];
    size_t i, used = 0U;
    if (text == NULL || is_reverse == NULL || length >= sizeof(normalized))
        return false;
    for (i = 0U; i < length; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            continue;
        if (used + 1U >= sizeof(normalized))
            return false;
        normalized[used++] = c;
    }
    normalized[used] = '\0';
    if (strcmp(normalized, "rep;bsf%1,%0") == 0) {
        *is_reverse = false;
        return true;
    }
    if (strcmp(normalized, "bsr%1,%0") == 0) {
        *is_reverse = true;
        return true;
    }
    return false;
}

/* The GNU =r + matched-digit family owns only explicit register operands.
 * No hidden target machine registers, jumps, labels, function calls or
 * directives. Every template instruction is made of a conservative x86
 * scalar carry/move mnemonic and references only %0..%{outputs-1}. */
/* Parser + x86 machine emitter share this strict no-side-channel grammar:
 * each item is cmc or a supported 64-bit two-register mnemonic with exactly
 * two %0..%3 operands. The previous implementation checked only the mnemonic
 * prefix and counted %N occurrences, but silently skipped ANY other bytes
 * (such as '; syscall' or 'call foo') before newline. Since the downstream
 * machine emitter copies the original template, that bypassed ISA ownership.
 * Whitespace, newline and semicolon are explicit item separators here. */
static bool x86_matched_space(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

bool minic_x86_64_asm_matched_template(const char *template_text,
                                        size_t template_length, size_t outputs) {
    static const char *const binary_ops[] = {
        "movq", "addq", "adcq", "subq", "sbbq"
    };
    size_t i = 0U, lines = 0U, refs = 0U, k;
    if (template_text == NULL || template_length == 0U ||
        template_length > 1024U || outputs == 0U || outputs > 4U)
        return false;
    while (i < template_length) {
        bool carry_only = false, matched = false;
        while (i < template_length &&
               (x86_matched_space(template_text[i]) ||
                template_text[i] == '\n' || template_text[i] == ';'))
            ++i;
        if (i == template_length) break;
        if (i + 3U <= template_length &&
            memcmp(template_text + i, "cmc", 3U) == 0) {
            carry_only = true;
            i += 3U;
        } else {
            for (k = 0U; k < sizeof(binary_ops) / sizeof(binary_ops[0]); ++k) {
                if (i + 4U <= template_length &&
                    memcmp(template_text + i, binary_ops[k], 4U) == 0) {
                    matched = true;
                    i += 4U;
                    break;
                }
            }
            if (!matched) return false;
            /* Require a real separator after the mnemonic, not e.g.
             * movq_suffix masquerading as movq. */
            if (i >= template_length || !x86_matched_space(template_text[i]))
                return false;
            while (i < template_length && x86_matched_space(template_text[i]))
                ++i;
            for (k = 0U; k < 2U; ++k) {
                if (i + 2U > template_length ||
                    template_text[i] != '%' ||
                    template_text[i + 1U] < '0' ||
                    (size_t)(template_text[i + 1U] - '0') >= outputs)
                    return false;
                i += 2U;
                ++refs;
                while (i < template_length && x86_matched_space(template_text[i]))
                    ++i;
                if (k == 0U) {
                    if (i >= template_length || template_text[i] != ',')
                        return false;
                    ++i;
                    while (i < template_length && x86_matched_space(template_text[i]))
                        ++i;
                }
            }
        }
        while (i < template_length && x86_matched_space(template_text[i]))
            ++i;
        /* Never allow an ignored instruction suffix, address decoration,
         * extra operand, literal register or comment to reach GNU AS. */
        if (i < template_length && template_text[i] != '\n' &&
            template_text[i] != ';')
            return false;
        if (carry_only && refs == 0U && i == template_length)
            return false; /* cmc alone cannot qualify a matched group */
        ++lines;
        if (i < template_length) ++i;
    }
    return lines > 0U && refs >= 2U;
}

static bool qualified(const MinicC0Program *program,
                      const MinicInlineAsm *src, bool *is_mulacc, bool *is_muldiv) {
    static const char *const cpuid_out[4] = {"=a","=b","=c","=d"};
    static const char *const cpuid_in[4] = {"0","1","2","3"};
    static const char *const mul_in[5] = {"0","1","2","a","m"};
    size_t i, n;
    bool generic = false;
    bool bit_scan = false, reverse = false;
    MinicType normalized;
    if (!program || !src || !src->template_text || !src->outputs ||
        !src->inputs || src->is_goto || src->label_count != 0U ||
        !is_mulacc || !is_muldiv) return false;
    *is_mulacc = false;
    *is_muldiv = false;
    if (src->output_count == 4U && src->input_count == 4U &&
        src->clobber_count == 0U && src->register_clobber_count == 0U &&
        !src->has_memory_clobber && src->template_length == 5U &&
        memcmp(src->template_text, "cpuid", 5U) == 0) {
        *is_mulacc = false;
        n = 4U;
        for (i = 0U; i < n; ++i)
            if (!c_is(&src->outputs[i], cpuid_out[i]) ||
                !c_is(&src->inputs[i], cpuid_in[i])) return false;
    } else if (src->is_volatile && src->output_count == 3U &&
               src->input_count == 5U && src->clobber_count == 2U &&
               src->register_clobber_count == 2U &&
               !src->has_memory_clobber && src->register_clobbers &&
               ((clobber_is(&src->register_clobbers[0], "cc") &&
                 clobber_is(&src->register_clobbers[1], "dx")) ||
                (clobber_is(&src->register_clobbers[0], "dx") &&
                 clobber_is(&src->register_clobbers[1], "cc"))) &&
               minic_x86_64_asm_mulacc_template(src->template_text, src->template_length)) {
        *is_mulacc = true;
        n = 3U;
        for (i = 0U; i < n; ++i)
            if (!c_is(&src->outputs[i], "=rm") ||
                !c_is(&src->inputs[i], mul_in[i])) return false;
        if (!c_is(&src->inputs[3], "a") ||
            !c_is(&src->inputs[4], "m")) return false;
    } else if (src->output_count == 1U && src->input_count == 3U &&
               src->clobber_count == 1U && src->register_clobber_count == 1U &&
               !src->has_memory_clobber && src->register_clobbers &&
               clobber_is(&src->register_clobbers[0], "rdx") &&
               minic_x86_64_asm_muldiv_template(src->template_text, src->template_length) &&
               c_is(&src->outputs[0], "=a") &&
               c_is(&src->inputs[0], "a") &&
               c_is(&src->inputs[1], "rm") &&
               c_is(&src->inputs[2], "rm")) {
        *is_muldiv = true;
    } else if (src->output_count == 1U && src->input_count == 1U &&
               !src->is_goto && src->label_count == 0U &&
               !src->has_memory_clobber &&
               src->clobber_count == 0U && src->register_clobber_count == 0U &&
               minic_x86_64_asm_bit_scan_template(
                   src->template_text, src->template_length, &reverse) &&
               c_is(&src->outputs[0], "=r") &&
               (c_is(&src->inputs[0], "r") || c_is(&src->inputs[0], "rm"))) {
        /* The Core retains one typed output lvalue and a scalar SSA input.
         * Both word operands use the target's native 64-bit unsigned long.
         * Does not admit x86 32-bit BSF or matched third operands. */
        bit_scan = true;
    } else if (src->is_volatile && src->has_memory_clobber &&
               src->clobber_count == 1U &&
               src->register_clobber_count == 0U &&
               ((src->output_count == 4U && src->input_count == 3U) ||
                (src->output_count == 3U && src->input_count == 2U) ||
                (src->output_count == 1U && src->input_count == 1U)) &&
               minic_x86_64_asm_matched_template(src->template_text, src->template_length, src->output_count)) {
        *is_mulacc = false;
        generic = true;
        n = src->output_count;
        for (i = 0U; i < n; ++i)
            if (!c_is(&src->outputs[i], "=r")) return false;
        for (i = 0U; i < src->input_count; ++i) {
            const MinicInlineAsmOperand *op = &src->inputs[i];
            if (!op->constraint_text || op->constraint_length != 1U ||
                op->constraint_text[0] != (char)('0' + i))
                return false;
        }
    } else return false;

    for (i = 0U; i < src->output_count + src->input_count; ++i) {
        const bool is_output = i < src->output_count;
        const MinicInlineAsmOperand *op = is_output
            ? &src->outputs[i] : &src->inputs[i-src->output_count];
        const MinicExpression *expr =
            minic_c0_program_expression(program, op->expression);
        const MinicType target_type =
            (*is_mulacc || bit_scan)
                ? minic_type_unsigned_long() : minic_type_unsigned_int();
        if (!expr || !minic_type_unqualified(expr->type, &normalized) ||
            (*is_muldiv
                ? (!minic_type_equal(normalized, minic_type_unsigned_long()) &&
                   !minic_type_equal(normalized, minic_type_unsigned_long_long()))
                : generic ? (!minic_type_is_integer(normalized) &&
                             !minic_type_is_pointer(normalized))
                          : !minic_type_equal(normalized, target_type)) ||
            (is_output && (expr->value_category != MINIC_VALUE_LVALUE ||
                           minic_type_is_const(expr->type))) ||
            (*is_mulacc && i == 7U &&
             expr->value_category != MINIC_VALUE_LVALUE))
            return false;
    }
    return true;
}

bool minic_x86_64_qualify_inline_asm(
    const MinicC0Program *program, const MinicInlineAsm *source,
    MinicTargetInlineAsmContract *contract) {
    bool is_mulacc = false, is_muldiv = false;
    size_t i, count;
    if (!contract || !qualified(program, source, &is_mulacc, &is_muldiv)) return false;
    (void)is_muldiv; /* all four mul/div operand roles are scalar/register */
    count = source->output_count + source->input_count;
    if (count > MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT) return false;
    contract->operand_count = count;
    for (i = 0U; i < count; ++i)
        contract->kinds[i] = i < source->output_count
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
            : is_mulacc && i == 7U
                ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT
                : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    return true;
}
