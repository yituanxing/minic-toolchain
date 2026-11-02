#include "target/aarch64/neon_emit.h"

#include <stdio.h>
#include <string.h>

bool minic_aarch64_neon_vector128_type(MinicType type) {
    MinicType canonical;
    switch (type.base_kind) {
    case MINIC_TYPE_BASE_AARCH64_UINT64X2:
    case MINIC_TYPE_BASE_AARCH64_UINT8X16:
    case MINIC_TYPE_BASE_AARCH64_INT8X16:
    case MINIC_TYPE_BASE_AARCH64_POLY8X16:
        canonical = minic_type_aarch64_fixed_vector128(type.base_kind);
        return type.pointer_depth == 0U &&
               type.base_qualifiers == MINIC_TYPE_QUALIFIER_NONE &&
               type.explicit_alignment == 0U &&
               minic_type_equal(type, canonical);
    default:
        return false;
    }
}

bool minic_aarch64_neon_vector128_core_type(
    const MinicCoreTypeContext *types, MinicCoreTypeId id) {
    const MinicCoreType *vector = minic_core_type_get(types, id);
    const MinicCoreType *element;
    if (vector == NULL || vector->kind != MINIC_CORE_TYPE_VECTOR ||
        !vector->value.vector.is_builtin || !vector->has_layout ||
        vector->size != 16U || vector->alignment != 16U)
        return false;
    element = minic_core_type_get(types,
        minic_core_type_unqualified(types, vector->value.vector.element));
    if (element == NULL || element->kind != MINIC_CORE_TYPE_INTEGER ||
        !element->has_layout || element->value.integer.is_bool) return false;
    return (element->value.integer.bit_width == 64U &&
            !element->value.integer.is_signed && element->size == 8U &&
            vector->value.vector.element_count == 2U) ||
           (element->value.integer.bit_width == 8U && element->size == 1U &&
            vector->value.vector.element_count == 16U);
}

static bool neon_emit_load_machine(FILE *out, const MinicA64FrameView *frame,
                                   size_t pointer_slot, size_t result_slot) {
    return out != NULL && frame != NULL &&
           minic_a64_emit_owned_slot(out, frame, false, MINIC_A64_REG_X,
                                     10U, pointer_slot) &&
           fputs("  ldr q16, [x10]\n", out) != EOF &&
           minic_a64_emit_owned_slot(out, frame, true, MINIC_A64_REG_Q,
                                     16U, result_slot);
}

bool minic_aarch64_neon_emit_load(FILE *out, const MinicA64FrameView *frame,
    MinicType type, size_t pointer_slot, size_t result_slot) {
    return minic_aarch64_neon_vector128_type(type) &&
           neon_emit_load_machine(out, frame, pointer_slot, result_slot);
}

bool minic_aarch64_neon_emit_load_core(FILE *out, const MinicA64FrameView *frame,
    const MinicCoreTypeContext *types, MinicCoreTypeId id,
    size_t pointer_slot, size_t result_slot) {
    return minic_aarch64_neon_vector128_core_type(types, id) &&
           neon_emit_load_machine(out, frame, pointer_slot, result_slot);
}

static bool neon_emit_store_machine(FILE *out, const MinicA64FrameView *frame,
                                    size_t pointer_slot, size_t value_slot) {
    return out != NULL && frame != NULL &&
           minic_a64_emit_owned_slot(out, frame, false, MINIC_A64_REG_X,
                                     10U, pointer_slot) &&
           minic_a64_emit_owned_slot(out, frame, false, MINIC_A64_REG_Q,
                                     16U, value_slot) &&
           fputs("  str q16, [x10]\n", out) != EOF;
}

bool minic_aarch64_neon_emit_store(FILE *out, const MinicA64FrameView *frame,
    MinicType type, size_t pointer_slot, size_t value_slot) {
    return minic_aarch64_neon_vector128_type(type) &&
           neon_emit_store_machine(out, frame, pointer_slot, value_slot);
}

bool minic_aarch64_neon_emit_store_core(FILE *out, const MinicA64FrameView *frame,
    const MinicCoreTypeContext *types, MinicCoreTypeId id,
    size_t pointer_slot, size_t value_slot) {
    return minic_aarch64_neon_vector128_core_type(types, id) &&
           neon_emit_store_machine(out, frame, pointer_slot, value_slot);
}

static bool neon_emit_xor_machine(FILE *out, const MinicA64FrameView *frame,
    size_t left_slot, size_t right_slot, size_t result_slot) {
    return out != NULL && frame != NULL &&
        minic_a64_emit_owned_slot(out, frame, false, MINIC_A64_REG_Q,
                                  16U, left_slot) &&
        minic_a64_emit_owned_slot(out, frame, false, MINIC_A64_REG_Q,
                                  17U, right_slot) &&
        fputs("  eor v16.16b, v16.16b, v17.16b\n", out) != EOF &&
        minic_a64_emit_owned_slot(out, frame, true, MINIC_A64_REG_Q,
                                  16U, result_slot);
}

bool minic_aarch64_neon_emit_xor(FILE *out, const MinicA64FrameView *frame,
    MinicType type, size_t left_slot, size_t right_slot, size_t result_slot) {
    /* v16-v18 are caller-clobbered, unlike low-half preserved v8-v15. */
    return minic_aarch64_neon_vector128_type(type) &&
           neon_emit_xor_machine(out, frame, left_slot, right_slot, result_slot);
}

