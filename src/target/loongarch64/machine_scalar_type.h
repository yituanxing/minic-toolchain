#ifndef MINIC_TARGET_LOONGARCH64_MACHINE_SCALAR_TYPE_H
#define MINIC_TARGET_LOONGARCH64_MACHINE_SCALAR_TYPE_H

#include "target/common/lp64_scalar.h"

/* Common Core-typed integer gate for LoongArch machine-side ASM contracts.
 * The callers own their distinct register, memory and instruction semantics.
 * A missing value table fails closed, even when value_count is inconsistent. */
static inline bool minic_loong_core_integer_operand(
    const MinicCoreFunction *fn, MinicCoreValueId value,
    bool pointee, size_t width, int signed_mode) {
    MinicCoreTypeId type_id;
    const MinicCoreType *type;
    if (fn == NULL || fn->type_context == NULL || fn->values == NULL ||
        value >= fn->value_count) return false;
    type_id = fn->values[value].core_type_id;
    if (pointee && !minic_lp64_core_pointer_pointee(
            fn->type_context, type_id, &type_id)) return false;
    type = minic_core_type_base(fn->type_context, type_id);
    return type != NULL && type->kind == MINIC_CORE_TYPE_INTEGER &&
        type->has_layout && type->size == width &&
        !type->value.integer.is_bool &&
        type->value.integer.bit_width == width * 8U &&
        (signed_mode < 0 ||
         type->value.integer.is_signed == (signed_mode != 0));
}

#endif
