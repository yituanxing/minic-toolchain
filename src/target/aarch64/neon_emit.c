#include "target/aarch64/neon_emit.h"

/* Register vectors are typed lanes, never an untyped 64/128-bit integer.
 * In particular, FP lanes admit only the architected binary32/binary64
 * AdvSIMD representations; half/bfloat/scalable declarations stay opaque. */
static bool aarch64_neon_element_lane_bytes(const MinicCoreType *element) {
    if (element == NULL || !element->has_layout) return false;
    if (element->kind == MINIC_CORE_TYPE_INTEGER)
        return !element->value.integer.is_bool &&
            (element->size == 1U || element->size == 2U ||
             element->size == 4U || element->size == 8U) &&
            element->value.integer.bit_width == element->size * 8U;
    if (element->kind == MINIC_CORE_TYPE_FLOAT)
        return (element->size == 4U &&
                element->value.floating.format == MINIC_CORE_FLOAT_BINARY32) ||
            (element->size == 8U &&
             element->value.floating.format == MINIC_CORE_FLOAT_BINARY64);
    return false;
}

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
    if (!aarch64_neon_element_lane_bytes(element)) return false;
    return vector->value.vector.element_count == 16U / element->size;
}

bool minic_aarch64_neon_register_vector_core_type(
    const MinicCoreTypeContext *types, MinicCoreTypeId id, size_t *bytes) {
    const MinicCoreType *vector;
    const MinicCoreType *element;
    if (bytes == NULL || types == NULL) return false;
    if (minic_aarch64_neon_vector128_core_type(types, id)) {
        *bytes = 16U;
        return true;
    }
    vector = minic_core_type_get(types, minic_core_type_unqualified(types, id));
    if (vector == NULL || vector->kind != MINIC_CORE_TYPE_VECTOR ||
        vector->value.vector.representation != MINIC_CORE_VECTOR_REGISTER_VALUE ||
        !vector->has_layout || vector->size != 8U || vector->alignment != 8U)
        return false;
    element = minic_core_type_get(types,
        minic_core_type_unqualified(types, vector->value.vector.element));
    if (!aarch64_neon_element_lane_bytes(element) ||
        vector->value.vector.element_count != 8U / element->size) return false;
    *bytes = 8U;
    return true;
}
