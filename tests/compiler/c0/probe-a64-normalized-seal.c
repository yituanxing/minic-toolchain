/* Host unit probe of A64's Source-qualified -> normalized-machine seal.
 * Link with the ordinary MiniC object set (without tools/minic-cc/main.o).
 * This test does not need an A64 cross-assembler or QEMU. */
#include "target/aarch64/asm_contract.h"
#include "target/aarch64/asm_contract_source.h"
#include "frontend/ast.h"
#include "frontend/type.h"

#include <stdio.h>
#include <string.h>

typedef struct ProbeCase {
    MinicC0Program program;
    MinicExpression expr[2];
    MinicInlineAsm src;
    MinicInlineAsmOperand output;
    MinicInlineAsmOperand input[2];
    MinicTargetInlineAsmContract contract;
} ProbeCase;

static void set_operand(MinicInlineAsmOperand *op, const char *constraint,
                        MinicExpressionId expression,
                        MinicInlineAsmOperandAccess access) {
    memset(op, 0, sizeof(*op));
    op->constraint_text = (char *)constraint;
    op->constraint_length = strlen(constraint);
    op->expression = expression;
    op->access = access;
}

static bool initialize_case(ProbeCase *test, const char *source,
                            MinicType first, MinicType second,
                            bool acquire, bool raw) {
    memset(test, 0, sizeof(*test));
    test->program.expressions = test->expr;
    test->program.expression_count = 2U;
    test->expr[0].type = first;
    test->expr[0].value_category = MINIC_VALUE_LVALUE;
    test->expr[1].type = second;
    test->expr[1].value_category = acquire && !raw
        ? MINIC_VALUE_LVALUE : MINIC_VALUE_RVALUE;
    test->src.template_text = (char *)source;
    test->src.template_length = strlen(source);
    test->src.is_volatile = true;
    test->src.outputs = &test->output;
    test->src.output_count = 1U;
    test->src.inputs = test->input;
    test->src.input_count = 1U;
    test->src.has_memory_clobber = !raw;
    test->src.clobber_count = raw ? 0U : 1U;
    if (acquire) {
        set_operand(&test->output, "=r", 0U,
                    MINIC_INLINE_ASM_OPERAND_WRITE_ONLY);
        set_operand(&test->input[0], raw ? "r" : "Q", 1U,
                    MINIC_INLINE_ASM_OPERAND_READ_ONLY);
    } else if (!raw) {
        set_operand(&test->output, "=Q", 0U,
                    MINIC_INLINE_ASM_OPERAND_WRITE_ONLY);
        set_operand(&test->input[0], "rZ", 1U,
                    MINIC_INLINE_ASM_OPERAND_READ_ONLY);
    } else {
        test->src.output_count = 0U;
        test->src.input_count = 2U;
        set_operand(&test->input[0], "rZ", 0U,
                    MINIC_INLINE_ASM_OPERAND_READ_ONLY);
        set_operand(&test->input[1], "r", 1U,
                    MINIC_INLINE_ASM_OPERAND_READ_ONLY);
        test->expr[0].value_category = MINIC_VALUE_RVALUE;
    }
    return minic_aarch64_qualify_inline_asm(
        &test->program, &test->src, &test->contract);
}

static bool check_normalization(ProbeCase *test, const char *machine,
                                uint32_t form, uint32_t width) {
    MinicTargetInlineAsmContract candidate = test->contract;
    if (!minic_aarch64_seal_normalized_inline_asm(
            &test->program, &test->src, machine, strlen(machine),
            &candidate) ||
        candidate.target_machine_form_id != form ||
        candidate.target_machine_form_variant != width ||
        candidate.operand_count != test->contract.operand_count ||
        memcmp(candidate.kinds, test->contract.kinds,
               sizeof(candidate.kinds)) != 0 ||
        candidate.implicit_memory_effect !=
            test->contract.implicit_memory_effect)
        return false;
    return true;
}

static bool check_reject(ProbeCase *test, const char *wrong) {
    MinicTargetInlineAsmContract candidate = test->contract;
    return !minic_aarch64_seal_normalized_inline_asm(
        &test->program, &test->src, wrong, strlen(wrong), &candidate);
}

int main(void) {
    static const char *const acquire[] = {
        "ldarb %w0, %1", "ldarh %w0, %1",
        "ldar %w0, %1", "ldar %0, %1"
    };
    static const char *const release[] = {
        "stlrb %w1, %0", "stlrh %w1, %0",
        "stlr %w1, %0", "stlr %x1, %0"
    };
    static const char *const raw_store[] = {
        "strb %w0, [%1]", "strh %w0, [%1]",
        "str %w0, [%1]", "str %x0, [%1]"
    };
    static const char *const raw_read_old[] = {
        "ldrb%w0,[%1]", "ldrh%w0,[%1]",
        "ldr%w0,[%1]", "ldr%0,[%1]"
    };
    static const char *const raw_read_new[] = {
        "ldarb%w0,[%1]", "ldarh%w0,[%1]",
        "ldar%w0,[%1]", "ldar%0,[%1]"
    };
    static const char middle[] =
        "662:.pushsection.altinstructions,\"a\""
        ".word661b-..word663f-..hword91"
        ".byte662b-661b.byte664f-663f"
        ".popsection.subsection1663:";
    static const char tail[] =
        "664:.org.-(664b-663b)+(662b-661b)"
        ".org.-(662b-661b)+(664b-663b).previous.endif";
    const MinicType widths[] = {
        minic_type_unsigned_char(), minic_type_unsigned_short(),
        minic_type_unsigned_int(), minic_type_unsigned_long_long()
    };
    MinicType address;
    size_t i;
    ProbeCase test;
    char raw_read[512];
    if (!minic_type_pointer_to(minic_type_unsigned_char(), &address))
        return 1;
    for (i = 0U; i < 4U; ++i) {
        const uint32_t width = (uint32_t)(1U << i);
        if (!initialize_case(&test, acquire[i], widths[i], widths[i],
                             true, false) ||
            !check_normalization(&test, acquire[i], 1U, width) ||
            !check_reject(&test, release[i]) ||
            !check_reject(&test, "ldar %w1, %0") ||
            !check_reject(&test, "ldar %w0, %1; dmb ish"))
            return (int)(10U + i);
        if (!initialize_case(&test, release[i], widths[i], widths[i],
                             false, false) ||
            !check_normalization(&test, release[i], 2U, width) ||
            !check_reject(&test, acquire[i]) ||
            !check_reject(&test, "stlr %w0, %1"))
            return (int)(20U + i);
        if (!initialize_case(&test, raw_store[i], widths[i], address,
                             false, true) ||
            !check_normalization(&test, raw_store[i], 4U, width) ||
            !check_reject(&test, "str %w1, [%0]") ||
            !check_reject(&test, "str %w0, [%1]; dmb ish"))
            return (int)(30U + i);
        if (snprintf(raw_read, sizeof(raw_read),
                ".if1==1661:%s%s%s%s", raw_read_old[i], middle,
                raw_read_new[i], tail) < 0 ||
            !initialize_case(&test, raw_read, widths[i], address,
                             true, true) ||
            !check_normalization(&test, raw_read, 3U, width) ||
            !check_reject(&test, "ldr %w0, [%1]"))
            return (int)(40U + i);
    }
    puts("PASS A64 LDAR/STLR/MMIO source-normalized seal widths=4 families=4 negatives=28");
    return 0;
}
