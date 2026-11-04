#ifndef MINIC_COMPILER_GLOBAL_PAYLOAD_H
#define MINIC_COMPILER_GLOBAL_PAYLOAD_H

/* C ingress owns semantic classification and byte-accurate DataLayout walks
 * for global initializers. The resulting checked fragments are independent
 * of ISA instructions, ELF directives and assembly output. Targets only
 * choose physical serialization and relocation spelling. */
#include "frontend/ast.h"
#include "core/module_ir.h"
#include "target/source_policy/source_info.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Source-only classification chooses the C initializer walker. It is not a
 * published Core Module contract: the machine sees the resulting complete
 * byte fragments, Core type layout, relocations, and linker symbol facts. */
typedef enum MinicSourceGlobalPayloadKind {
    MINIC_SOURCE_GLOBAL_PAYLOAD_ZERO = 0,
    MINIC_SOURCE_GLOBAL_PAYLOAD_ZERO_RELOCATIONS,
    MINIC_SOURCE_GLOBAL_PAYLOAD_RECORD_FLAT,
    MINIC_SOURCE_GLOBAL_PAYLOAD_RECORD_RECURSIVE,
    MINIC_SOURCE_GLOBAL_PAYLOAD_RECORD_FLEXIBLE,
    MINIC_SOURCE_GLOBAL_PAYLOAD_ARRAY_RECORD,
    MINIC_SOURCE_GLOBAL_PAYLOAD_ARRAY_RECURSIVE,
    MINIC_SOURCE_GLOBAL_PAYLOAD_ARRAY_SCALAR_RELOCATIONS,
    MINIC_SOURCE_GLOBAL_PAYLOAD_SCALAR_RELOCATION,
    MINIC_SOURCE_GLOBAL_PAYLOAD_SCALAR_BITS
} MinicSourceGlobalPayloadKind;

typedef struct MinicMultiGlobalEmissionPlan {
    MinicSourceGlobalPayloadKind payload_kind;
    MinicCoreModuleDefaultSection default_section;
    size_t storage_size;
    size_t storage_alignment;
    size_t scalar_width;
    MinicType scalar_type;
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
    const MinicC0Program *program, const MinicTargetInvocation *target,
    const MinicGlobalObject *object, MinicMultiGlobalEmissionPlan *plan);
bool minic_multi_global_payload_visit(
    const MinicC0Program *program, const MinicTargetInvocation *target,
    const MinicGlobalObject *object, const MinicMultiGlobalEmissionPlan *plan,
    const MinicMultiGlobalPayloadSink *sink);
/* Source initializer admission for explicitly named ELF .bss/.bss.*:
 * Common's NOBITS serializer cannot encode nonzero bytes or relocations.
 * This decision is sealed by the ModuleIR ingress for every backend. */
bool minic_multi_global_explicit_bss_admitted(const MinicGlobalObject *object);
bool minic_multi_record_initializer_needs_recursive(
    const MinicC0Program *program, const MinicRecord *record,
    const MinicGlobalObject *object);
const char *minic_multi_global_relocation_target_name(
    const MinicC0Program *program, const MinicGlobalRelocation *relocation);

#endif
