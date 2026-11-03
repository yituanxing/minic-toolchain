#ifndef MINIC_TARGET_AARCH64_ABI_H
#define MINIC_TARGET_AARCH64_ABI_H
#include "core/core_type.h"
#include <stdbool.h>
#include <stddef.h>
/* Executable Core-side AAPCS64 classifier. Machine ABI must not reconstruct
 * record/HFA identity from a C Program after Core types have been resolved. */
bool minic_aarch64_indirect_record_core(const MinicCoreTypeContext *,
                                        MinicCoreTypeId, size_t *);
#endif
