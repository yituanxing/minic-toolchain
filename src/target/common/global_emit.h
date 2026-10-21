#ifndef MINIC_TARGET_COMMON_GLOBAL_EMIT_H
#define MINIC_TARGET_COMMON_GLOBAL_EMIT_H

#include "frontend/ast.h"
#include "target/target_info.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* Initializer layout is independent of instruction selection. The caller
 * retains ownership of the target relocation expression/encoding. */
typedef bool (*MinicMultiRelocEmitter)(FILE *, const MinicC0Program *,
                                       const MinicTargetInfo *,
                                       const MinicGlobalRelocation *, size_t);

bool minic_multi_emit_relocatable_scalar_array(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, size_t storage_size,
    MinicMultiRelocEmitter emit_relocation);
bool minic_multi_emit_zero_relocation_global(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, size_t storage_size,
    MinicMultiRelocEmitter emit_relocation);
bool minic_multi_emit_recursive_record_global(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, size_t storage_size,
    MinicMultiRelocEmitter emit_relocation);
bool minic_multi_emit_record_array_global(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, size_t storage_size,
    MinicMultiRelocEmitter emit_relocation);
bool minic_multi_emit_flexible_record_global(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, size_t storage_size,
    MinicMultiRelocEmitter emit_relocation);

#endif
