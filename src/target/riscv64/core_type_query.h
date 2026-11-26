#ifndef MINIC_TARGET_RISCV64_CORE_TYPE_QUERY_H
#define MINIC_TARGET_RISCV64_CORE_TYPE_QUERY_H

#include "core/core_ir.h"

/* RV64 machine decisions consume the attached execution type exclusively.
 * Source MinicType descriptors have no authority after Core has been sealed.
 * Peeling physical alignment views is shared by ISA, frame and ABI gates. */


static inline bool minic_riscv64_core_is_record(
    const MinicCoreFunction *function, MinicCoreTypeId id) {
    return minic_core_function_type_is_kind(function, id, MINIC_CORE_TYPE_RECORD);
}

static inline bool minic_riscv64_core_scalar_kind(const MinicCoreType *type) {
    return type != NULL &&
        (type->kind == MINIC_CORE_TYPE_INTEGER ||
         type->kind == MINIC_CORE_TYPE_POINTER ||
         (type->kind == MINIC_CORE_TYPE_FLOAT &&
          (type->value.floating.format == MINIC_CORE_FLOAT_BINARY32 ||
           type->value.floating.format == MINIC_CORE_FLOAT_BINARY64 ||
           type->value.floating.format == MINIC_CORE_FLOAT_BINARY128)));
}

#endif
