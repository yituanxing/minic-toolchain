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
    /* Source-builtin opaque identities are target-owned and open-ended.
     * Preserve one nominal Core opaque type per registered source identity,
     * without making the generic C->Core ingress enumerate any ISA's types. */
    struct MinicCoreOpaqueSourceTypeCacheEntry *opaque_types;
    size_t opaque_type_count;
    size_t opaque_type_capacity;
    bool failed;
} MinicCoreTypeImporter;

typedef struct MinicCoreOpaqueSourceTypeCacheEntry {
    unsigned int source_builtin_id;
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
