#ifndef MINIC_TARGET_AARCH64_NEON_EMIT_H
#define MINIC_TARGET_AARCH64_NEON_EMIT_H

#include "frontend/ast.h"
#include "core/core_type.h"
#include "target/aarch64/frame_emit.h"

/* Target-owned AdvSIMD machine primitives. These routines neither create
 * vector Core SSA values nor admit an unimplemented source-level ACLE ABI.
 * Their callers must present qualified typed Core operations and offsets
 * allocated by the normal LP64 spill planner. */
bool minic_aarch64_neon_vector128_type(MinicType type);
bool minic_aarch64_neon_vector128_core_type(const MinicCoreTypeContext *,
                                            MinicCoreTypeId);

/* A pointer resides in a normal x-register SSA slot; each vector result or
 * operand resides in a separate 16-byte Core value slot. Unaligned user
 * pointers are valid for ordinary NEON LD1/ST1 semantics. */
bool minic_aarch64_neon_emit_load(FILE *out, const MinicA64FrameView *frame,
    MinicType type, size_t pointer_slot, size_t result_slot);
bool minic_aarch64_neon_emit_store(FILE *out, const MinicA64FrameView *frame,
    MinicType type, size_t pointer_slot, size_t value_slot);
bool minic_aarch64_neon_emit_xor(FILE *out, const MinicA64FrameView *frame,
    MinicType type, size_t left_slot, size_t right_slot, size_t result_slot);
bool minic_aarch64_neon_emit_load_core(FILE *, const MinicA64FrameView *,
    const MinicCoreTypeContext *, MinicCoreTypeId, size_t, size_t);
bool minic_aarch64_neon_emit_store_core(FILE *, const MinicA64FrameView *,
    const MinicCoreTypeContext *, MinicCoreTypeId, size_t, size_t);
bool minic_aarch64_neon_emit_xor_core(FILE *, const MinicA64FrameView *,
    const MinicCoreTypeContext *, MinicCoreTypeId, size_t, size_t, size_t);

#endif
