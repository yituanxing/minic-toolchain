#ifndef MINIC_TARGET_COMMON_GLOBAL_EMIT_H
#define MINIC_TARGET_COMMON_GLOBAL_EMIT_H

#include "frontend/ast.h"
#include "target/target_info.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>

/* Initializer layout is independent of instruction selection. The caller
 * retains ownership of the target relocation expression/encoding. */
typedef bool (*MinicMultiRelocEmitter)(FILE *, const MinicC0Program *,
                                       const MinicTargetInfo *,
                                       const MinicGlobalRelocation *, size_t);
/* Scalar payload spelling belongs to the target; DataLayout and relocation
 * slot traversal remain shared between the four supported architectures. */
typedef bool (*MinicMultiScalarBitsEmitter)(FILE *, const MinicC0Program *,
                                            const MinicTargetInfo *, MinicType,
                                            uint64_t);

/* Common source-section sanitizer shared by the x86/RV32/RV64 and direct
 * ELF emission paths: no architecture may interpolate raw GAS directives. */
bool minic_multi_elf_safe_section_name(const char *);
bool minic_multi_emit_relocatable_scalar_array(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, size_t storage_size,
    MinicMultiRelocEmitter emit_relocation,
    MinicMultiScalarBitsEmitter emit_bits);
/* Canonical flat-record layout walk; target callbacks retain assembly syntax. */
bool minic_multi_record_initializer_needs_recursive(
    const MinicRecord *record, const MinicGlobalObject *object);
bool minic_multi_emit_flat_record_global(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, size_t storage_size,
    MinicMultiRelocEmitter emit_relocation,
    MinicMultiScalarBitsEmitter emit_bits);
bool minic_multi_emit_zero_relocation_global(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, size_t storage_size,
    MinicMultiRelocEmitter emit_relocation);
/* Admit only nested records/arrays/unions the common recursive walker can
 * serialize without RV64's special bitfield/flexible-array paths. */
/* Shared ELF bitfield packing, including explicit zero-width barriers,
 * stored as data bytes by the common recursive GlobalObject layout. */
bool minic_multi_emit_recursive_aggregate_global(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, size_t storage_size,
    MinicMultiRelocEmitter emit_relocation,
    MinicMultiScalarBitsEmitter emit_bits);
bool minic_multi_emit_recursive_record_global(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, size_t storage_size,
    MinicMultiRelocEmitter emit_relocation);
bool minic_multi_emit_record_array_global(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, size_t storage_size,
    MinicMultiRelocEmitter emit_relocation);
/* GNU/ELF globals shared by AArch64 and LoongArch64. */
bool minic_multi_emit_simple_global(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, MinicMultiRelocEmitter emit_relocation);
/* Resolve function/object AST identity for target-owned ELF relocations. */
const char *minic_multi_global_relocation_target_name(
    const MinicC0Program *program, const MinicGlobalRelocation *relocation);

bool minic_multi_emit_elf_relocation(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalRelocation *relocation, size_t width);
const char *minic_multi_elf_visibility_directive(
    MinicSymbolVisibility visibility);
bool minic_multi_emit_elf_symbol_visibility(
    FILE *out, MinicSymbolVisibility visibility, const char *name);

#endif
