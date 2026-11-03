#include "target/aarch64/neon_emit.h"

#include <stdio.h>

bool minic_aarch64_neon_vector128_core_type(
    const MinicCoreTypeContext *types, MinicCoreTypeId id) {
    const MinicCoreType *vector = minic_core_type_get(
        types, minic_core_type_unqualified(types, id));
    const MinicCoreType *element;
    if (vector == NULL || vector->kind != MINIC_CORE_TYPE_VECTOR ||
        vector->value.vector.representation !=
            MINIC_CORE_VECTOR_REGISTER_VALUE || !vector->has_layout ||
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

bool minic_aarch64_neon_emit_xor_core(FILE *out, const MinicA64FrameView *frame,
    const MinicCoreTypeContext *types, MinicCoreTypeId id,
    size_t left_slot, size_t right_slot, size_t result_slot) {
    /* v16-v18 are caller-clobbered, unlike low-half preserved v8-v15. */
    return minic_aarch64_neon_vector128_core_type(types, id) &&
           neon_emit_xor_machine(out, frame, left_slot, right_slot, result_slot);
}
