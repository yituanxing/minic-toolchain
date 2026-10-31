#include "core/core_lower_asm_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Exercise Core's target-independent GNU operand normalization directly.
 * Machine-specific register assignment is separately validated by targets. */
static bool normalized_is(MinicInlineAsm *source,
                          const char *raw, const char *expected) {
    char *normalized = NULL;
    size_t normalized_length = 0U;
    bool ok;

    source->template_text = (char *)raw; /* the normalizer reads but never edits */
    source->template_length = strlen(raw);
    if (!core_inline_asm_numeric_template(source, &normalized, &normalized_length))
        return false;
    ok = strlen(expected) == normalized_length &&
         memcmp(normalized, expected, normalized_length) == 0;
    free(normalized);
    return ok;
}

static bool must_reject(MinicInlineAsm *source, const char *raw) {
    char *normalized = NULL;
    size_t normalized_length = 0U;
    bool accepted;

    source->template_text = (char *)raw; /* the normalizer reads but never edits */
    source->template_length = strlen(raw);
    accepted = core_inline_asm_numeric_template(source, &normalized,
                                                &normalized_length);
    free(normalized);
    return !accepted;
}

static bool verify_core_roles(size_t count, bool corrupt_index, bool expected) {
    MinicCoreFunction function;
    MinicCoreInstruction integer;
    MinicCoreInstruction asm_effect;
    MinicCoreTerminator terminator;
    MinicCoreValueId scalar;
    MinicCoreBlockId block;
    MinicCoreInlineAsmId asm_id;
    size_t index;
    bool initialized;
    bool verified = false;

    minic_core_function_initialize(&function);
    (void)memset(&integer, 0, sizeof(integer));
    (void)memset(&asm_effect, 0, sizeof(asm_effect));
    (void)memset(&terminator, 0, sizeof(terminator));
    initialized = minic_core_function_set_signature(
                      &function, "capacity", 8U, minic_type_void(), NULL, 0U) &&
                  minic_core_function_add_block(&function, &block) &&
                  minic_core_function_add_opaque_inline_asm(
                      &function, "# sixteen slots", 15U, true, false, &asm_id);
    if (initialized) {
        integer.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        integer.type = minic_type_int();
        integer.value.integer_value = 1;
        initialized = minic_core_function_append_value_instruction(
            &function, block, &integer, &scalar);
    }
    if (initialized) {
        asm_effect.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
        asm_effect.type = minic_type_void();
        asm_effect.value.structured_inline_asm.inline_asm_id = asm_id;
        asm_effect.value.structured_inline_asm.operand_count = count;
        for (index = 0U;
             index < count && index < MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT;
             ++index) {
            MinicCoreStructuredInlineAsmOperand *operand =
                &asm_effect.value.structured_inline_asm.operands[index];
            operand->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
            operand->operand_index = index;
            operand->value = scalar;
        }
        if (corrupt_index)
            asm_effect.value.structured_inline_asm.operands[count - 1U].operand_index = count;
        terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
        terminator.return_value = MINIC_CORE_VALUE_INVALID;
        terminator.return_object = MINIC_CORE_OBJECT_INVALID;
        initialized = minic_core_function_append_effect_instruction(
                          &function, block, &asm_effect) &&
                      minic_core_function_set_terminator(
                          &function, block, &terminator);
    }
    if (initialized)
        verified = minic_core_function_verify(&function);
    minic_core_function_destroy(&function);
    return initialized && verified == expected;
}

int main(void) {
    MinicInlineAsm source;
    MinicInlineAsmOperand outputs[4];
    MinicInlineAsmOperand inputs[12];
    MinicInlineAsmLabel label;

    (void)memset(&source, 0, sizeof(source));
    (void)memset(outputs, 0, sizeof(outputs));
    (void)memset(inputs, 0, sizeof(inputs));
    (void)memset(&label, 0, sizeof(label));
    source.outputs = outputs;
    source.output_count = 4U;
    source.inputs = inputs;
    source.input_count = 12U;
    inputs[6].name = "ten";
    inputs[6].name_length = 3U;
    inputs[11].name = "last";
    inputs[11].name_length = 4U;

    if (MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT != 16U ||
        !normalized_is(&source, "add %10,%0\n# %15 %[last] %z[ten] %%",
                       "add %10,%0\n# %15 %15 %z10 %%") ||
        !normalized_is(&source, "# %0 %1 %2 %3 %4 %5 %6 %7 %8 %9 %10 %11 %12 %13 %14 %15",
                       "# %0 %1 %2 %3 %4 %5 %6 %7 %8 %9 %10 %11 %12 %13 %14 %15") ||
        !must_reject(&source, "# %16") ||
        !must_reject(&source, "# %[missing]") ||
        !must_reject(&source, "# %999999999999999999999999")) {
        (void)fprintf(stderr, "FAIL 16-operand Core normalization\n");
        return 1;
    }
    if (!verify_core_roles(13U, false, true) ||
        !verify_core_roles(16U, false, true) ||
        !verify_core_roles(16U, true, false) ||
        !verify_core_roles(17U, false, false)) {
        (void)fprintf(stderr, "FAIL 13-16 operand Core verifier boundary\n");
        return 5;
    }

    /* A GNU goto label uses the operand slot immediately after all data
     * operands. The sole extra slot must not admit a data operand. */
    source.is_goto = true;
    source.label_count = 1U;
    label.name = "destination";
    label.name_length = 11U;
    source.labels = &label;
    if (!normalized_is(&source, "b %l[destination]", "b %l16") ||
        !normalized_is(&source, "b %l16", "b %l16") ||
        !must_reject(&source, "b %16")) {
        (void)fprintf(stderr, "FAIL 16-operand GNU goto slot\n");
        return 2;
    }

    source.is_goto = false;
    source.label_count = 0U;
    source.output_count = 4U;
    source.input_count = 13U;
    if (!must_reject(&source, "# %15")) {
        (void)fprintf(stderr, "FAIL over-capacity 4+13 fail-closed\n");
        return 3;
    }
    source.output_count = 17U;
    source.input_count = 0U;
    if (!must_reject(&source, "# no operands")) {
        (void)fprintf(stderr, "FAIL over-capacity outputs fail-closed\n");
        return 4;
    }

    (void)puts("PASS Core GNU structured asm 16 operands + %10-%15 + named + modifier + goto + over-cap fail-closed");
    return 0;
}
