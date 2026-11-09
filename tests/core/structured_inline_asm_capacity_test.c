#include "compiler/c_lower/core_lower_asm_internal.h"
#include "compiler/c_lower/core_ir_source_internal.h"

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

typedef enum CoreAsmPoison {
    CORE_ASM_VALID = 0,
    CORE_ASM_BAD_INDEX,
    CORE_ASM_MIXED_OUTPUT_AFTER_INPUT,
    CORE_ASM_EARLY_CLOBBER_INPUT,
    CORE_ASM_READWRITE_INPUT,
    CORE_ASM_UNCLAIMED_INPUT_TIE,
    CORE_ASM_TYPED_FOUR,
    CORE_ASM_TYPED_MISSING_SLOT,
    CORE_ASM_TYPED_UNQUALIFIED_OWNER
} CoreAsmPoison;

static char *core_asm_test_owned(const char *source) {
    const size_t length = strlen(source);
    char *copy = (char *)malloc(length + 1U);
    if (copy != NULL) memcpy(copy, source, length + 1U);
    return copy;
}

static bool verify_core_roles(size_t count, CoreAsmPoison poison, bool expected) {
    MinicCoreSourceBuilder builder;
    MinicCoreFunction *function = &builder.core;
    MinicCoreTypeContext context;
    MinicCoreType type = {0};
    MinicCoreTypeId integer_type, void_type;
    MinicCoreInstruction integer;
    MinicCoreInstruction asm_effect;
    MinicCoreTerminator terminator;
    MinicCoreValueId scalar;
    MinicCoreBlockId block;
    MinicCoreInlineAsmId asm_id;
    size_t index;
    bool initialized;
    bool verified = false;

    minic_core_type_context_initialize(&context);
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = 4U;
    type.alignment = 4U;
    type.value.integer.bit_width = 32U;
    type.value.integer.is_signed = true;
    if (!minic_core_type_register(&context, &type, &integer_type)) {
        minic_core_type_context_destroy(&context);
        return false;
    }
    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_VOID;
    if (!minic_core_type_register(&context, &type, &void_type)) {
        minic_core_type_context_destroy(&context);
        return false;
    }
    minic_core_source_builder_initialize(&builder);
    (void)memset(&integer, 0, sizeof(integer));
    (void)memset(&asm_effect, 0, sizeof(asm_effect));
    (void)memset(&terminator, 0, sizeof(terminator));
    initialized = minic_core_function_set_signature(
                      &builder, "capacity", 8U, minic_type_void(), NULL, 0U) &&
                  minic_core_function_add_block(function, &block) &&
                  minic_core_function_add_opaque_inline_asm(
                      function, "# sixteen slots", 15U, true, false, &asm_id);
    if (initialized) {
        integer.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        integer.value.integer_value = 1;
        initialized = minic_core_function_append_value_instruction(
            function, block, &integer, &scalar);
    }
    if (initialized && poison >= CORE_ASM_TYPED_FOUR) {
        MinicCoreInlineAsm *source = &function->inline_asms[asm_id];
        static const int64_t numeric[3] = {519, 0, 12};
        source->operand_plan_owner = MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED;
        source->target_machine_form_id = 173U;
        source->resolved_immediate_count = 4U;
        source->resolved_immediates[0].kind =
            MINIC_CORE_INLINE_ASM_IMMEDIATE_SYMBOL;
        source->resolved_immediates[0].symbol = core_asm_test_owned(".L.str_0");
        source->resolved_immediates[0].asm_name = core_asm_test_owned("path");
        initialized = source->resolved_immediates[0].symbol != NULL &&
                      source->resolved_immediates[0].asm_name != NULL;
        for (index = 1U; index < 4U; ++index) {
            source->resolved_immediates[index].kind =
                MINIC_CORE_INLINE_ASM_IMMEDIATE_INTEGER;
            source->resolved_immediates[index].integer = numeric[index - 1U];
        }
        if (poison == CORE_ASM_TYPED_MISSING_SLOT)
            source->resolved_immediates[2].kind =
                MINIC_CORE_INLINE_ASM_IMMEDIATE_INVALID;
        if (poison == CORE_ASM_TYPED_UNQUALIFIED_OWNER)
            source->operand_plan_owner = MINIC_ASM_OPERAND_PLAN_GENERIC_STRUCTURED;
    }
    if (initialized) {
        asm_effect.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
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
        if (poison == CORE_ASM_BAD_INDEX)
            asm_effect.value.structured_inline_asm.operands[count - 1U].operand_index = count;
        if (poison == CORE_ASM_MIXED_OUTPUT_AFTER_INPUT)
            asm_effect.value.structured_inline_asm.operands[count - 1U].kind =
                MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT;
        if (poison == CORE_ASM_EARLY_CLOBBER_INPUT)
            asm_effect.value.structured_inline_asm.operands[count - 1U].physical_early_clobber = true;
        if (poison == CORE_ASM_READWRITE_INPUT) {
            function->inline_asms[asm_id].operand_plan_owner =
                MINIC_ASM_OPERAND_PLAN_GENERIC_STRUCTURED;
            asm_effect.value.structured_inline_asm.operands[count - 1U].physical_read_write = true;
        }
        if (poison == CORE_ASM_UNCLAIMED_INPUT_TIE) {
            asm_effect.value.structured_inline_asm.operands[count - 1U].has_tied_output = true;
            asm_effect.value.structured_inline_asm.operands[count - 1U].tied_output_index = 0U;
        }
        terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
        terminator.return_value = MINIC_CORE_VALUE_INVALID;
        terminator.return_object = MINIC_CORE_OBJECT_INVALID;
        initialized = minic_core_function_append_effect_instruction(
                          function, block, &asm_effect) &&
                      minic_core_function_set_terminator(
                          function, block, &terminator);
    }
    if (initialized) {
        initialized = minic_core_function_define_signature(
            function, &context, void_type, NULL, 0U,
            MINIC_CALLING_CONVENTION_DEFAULT, false);
        function->values[0].core_type_id = integer_type;
        function->instructions[0].core_type_id = integer_type;
        function->instructions[1].core_type_id = void_type;
        verified = minic_core_function_attach_type_context(function, &context) &&
                   minic_core_function_verify(function);
    }
    minic_core_source_builder_destroy(&builder);
    minic_core_type_context_destroy(&context);
    return initialized && verified == expected;
}

