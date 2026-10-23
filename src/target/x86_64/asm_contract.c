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

/* The GNU =r + matched-digit family owns only explicit register operands.
 * No hidden target machine registers, jumps, labels, function calls or
 * directives. Every template instruction is made of a conservative x86
 * scalar carry/move mnemonic and references only %0..%{outputs-1}. */
bool minic_x86_64_asm_matched_template(const char *template_text,
                                        size_t template_length, size_t outputs) {
    size_t i = 0U, lines = 0U, refs = 0U;
    if (!template_text || template_length > 1024U) return false;
    while (i < template_length) {
        size_t begin, len;
        while (i < template_length &&
               (template_text[i] == ' ' || template_text[i] == '\t' ||
                template_text[i] == '\r')) ++i;
        if (i == template_length) break;
        if (template_text[i] == '\n') { ++i; continue; }
        begin = i;
        while (i < template_length &&
               template_text[i] >= 'a' &&
               template_text[i] <= 'z') ++i;
        len = i - begin;
        if (!((len == 3U &&
               memcmp(template_text + begin, "cmc", 3U) == 0) ||
              (len == 4U &&
               (memcmp(template_text + begin, "movq", 4U) == 0 ||
                memcmp(template_text + begin, "addq", 4U) == 0 ||
                memcmp(template_text + begin, "adcq", 4U) == 0 ||
                memcmp(template_text + begin, "subq", 4U) == 0 ||
                memcmp(template_text + begin, "sbbq", 4U) == 0))))
            return false;
        ++lines;
        while (i < template_length && template_text[i] != '\n') {
            if (template_text[i] == '%') {
                if (i + 1U >= template_length ||
                    template_text[i+1U] < '0' ||
                    (size_t)(template_text[i+1U]-'0') >= outputs)
                    return false;
                ++refs;
                i += 2U;
            } else ++i;
        }
    }
    return lines > 0U && refs > 0U;
}

static bool qualified(const MinicC0Program *program,
                      const MinicInlineAsm *src, bool *is_mulacc) {
    static const char *const cpuid_out[4] = {"=a","=b","=c","=d"};
    static const char *const cpuid_in[4] = {"0","1","2","3"};
    static const char *const mul_in[5] = {"0","1","2","a","m"};
    size_t i, n;
    bool generic = false;
    MinicType normalized;
    if (!program || !src || !src->template_text || !src->outputs ||
        !src->inputs || src->is_goto || src->label_count != 0U ||
        !is_mulacc) return false;
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
            *is_mulacc ? minic_type_unsigned_long() : minic_type_unsigned_int();
        if (!expr || !minic_type_unqualified(expr->type, &normalized) ||
            (generic ? (!minic_type_is_integer(normalized) &&
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
    bool is_mulacc = false;
    size_t i, count;
    if (!contract || !qualified(program, source, &is_mulacc)) return false;
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
