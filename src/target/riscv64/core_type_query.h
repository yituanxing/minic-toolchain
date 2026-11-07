#ifndef MINIC_TARGET_RISCV64_CORE_TYPE_QUERY_H
#define MINIC_TARGET_RISCV64_CORE_TYPE_QUERY_H

#include "core/core_ir.h"

/* RV64 machine decisions consume the attached execution type exclusively.
 * Source MinicType descriptors have no authority after Core has been sealed.
 * Peeling physical alignment views is shared by ISA, frame and ABI gates. */
static inline const MinicCoreType *minic_riscv64_core_base_type(
    const MinicCoreFunction *function, MinicCoreTypeId id) {
    if (function == NULL || function->type_context == NULL) return NULL;
    return minic_core_type_get(function->type_context,
        minic_core_type_unqualified(function->type_context, id));
}

static inline bool minic_riscv64_core_is_i128(
    const MinicCoreFunction *function, MinicCoreTypeId id) {
    const MinicCoreType *type = minic_riscv64_core_base_type(function, id);
    return type != NULL && type->kind == MINIC_CORE_TYPE_INTEGER &&
           type->value.integer.bit_width == 128U;
}

static inline bool minic_riscv64_core_is_record(
    const MinicCoreFunction *function, MinicCoreTypeId id) {
    const MinicCoreType *type = minic_riscv64_core_base_type(function, id);
    return type != NULL && type->kind == MINIC_CORE_TYPE_RECORD;
}

#endif