/* A register output with an SSA definition is not a pointer-backed output.
 * Core must preserve one result, regard its output binding as a definition
 * rather than a use, and reject duplicate/malformed register results. */
static bool verify_core_scalar_result(bool bad_value, bool duplicate_output,
                                      bool missing_result, bool expected) {
    MinicCoreSourceBuilder builder;
    MinicCoreFunction *function = &builder.core;
    MinicCoreTypeContext context;
    MinicCoreType type = {0};
    MinicCoreTypeId integer_type, void_type;
    MinicCoreInstruction integer = {0}, result = {0};
    MinicCoreTerminator terminator = {0};
    MinicCoreValueId input, output;
    MinicCoreBlockId block;
    MinicCoreInlineAsmId asm_id;
    bool ready;
    bool verified = false;

    minic_core_type_context_initialize(&context);
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = 4U;
    type.alignment = 4U;
    type.value.integer.bit_width = 32U;
    type.value.integer.is_signed = true;
    if (!minic_core_type_register(&context, &type, &integer_type)) {
        minic_core_type_context_destroy(&context);
        return false;
    }
    memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_VOID;
    if (!minic_core_type_register(&context, &type, &void_type)) {
        minic_core_type_context_destroy(&context);
        return false;
    }
    minic_core_source_builder_initialize(&builder);
    ready = minic_core_function_set_signature(
                &builder, "ssa_asm", 7U, minic_type_void(), NULL, 0U) &&
            minic_core_function_add_block(function, &block) &&
            minic_core_function_add_opaque_inline_asm(
                function, "addi %0,%1,0", 12U, true, false, &asm_id);
    if (ready) {
        integer.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        integer.value.integer_value = 41;
        ready = minic_core_function_append_value_instruction(
            function, block, &integer, &input);
    }
    if (ready) {
        result.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
        result.value.structured_inline_asm.inline_asm_id = asm_id;
        result.value.structured_inline_asm.operand_count = 2U;
        result.value.structured_inline_asm.operands[0].kind =
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_RESULT;
        result.value.structured_inline_asm.operands[0].operand_index = 0U;
        result.value.structured_inline_asm.operands[0].value =
            bad_value ? input : MINIC_CORE_VALUE_INVALID;
        result.value.structured_inline_asm.operands[1].kind =
            duplicate_output ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_RESULT
                             : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        result.value.structured_inline_asm.operands[1].operand_index = 1U;
        result.value.structured_inline_asm.operands[1].value =
            duplicate_output ? MINIC_CORE_VALUE_INVALID : input;
        ready = minic_core_function_append_value_instruction(
            function, block, &result, &output);
    }
    if (ready) {
        terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
        terminator.return_value = MINIC_CORE_VALUE_INVALID;
        terminator.return_object = MINIC_CORE_OBJECT_INVALID;
        ready = minic_core_function_set_terminator(function, block, &terminator) &&
                minic_core_function_define_signature(
                    function, &context, void_type, NULL, 0U,
                    MINIC_CALLING_CONVENTION_DEFAULT, false);
    }
    if (ready) {
        function->values[input].core_type_id = integer_type;
        function->values[output].core_type_id = integer_type;
        function->instructions[0].core_type_id = integer_type;
        function->instructions[1].core_type_id = integer_type;
        if (missing_result)
            function->instructions[1].result = MINIC_CORE_VALUE_INVALID;
        verified = minic_core_function_attach_type_context(function, &context) &&
                   minic_core_function_verify(function);
    }
    minic_core_source_builder_destroy(&builder);
    minic_core_type_context_destroy(&context);
    return ready && verified == expected;
}

