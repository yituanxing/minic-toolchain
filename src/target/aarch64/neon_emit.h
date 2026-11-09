#ifndef MINIC_TARGET_AARCH64_NEON_EMIT_H
#define MINIC_TARGET_AARCH64_NEON_EMIT_H

#include "core/core_type.h"

/* AArch64 AdvSIMD's ABI-neutral Core shape classifier. Native Q-register
 * emission is owned by the typed MachineIR selector and GNU serializer;
 * no parallel Core-to-FILE* vector instruction writer exists here. */
bool minic_aarch64_neon_vector128_core_type(const MinicCoreTypeContext *,
                                            MinicCoreTypeId);
bool minic_aarch64_neon_register_vector_core_type(
    const MinicCoreTypeContext *, MinicCoreTypeId, size_t *);

#endif
