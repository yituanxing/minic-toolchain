#include "target/aarch64/abi.h"
#include "target/common/lp64_aggregate.h"

/* AAPCS64 HFA/HVA classification over Core's resolved ABI shape. Once the
 * Core type table is attached there is deliberately no C descriptor or
 * DataLayout fallback: unknown vector, incomplete record and recursive
 * type shapes are rejected conservatively. */
static bool aarch64_special_core_aggregate_member(
    const MinicCoreTypeContext *types, MinicCoreTypeId id, unsigned int depth) {
    const MinicCoreType *type;
    size_t i;
    if (depth > 16U || types == NULL) return true;
    id = minic_core_type_unqualified(types, id);
    type = minic_core_type_get(types, id);
    if (type == NULL) return true;
    switch (type->kind) {
    case MINIC_CORE_TYPE_POINTER:
    case MINIC_CORE_TYPE_INTEGER:
        return false;
    case MINIC_CORE_TYPE_ARRAY:
        return aarch64_special_core_aggregate_member(
                   types, type->value.array.element, depth + 1U);
    case MINIC_CORE_TYPE_RECORD:
        if (!type->value.record.is_complete ||
            (type->value.record.field_count != 0U &&
             type->value.record.fields == NULL)) return true;
        for (i = 0U; i < type->value.record.field_count; ++i)
            if (aarch64_special_core_aggregate_member(
                    types, type->value.record.fields[i].type_id, depth + 1U))
                return true;
        return false;
    /* FP and SIMD values may be HFA/HVA members, which our aggregate
     * argument marshaler does not claim to implement. */
    case MINIC_CORE_TYPE_FLOAT:
    case MINIC_CORE_TYPE_VECTOR:
        return true;
    default:
        return true;
    }
}

bool minic_aarch64_indirect_record_core(const MinicCoreTypeContext *types,
                                        MinicCoreTypeId id, size_t *bytes) {
    return bytes != NULL &&
           minic_multi_indirect_record_core(types, id, bytes) &&
           !aarch64_special_core_aggregate_member(types, id, 0U);
}
