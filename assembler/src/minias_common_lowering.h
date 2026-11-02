#ifndef MINIAS_COMMON_LOWERING_H
#define MINIAS_COMMON_LOWERING_H
#include "minias_internal.h"
#include "source_graph_common.h"
/* Common-private domain boundary; never part of Source or Target APIs. */
typedef struct MiniAsSectionIdentity {
    const char *name;
    const char *group_signature;
    const char *link_order_reference;
    bool has_unique_id;
    uint32_t unique_id;
} MiniAsSectionIdentity;
struct MiniAsMeasureErrorSink { MiniAs *as; };
struct MiniAsParseContext { MiniAs *as; };
struct MiniAsDirectiveContext { MiniAs *as; };
bool advance_current_location(MiniAs *,uint64_t);
int minias_find_section_key(const MiniAs *,const MiniAsSectionIdentity *);
int minias_ensure_section_key(MiniAs *,const MiniAsSectionIdentity *,
                              uint32_t,uint32_t,uint64_t,uint64_t);
bool minias_record_fixup(MiniAs *,int,uint64_t,MiniAsFixupKind,unsigned,
                         const char *,size_t,int64_t,MiniAsFixupOrigin,
                         bool,bool,size_t);
bool minias_section_append_pattern(MiniAs *,int,const unsigned char *,
                                   size_t,size_t,bool);
bool minias_section_append_fill(MiniAs *,int,size_t,uint8_t,bool);
uint64_t minias_target_exec_alignment(const MiniAs *);
bool minias_queue_literal(MiniAs *,const MiniAsSymbolExpr *,unsigned,
                          MiniAsSymbolExpr *);
bool minias_common_emit_data_stmt(MiniAs *,const MiniAsStmt *);
bool minias_common_parse_source_graph(MiniAs *,MiniAsSourceGraph *);
#endif
