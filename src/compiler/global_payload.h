#ifndef MINIC_COMPILER_GLOBAL_PAYLOAD_H
#define MINIC_COMPILER_GLOBAL_PAYLOAD_H

/* C ingress owns semantic classification and byte-accurate DataLayout walks
 * for global initializers. The resulting checked fragments are independent
 * of ISA instructions, ELF directives and assembly output. Targets only
 * choose physical serialization and relocation spelling. */
#include "frontend/ast.h"
#include "target/target_info.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

/* Payload classification is source/DataLayout owned. The legacy default
 * section spelling is retained temporarily for FILE-based target callers;
 * the sealed ModuleIR snapshots the classification before code emission. */
typedef struct MinicMultiGlobalEmissionPlan {
    MinicMultiGlobalPayloadKind payload_kind;
    size_t storage_size;
    size_t storage_alignment;
    size_t scalar_width;
    MinicType scalar_type;
    const char *default_elf_section;
} MinicMultiGlobalEmissionPlan;

typedef struct MinicMultiGlobalPayloadSink {
    void *context;
    bool (*emit_zero)(void *context, size_t bytes);
    bool (*emit_bits)(void *context, MinicType source_type,
                      size_t width, uint64_t bits);
    bool (*emit_relocation)(void *context,
                            const MinicGlobalRelocation *source, size_t width);
} MinicMultiGlobalPayloadSink;

bool minic_multi_global_payload_plan_build(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, MinicMultiGlobalEmissionPlan *plan);
bool minic_multi_global_payload_visit(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, const MinicMultiGlobalEmissionPlan *plan,
    const MinicMultiGlobalPayloadSink *sink);
/* Source initializer admission for explicitly named ELF .bss/.bss.*:
 * Common's NOBITS serializer cannot encode nonzero bytes or relocations.
 * The one source check is reused by ModuleIR and historical FILE adapters. */
bool minic_multi_global_explicit_bss_admitted(const MinicGlobalObject *object);
bool minic_multi_record_initializer_needs_recursive(
    const MinicRecord *record, const MinicGlobalObject *object);
const char *minic_multi_global_relocation_target_name(
    const MinicC0Program *program, const MinicGlobalRelocation *relocation);

#endif
