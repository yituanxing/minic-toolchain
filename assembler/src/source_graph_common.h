#ifndef MINIAS_SOURCE_GRAPH_COMMON_H
#define MINIAS_SOURCE_GRAPH_COMMON_H

#include "minias_target_api.h"
#include "source_statement_event.h"
#include "minias_source_replay_contract.h"

/* Source and Common exchange a Source-owned context and typed events; no
 * Common session token passes through the Source implementation. */
#include "minias_source_semantic_services.h"

/* Common may drive the source pass and retain a persistent graph, but does
 * not own or inspect SourceFrame, macro/conditional stacks or SiteID tables.
 * The graph outlives each freshly allocated MiniAs semantic pass. */
MiniAsSourceGraph *minias_source_graph_create(void);
void minias_source_graph_destroy(MiniAsSourceGraph *graph);
bool minias_source_graph_is_loaded(const MiniAsSourceGraph *graph);
bool minias_source_graph_load_physical(MiniAsSourceContext *source, const char *path,
                                      MiniAsSourceGraph *graph);
size_t minias_source_graph_physical_lines(const MiniAsSourceGraph *graph);
void minias_source_graph_trace(const MiniAsSourceGraph *graph,
                               size_t replay_passes, FILE *diagnostic);
bool minias_source_context_process_graph(
    MiniAsSourceContext *source,MiniAsSourceGraph *graph,
    const MiniAsSourceStatementSink *statements);
bool minias_source_graph_begin_eof(MiniAsSourceContext *source);
bool minias_source_graph_validate_fragment_sites(
    MiniAsSourceContext *source,const MiniAsSourceReplayReader *reader);
/* SourceGraph receives only pass-local value projections from the live
 * Common reader. No Common Fragment/MCInst record crosses the boundary. */
bool minias_source_graph_lookup_forced_branch(MiniAsSourceContext *source,
                                              const MiniAsSourceReplayReader *reader,
                                              size_t fragment_ordinal,
                                              size_t *minimum_size);
bool minias_source_graph_rebind_forced_branches(
    MiniAsSourceContext *source,const MiniAsSourceReplayReader *reader,
    MiniAsFragmentGrowthSet *growth);
bool minias_source_graph_capture_forced_branches(
    MiniAsSourceContext *source,const MiniAsSourceReplayReader *reader,
    const MiniAsFragmentGrowthSet *growth);

/* SourceContext owns the pass-local lexical state and the current SiteID.
 * Common saves/restores that ID only when materializing deferred fragments
 * at EOF, so they retain the source identity of their original directive. */
MiniAsSourceContext *minias_source_context_create(
    const MiniAsSourceSemanticServices *services);
void minias_source_context_destroy(MiniAsSourceContext *source);
MiniAsStableSiteId minias_source_current_site(const MiniAsSourceContext *source);
/* Source records syntax whose condition may change after MCLayout; Common
 * imports the pass result without Source mutating Common layout flags. */
bool minias_source_context_layout_sensitive(const MiniAsSourceContext *source);
void minias_source_restore_site(MiniAsSourceContext *source,MiniAsStableSiteId site);
/* The Source module emits statement events through the borrowed sink;
 * the Common statement lowerer remains private to assembler.c. */

#endif
