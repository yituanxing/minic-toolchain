#ifndef MINIAS_OBJECT_WRITER_H
#define MINIAS_OBJECT_WRITER_H

#include "minias_object_model.h"
#include "minias_elf_target_policy.h"
#include "minias_coff_target_policy.h"

/* One terminal, stack-scoped request. Common owns the retained semantic
 * records and emitted bytes; this dispatcher borrows them and selects a
 * format writer. No writer receives the mutable assembly session or a
 * general TargetOps callback table. */
typedef struct MiniAsObjectWriterRequest {
    MiniAsObjectModelView model;
    const MiniAsObjectSpec *spec;
    MiniAsTargetABIView target_abi;
    uint32_t target_output_features;

    /* Format-private, immutable writer policy. There is only an ELF writer
     * today; do not advertise COFF/Mach-O merely by adding enum values. */
    struct {
        const MiniAsElfTargetPolicy *policy;
    } elf;
    struct {
        const MiniAsCoffTargetPolicy *policy;
    } coff;

    char error[MINIAS_MAX_ERROR];
} MiniAsObjectWriterRequest;

bool minias_write_object(MiniAsObjectWriterRequest *,const char *path);

#endif
