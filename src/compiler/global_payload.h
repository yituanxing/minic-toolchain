#ifndef MINIC_COMPILER_GLOBAL_PAYLOAD_H
#define MINIC_COMPILER_GLOBAL_PAYLOAD_H

/* C ingress owns semantic classification and byte-accurate DataLayout walks
 * for global initializers. The resulting checked fragments are independent
 * of ISA instructions, ELF directives and assembly output. Targets only
 * choose physical serialization and relocation spelling. */
#include "frontend/ast.h"
#include "core/module_ir.h"
#include "target/target_info.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Source/DataLayout owns the C initializer walk, but it publishes the
 * target-neutral ModuleIR classification directly.  Do not maintain a
 * second 10-way payload taxonomy or smuggle GAS section spelling through
 * the C ingress: the backend owns physical ELF/assembly serialization. */
typedef struct MinicMultiGlobalEmissionPlan {
    MinicCoreModulePayloadKind payload_kind;
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
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *object, MinicMultiGlobalEmissionPlan *plan);
bool minic_multi_global_payload_visit(
    const MinicC0Program *program, const MinicTargetInfo *target,
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
