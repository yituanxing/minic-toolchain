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

bool minic_aarch64_neon_emit_load(FILE *out, const MinicA64FrameView *frame,
    MinicType type, size_t pointer_slot, size_t result_slot) {
    if (out == NULL || frame == NULL ||
        !minic_aarch64_neon_vector128_type(type) ||
        !minic_a64_emit_owned_slot(out, frame, false, MINIC_A64_REG_X,
                                  10U, pointer_slot) ||
        fputs("  ldr q16, [x10]\n", out) == EOF ||
        !minic_a64_emit_owned_slot(out, frame, true, MINIC_A64_REG_Q,
                                  16U, result_slot))
        return false;
    return true;
}

bool minic_aarch64_neon_emit_store(FILE *out, const MinicA64FrameView *frame,
    MinicType type, size_t pointer_slot, size_t value_slot) {
    if (out == NULL || frame == NULL ||
        !minic_aarch64_neon_vector128_type(type) ||
        !minic_a64_emit_owned_slot(out, frame, false, MINIC_A64_REG_X,
                                  10U, pointer_slot) ||
        !minic_a64_emit_owned_slot(out, frame, false, MINIC_A64_REG_Q,
                                  16U, value_slot) ||
        fputs("  str q16, [x10]\n", out) == EOF)
        return false;
    return true;
}

bool minic_aarch64_neon_emit_xor(FILE *out, const MinicA64FrameView *frame,
    MinicType type, size_t left_slot, size_t right_slot, size_t result_slot) {
    if (out == NULL || frame == NULL ||
        !minic_aarch64_neon_vector128_type(type) ||
        !minic_a64_emit_owned_slot(out, frame, false, MINIC_A64_REG_Q,
                                  16U, left_slot) ||
        !minic_a64_emit_owned_slot(out, frame, false, MINIC_A64_REG_Q,
                                  17U, right_slot) ||
        /* AAPCS64 preserves the low 64 bits of v8-v15. v16-v18 are
         * caller-clobbered, so our temporary Q values do not corrupt ABI
         * callee-saved vector state in an otherwise scalar C function. */
        fputs("  eor v16.16b, v16.16b, v17.16b\n", out) == EOF ||
        !minic_a64_emit_owned_slot(out, frame, true, MINIC_A64_REG_Q,
                                  16U, result_slot))
        return false;
    return true;
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

bool minic_aarch64_neon_emit_eor3(FILE *out, const MinicA64FrameView *frame,
    MinicType type, bool qualified_sha3_asm,
    size_t first_slot, size_t second_slot, size_t third_slot,
    size_t result_slot) {
    /* `eor3` is the exact Linux xor-neon SHA3 alternate operation. Do not
     * accidentally admit unsigned byte or polynomial intrinsics as this
     * particular GCC `uint64x2_t` inline-ASM contract. */
    if (out == NULL || frame == NULL ||
        type.base_kind != MINIC_TYPE_BASE_AARCH64_UINT64X2 ||
        !minic_aarch64_neon_vector128_type(type) ||
        !qualified_sha3_asm ||
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
