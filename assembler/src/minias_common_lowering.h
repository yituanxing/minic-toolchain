#ifndef MINIAS_COMMON_LOWERING_H
#define MINIAS_COMMON_LOWERING_H
#include "minias_internal.h"
#include "minias_common_context.h"
#include "minias_common_emission.h"
#include "source_graph_common.h"
/* Common-private domain boundary; never part of Source or Target APIs. */
struct MiniAsMeasureErrorSink { MiniAs *as; };
struct MiniAsParseContext { MiniAs *as; };
struct MiniAsDirectiveContext { MiniAs *as; };
uint64_t minias_target_exec_alignment(const MiniAs *);
bool minias_queue_literal(MiniAs *,const MiniAsSymbolExpr *,unsigned,
                          MiniAsSymbolExpr *);
bool minias_common_parse_source_graph(MiniAs *,MiniAsSourceGraph *);
#endif
