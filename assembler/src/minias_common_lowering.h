#ifndef MINIAS_COMMON_LOWERING_H
#define MINIAS_COMMON_LOWERING_H
#include "minias_internal.h"
/* Common-private domain boundary; never part of Source or Target APIs. */
struct MiniAsMeasureErrorSink { MiniAs *as; };
struct MiniAsParseContext { MiniAs *as; };
struct MiniAsDirectiveContext { MiniAs *as; };
/* A single parse-complete snapshot consumed by MCLayout. Parse internals
 * and mutable worklists remain entirely private to Lowering. */
typedef struct MiniAsLoweringLayoutPlan {
    bool layout_sensitive;
    bool has_deferred_data;
    bool has_org_constraints;
    bool has_cross_subsection_size_end;
} MiniAsLoweringLayoutPlan;
bool minias_common_lowering_init(MiniAs *);
void minias_common_lowering_destroy(MiniAs *);
bool minias_common_lowering_layout_plan(const MiniAs *,MiniAsLoweringLayoutPlan *);
uint64_t minias_target_exec_alignment(const MiniAs *);
bool minias_queue_literal(MiniAs *,const MiniAsSymbolExpr *,unsigned,
                          MiniAsSymbolExpr *);
bool minias_common_parse_source_graph(MiniAs *,MiniAsSourceGraph *);
#endif