bool minic_aarch64_neon_emit_xor_core(FILE *out, const MinicA64FrameView *frame,
    const MinicCoreTypeContext *types, MinicCoreTypeId id,
    size_t left_slot, size_t right_slot, size_t result_slot) {
    return minic_aarch64_neon_vector128_core_type(types, id) &&
           neon_emit_xor_machine(out, frame, left_slot, right_slot, result_slot);
}

static bool neon_exact_constraint(const MinicInlineAsmOperand *operand,
                                  const char *name) {
    const size_t length = strlen(name);
    return operand != NULL && operand->constraint_text != NULL &&
           operand->constraint_length == length &&
           memcmp(operand->constraint_text, name, length) == 0;
}

bool minic_aarch64_neon_sha3_eor3_source_form(
    const MinicC0Program *program, const MinicInlineAsm *source) {
    static const char exact_template[] =
        ".arch_extension sha3\n"
        "eor3 %0.16b, %1.16b, %2.16b, %3.16b";
    const MinicType expected = minic_type_aarch64_fixed_vector128(
        MINIC_TYPE_BASE_AARCH64_UINT64X2);
    size_t i;
    if (program == NULL || program->expressions == NULL ||
        source == NULL || source->template_text == NULL ||
        source->template_length != sizeof(exact_template) - 1U ||
        memcmp(source->template_text, exact_template,
               sizeof(exact_template) - 1U) != 0 ||
        source->outputs == NULL || source->inputs == NULL ||
        source->output_count != 1U || source->input_count != 3U ||
        source->is_goto || source->is_volatile ||
        source->has_memory_clobber || source->clobber_count != 0U ||
        source->register_clobber_count != 0U ||
        source->label_count != 0U ||
        !neon_exact_constraint(&source->outputs[0], "=w") ||
        source->outputs[0].access != MINIC_INLINE_ASM_OPERAND_WRITE_ONLY ||
        source->outputs[0].expression >= program->expression_count ||
        program->expressions[source->outputs[0].expression].value_category !=
            MINIC_VALUE_LVALUE ||
        !minic_type_equal(
            program->expressions[source->outputs[0].expression].type, expected))
        return false;
    for (i = 0U; i < 3U; ++i) {
        const MinicInlineAsmOperand *input = &source->inputs[i];
        if (!neon_exact_constraint(input, "w") ||
            input->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY ||
            input->expression >= program->expression_count ||
            !minic_type_equal(program->expressions[input->expression].type,
                              expected))
            return false;
    }
    return true;
}

static bool neon_emit_eor3_machine(FILE *out, const MinicA64FrameView *frame,
    bool qualified_sha3_asm,
    size_t first_slot, size_t second_slot, size_t third_slot,
    size_t result_slot) {
    /* `eor3` is the exact Linux xor-neon SHA3 alternate operation. Do not
     * accidentally admit unsigned byte or polynomial intrinsics as this
     * particular GCC `uint64x2_t` inline-ASM contract. */
    if (out == NULL || frame == NULL || !qualified_sha3_asm ||
        !minic_a64_emit_owned_slot(out, frame, false, MINIC_A64_REG_Q,
                                  16U, first_slot) ||
        !minic_a64_emit_owned_slot(out, frame, false, MINIC_A64_REG_Q,
                                  17U, second_slot) ||
        !minic_a64_emit_owned_slot(out, frame, false, MINIC_A64_REG_Q,
                                  18U, third_slot) ||
        fputs("  .arch_extension sha3\n"
              "  eor3 v16.16b, v16.16b, v17.16b, v18.16b\n", out) == EOF ||
        !minic_a64_emit_owned_slot(out, frame, true, MINIC_A64_REG_Q,
                                  16U, result_slot))
        return false;
    return true;
}

bool minic_aarch64_neon_emit_eor3(FILE *out, const MinicA64FrameView *frame,
    MinicType type, bool qualified_sha3_asm,
    size_t first_slot, size_t second_slot, size_t third_slot,
    size_t result_slot) {
    return type.base_kind == MINIC_TYPE_BASE_AARCH64_UINT64X2 &&
           minic_aarch64_neon_vector128_type(type) &&
           neon_emit_eor3_machine(out, frame, qualified_sha3_asm,
                first_slot, second_slot, third_slot, result_slot);
}

bool minic_aarch64_neon_emit_eor3_core(FILE *out, const MinicA64FrameView *frame,
    const MinicCoreTypeContext *types, MinicCoreTypeId id,
    bool qualified_sha3_asm,
    size_t first_slot, size_t second_slot, size_t third_slot,
    size_t result_slot) {
    const MinicCoreType *vector = minic_core_type_get(types, id);
    const MinicCoreType *element;
    if (!minic_aarch64_neon_vector128_core_type(types, id) ||
        vector == NULL || vector->value.vector.element_count != 2U)
        return false;
    element = minic_core_type_get(types,
        minic_core_type_unqualified(types, vector->value.vector.element));
    return element != NULL && element->kind == MINIC_CORE_TYPE_INTEGER &&
           element->value.integer.bit_width == 64U &&
           !element->value.integer.is_signed &&
           neon_emit_eor3_machine(out, frame, qualified_sha3_asm,
                first_slot, second_slot, third_slot, result_slot);
}
