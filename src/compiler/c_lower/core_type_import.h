#ifndef MINIC_CORE_TYPE_IMPORT_H
#define MINIC_CORE_TYPE_IMPORT_H

/* C-owned semantic types terminate at this ingress. Do not include this
 * adapter from core_type.h, the verifier, or machine emitters. */
#include "core/core_type.h"
#include "frontend/ast.h"
#include "target/source_policy/source_info.h"

typedef struct MinicCoreTypeImporter {
    const MinicC0Program *program;
    const MinicTargetInvocation *invocation;
    MinicCoreTypeContext *context;
    MinicCoreTypeId *record_types;
    size_t record_count;
    MinicCoreTypeId *function_types;
    size_t function_count;
    /* Nominal source types with no object layout (a target builtin or an
     * incomplete C enum) still need distinct identities in function pointer
     * signatures and transitive record members.  Keep one Core opaque ID per
     * kind/source identity; never infer a machine ABI for an incomplete enum. */
    struct MinicCoreOpaqueSourceTypeCacheEntry *opaque_types;
    size_t opaque_type_count;
    size_t opaque_type_capacity;
    bool failed;
} MinicCoreTypeImporter;

typedef struct MinicCoreOpaqueSourceTypeCacheEntry {
    size_t source_id;
    bool is_incomplete_enum;
    MinicCoreTypeId type_id;
} MinicCoreOpaqueSourceTypeCacheEntry;

bool minic_core_type_importer_initialize(MinicCoreTypeImporter *importer,
                                        const MinicC0Program *program,
                                        const MinicTargetInvocation *invocation,
                                        MinicCoreTypeContext *context);
void minic_core_type_importer_destroy(MinicCoreTypeImporter *importer);
/* The importer is a compilation-unit transaction. On failure it is poisoned;
 * its owner must discard the incomplete context instead of publishing it. */
bool minic_core_type_import(MinicCoreTypeImporter *importer,
                            MinicType source,
                            MinicCoreTypeId *result);

#endif