/* A GNU +r output on a global hardware-register binding is not an
 * address-backed C lvalue. Its Core input must be the very same fixed
 * register's read, while the structured ASM owns its physical output. */
static bool verify_fixed_register_readwrite_role(void) {
    MinicCoreSourceBuilder builder;
    MinicCoreFunction *function = &builder.core;
    MinicCoreTypeContext context;
    MinicCoreType type = {0};
    MinicCoreTypeId integer_type, void_type;
    MinicCoreInstruction read = {0}, asm_effect = {0};
    MinicCoreTerminator terminator = {0};
    MinicCoreStructuredInlineAsmOperand *operand;
    MinicCoreValueId input, unrelated;
    MinicCoreBlockId block;
    MinicCoreInlineAsmId asm_id;
    size_t fixed_id, other_id, output_count;
    bool ok, verified;

    minic_core_source_builder_initialize(&builder);
    minic_core_type_context_initialize(&context);
    type.kind = MINIC_CORE_TYPE_INTEGER;
    type.has_layout = true;
    type.size = 8U;
    type.alignment = 8U;
    type.value.integer.bit_width = 64U;
    type.value.integer.is_signed = false;
    ok = minic_core_type_register(&context, &type, &integer_type);
    memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_VOID;
    ok = ok && minic_core_type_register(&context, &type, &void_type) &&
         minic_core_function_set_signature(&builder, "fixed_rw", 8U,
                                            minic_type_void(), NULL, 0U) &&
         minic_core_function_add_fixed_register_binding(&builder, "rsp", 3U,
             minic_type_unsigned_long(), false, &fixed_id) &&
         minic_core_function_add_fixed_register_binding(&builder, "r12", 3U,
             minic_type_unsigned_long(), false, &other_id) &&
         minic_core_function_add_block(function, &block) &&
         minic_core_function_add_opaque_inline_asm(
             function, "mov %0,%0", 9U, true, false, &asm_id);
    if (ok) {
        read.kind = MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ;
        read.value.fixed_register_binding_id = fixed_id;
        ok = minic_core_function_append_value_instruction(
            function, block, &read, &input);
    }
    if (ok) {
        read.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        read.value.integer_value = 1;
        ok = minic_core_function_append_value_instruction(
            function, block, &read, &unrelated);
    }
    if (ok) {
        asm_effect.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
        asm_effect.value.structured_inline_asm.inline_asm_id = asm_id;
        asm_effect.value.structured_inline_asm.operand_count = 1U;
        operand = &asm_effect.value.structured_inline_asm.operands[0];
        operand->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_FIXED_REGISTER_READWRITE;
        operand->operand_index = 0U;
        operand->value = input;
        operand->has_fixed_register_binding = true;
        operand->fixed_register_binding_id = fixed_id;
        operand->physical_read_write = true;
        function->inline_asms[asm_id].operand_plan_owner =
            MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED;
        function->inline_asms[asm_id].target_machine_form_id = 1U;
        terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
        terminator.return_value = MINIC_CORE_VALUE_INVALID;
        terminator.return_object = MINIC_CORE_OBJECT_INVALID;
        ok = minic_core_function_append_effect_instruction(
                function, block, &asm_effect) &&
             minic_core_function_set_terminator(function, block, &terminator) &&
             minic_core_function_define_signature(function, &context, void_type,
                NULL, 0U, MINIC_CALLING_CONVENTION_DEFAULT, false);
    }
    if (ok) {
        function->values[input].core_type_id = integer_type;
        function->values[unrelated].core_type_id = integer_type;
        function->instructions[0].core_type_id = integer_type;
        function->instructions[1].core_type_id = integer_type;
        function->instructions[2].core_type_id = void_type;
        function->fixed_register_bindings[fixed_id].core_type_id = integer_type;
        function->fixed_register_bindings[other_id].core_type_id = integer_type;
        ok = minic_core_function_attach_type_context(function, &context);
    }
    if (ok) {
        operand = &function->instructions[2].value.structured_inline_asm.operands[0];
        verified = minic_core_function_verify(function);
        ok = verified &&
             minic_core_asm_output_operand_count(&function->instructions[2],
                                                 &output_count) &&
             output_count == 1U;
    }
    if (ok) {
        operand->physical_read_write = false;
        ok = !minic_core_function_verify(function);
        operand->physical_read_write = true;
    }
    if (ok) {
        operand->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
        ok = !minic_core_function_verify(function);
        operand->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_FIXED_REGISTER_READWRITE;
    }
    if (ok) {
        operand->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        ok = !minic_core_function_verify(function);
        operand->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_FIXED_REGISTER_READWRITE;
    }
    if (ok) {
        operand->has_fixed_register_binding = false;
        ok = !minic_core_function_verify(function);
        operand->has_fixed_register_binding = true;
    }
    if (ok) {
        operand->fixed_register_binding_id = other_id;
        ok = !minic_core_function_verify(function);
        operand->fixed_register_binding_id = fixed_id;
    }
    if (ok) {
        operand->value = unrelated;
        ok = !minic_core_function_verify(function);
        operand->value = input;
    }
    if (ok) {
        function->fixed_register_bindings[fixed_id].is_local = true;
        ok = !minic_core_function_verify(function);
        function->fixed_register_bindings[fixed_id].is_local = false;
    }
    if (ok) {
        operand->physical_early_clobber = true;
        ok = !minic_core_function_verify(function);
        operand->physical_early_clobber = false;
    }
    if (ok) {
        function->inline_asms[asm_id].operand_plan_owner =
            MINIC_ASM_OPERAND_PLAN_GENERIC_STRUCTURED;
        ok = !minic_core_function_verify(function);
        function->inline_asms[asm_id].operand_plan_owner =
            MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED;
    }
    if (ok) ok = minic_core_function_verify(function);
    minic_core_source_builder_destroy(&builder);
    minic_core_type_context_destroy(&context);
    return ok;
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
    if (!verify_core_roles(13U, CORE_ASM_VALID, true) ||
        !verify_core_roles(16U, CORE_ASM_VALID, true) ||
        !verify_core_roles(16U, CORE_ASM_BAD_INDEX, false) ||
        !verify_core_roles(17U, CORE_ASM_VALID, false) ||
        !verify_core_roles(13U, CORE_ASM_MIXED_OUTPUT_AFTER_INPUT, false) ||
        !verify_core_roles(13U, CORE_ASM_EARLY_CLOBBER_INPUT, false) ||
        !verify_core_roles(13U, CORE_ASM_READWRITE_INPUT, false) ||
        !verify_core_roles(13U, CORE_ASM_UNCLAIMED_INPUT_TIE, false)) {
        (void)fprintf(stderr, "FAIL 13-16 operand Core verifier boundary\n");
        return 5;
    }
    if (!verify_core_roles(4U, CORE_ASM_TYPED_FOUR, true) ||
        !verify_core_roles(4U, CORE_ASM_TYPED_MISSING_SLOT, false) ||
        !verify_core_roles(4U, CORE_ASM_TYPED_UNQUALIFIED_OWNER, false)) {
        (void)fprintf(stderr,
            "FAIL four GNU typed source-immediate witnesses / forged Core contract\n");
        return 8;
    }
    if (!verify_core_scalar_result(false, false, false, true) ||
        !verify_core_scalar_result(true, false, false, false) ||
        !verify_core_scalar_result(false, true, false, false) ||
        !verify_core_scalar_result(false, false, true, false)) {
        (void)fprintf(stderr, "FAIL SSA register-result Core verifier boundary\n");
        return 6;
    }
    if (!verify_fixed_register_readwrite_role()) {
        (void)fprintf(stderr,
            "FAIL fixed hardware-register GNU +r input/output Core contract\n");
        return 7;
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

    (void)puts("PASS Core GNU structured asm 16 operands + %10-%15 + named + modifier + goto + 4 typed immediates + over-cap fail-closed");
    return 0;
}
