#include "target/aarch64/neon_emit.h"


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
