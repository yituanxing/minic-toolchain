#ifndef MINIAS_FRAGMENT_INTERNAL_H
#define MINIAS_FRAGMENT_INTERNAL_H

#include "minias_target_api.h"

/* Shared only by Common and the SourceGraph replay implementation. The
 * parser/MCLayout/Emission own every field and every allocation; SourceGraph
 * borrows the retained Fragments to compare stable replay shapes. Targets
 * receive the restricted phase views in minias_target_api.h instead. */
typedef struct MiniAsInstructionFragment {
    MiniAsMCInst *inst;
} MiniAsInstructionFragment;

typedef struct MiniAsAlignmentFragment {
    uint64_t alignment;
    bool has_fill;
    unsigned char fill;
    bool has_max_skip;
    uint64_t max_skip;
} MiniAsAlignmentFragment;

typedef struct MiniAsOriginFragment {
    MiniAsOrgMode mode;
    MiniAsDeferredExpr *expr;
    uint64_t target;
    size_t symbol_index;
    int64_t addend;
    bool has_fill;
    unsigned char fill;
} MiniAsOriginFragment;

typedef struct MiniAsFixedDataFragment {
    MiniAsMCData *data;
} MiniAsFixedDataFragment;

typedef struct MiniAsVariableDataFragment {
    bool deferred_size;
    bool resolved;
    uint64_t repeat;
    uint64_t value;
    unsigned element_width;
    MiniAsDeferredExpr *exprs[3];
    unsigned expr_count;
} MiniAsVariableDataFragment;

typedef struct MiniAsBytesFragment {
    bool deferred_size;
    unsigned char *data;
    size_t data_size;
    unsigned leb_mode;
    MiniAsDeferredExpr *leb_expr;
} MiniAsBytesFragment;

typedef union MiniAsFragmentPayload {
    MiniAsInstructionFragment instruction;
    MiniAsAlignmentFragment alignment;
    MiniAsOriginFragment origin;
    MiniAsFixedDataFragment fixed_data;
    MiniAsVariableDataFragment variable;
    MiniAsBytesFragment bytes;
} MiniAsFragmentPayload;

typedef struct MiniAsStmt {
    MiniAsFragmentKind fragment_kind;
    MiniAsStableSiteId source_site;
    char *op;
    char *args;
    size_t line;
    int section;
    uint32_t subsection;
    uint64_t offset;
    uint32_t size;
    MiniAsTargetSiteState site;
    MiniAsFragmentPayload payload;
} MiniAsStmt;

#endif
