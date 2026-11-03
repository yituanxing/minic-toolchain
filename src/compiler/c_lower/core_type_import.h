#ifndef MINIC_CORE_TYPE_IMPORT_H
#define MINIC_CORE_TYPE_IMPORT_H

/* C-owned semantic types terminate at this ingress. Do not include this
 * adapter from core_type.h, the verifier, or machine emitters. */
#include "core/core_type.h"
#include "frontend/ast.h"
#include "target/data_layout.h"

typedef struct MinicCoreTypeImporter {
    const MinicC0Program *program;
    const MinicDataLayout *layout;
    MinicCoreTypeContext *context;
    MinicCoreTypeId *record_types;
    size_t record_count;
    MinicCoreTypeId *function_types;
    size_t function_count;
    /* Private source-builtin identity cache, never exposed to Core users. */
    MinicCoreTypeId opaque_types[3];
    bool failed;
} MinicCoreTypeImporter;

bool minic_core_type_importer_initialize(MinicCoreTypeImporter *importer,
                                        const MinicC0Program *program,
                                        const MinicDataLayout *layout,
                                        MinicCoreTypeContext *context);
void minic_core_type_importer_destroy(MinicCoreTypeImporter *importer);
/* The importer is a compilation-unit transaction. On failure it is poisoned;
 * its owner must discard the incomplete context instead of publishing it. */
bool minic_core_type_import(MinicCoreTypeImporter *importer,
                            MinicType source,
                            MinicCoreTypeId *result);

#endif
