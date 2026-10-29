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
 * slot traversal remain shared across all registered ELF targets. */
typedef bool (*MinicMultiScalarBitsEmitter)(FILE *, const MinicC0Program *,
                                            const MinicTargetInfo *, MinicType,
                                            uint64_t);

/* One checked language/DataLayout decision for a global object's payload.
 * No ISA register, relocation encoding or assembly spelling enters the plan.
 * Native backends can select their directives from this same classification
 * while retaining their own relocation callbacks and ELF symbol policy. */
typedef enum MinicMultiGlobalPayloadKind {
    MINIC_MULTI_GLOBAL_PAYLOAD_ZERO,
    MINIC_MULTI_GLOBAL_PAYLOAD_ZERO_RELOCATIONS,
    MINIC_MULTI_GLOBAL_PAYLOAD_RECORD_FLAT,
    MINIC_MULTI_GLOBAL_PAYLOAD_RECORD_RECURSIVE,
    MINIC_MULTI_GLOBAL_PAYLOAD_RECORD_FLEXIBLE,
    MINIC_MULTI_GLOBAL_PAYLOAD_ARRAY_RECORD,
    MINIC_MULTI_GLOBAL_PAYLOAD_ARRAY_RECURSIVE,
    MINIC_MULTI_GLOBAL_PAYLOAD_ARRAY_SCALAR_RELOCATIONS,
    MINIC_MULTI_GLOBAL_PAYLOAD_SCALAR_RELOCATION,
    MINIC_MULTI_GLOBAL_PAYLOAD_SCALAR_BITS
} MinicMultiGlobalPayloadKind;

typedef struct MinicMultiGlobalEmissionPlan {
    MinicMultiGlobalPayloadKind payload_kind;
    size_t storage_size;
    size_t storage_alignment;
    size_t scalar_width;
    MinicType scalar_type;
    /* Default ELF section only; an explicit validated section attribute
     * remains the owner's choice.  It is not target-specific GAS syntax. */
    const char *default_elf_section;
} MinicMultiGlobalEmissionPlan;

bool minic_multi_global_emission_plan_build(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, MinicMultiGlobalEmissionPlan *plan);

/* The ABI-neutral payload decision and byte walk are owned here once.
 * Native targets supply only scalar GAS spelling and relocation expression. */
bool minic_multi_emit_global_payload(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, const MinicMultiGlobalEmissionPlan *plan,
    MinicMultiRelocEmitter emit_relocation,
    MinicMultiScalarBitsEmitter emit_bits);
bool minic_multi_emit_elf_global_section(
    FILE *out, const MinicGlobalObject *object, const char *fallback);
bool minic_multi_emit_elf_global_binding(
    FILE *out, const MinicGlobalObject *object);

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
