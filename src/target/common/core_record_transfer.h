#ifndef MINIC_TARGET_COMMON_CORE_RECORD_TRANSFER_H
#define MINIC_TARGET_COMMON_CORE_RECORD_TRANSFER_H

#include "core/core_ir.h"
#include "target/common/lp64_scalar.h"

/* Only common identity/layout checks. Target byte limits, zero-size policy
 * and destination frame placement remain with the caller. */
static inline bool minic_core_record_pointer_matches(
    const MinicCoreFunction *fn, MinicCoreValueId value, MinicCoreTypeId record) {
    MinicCoreTypeId pointee;
    return fn->values != NULL && value < fn->value_count &&
           minic_lp64_core_pointer_pointee(fn->type_context,
                fn->values[value].core_type_id, &pointee) &&
           minic_lp64_core_equal_unqualified(fn->type_context, pointee, record);
}

static inline const MinicCoreType *minic_core_record_transfer_type(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins) {
    const MinicCoreType *record;
    if (fn == NULL || fn->type_context == NULL || ins == NULL ||
        ins->result != MINIC_CORE_VALUE_INVALID) return NULL;
    record = minic_core_function_base_type(fn, ins->core_type_id);
    if (record == NULL || record->kind != MINIC_CORE_TYPE_RECORD ||
        !record->has_layout || record->alignment == 0U) return NULL;
    if (ins->kind == MINIC_CORE_INSTRUCTION_RECORD_COPY) {
        if (!minic_core_record_pointer_matches(fn,
                ins->value.record_copy.destination_address, ins->core_type_id) ||
            !minic_core_record_pointer_matches(fn,
                ins->value.record_copy.source_address, ins->core_type_id)) return NULL;
    } else if (ins->kind == MINIC_CORE_INSTRUCTION_RECORD_LOAD) {
        MinicCoreObjectId dst = ins->value.record_load.destination_object;
        if (fn->objects == NULL || dst >= fn->object_count ||
            !minic_lp64_core_equal_unqualified(fn->type_context,
                fn->objects[dst].core_type_id, ins->core_type_id) ||
            !minic_core_record_pointer_matches(fn,
                ins->value.record_load.source_address, ins->core_type_id)) return NULL;
    } else return NULL;
    return record;
}

#endif
