#ifndef MINIAS_SOURCE_GRAPH_COMMON_H
#define MINIAS_SOURCE_GRAPH_COMMON_H

#include "minias_target_api.h"
#include "source_statement_event.h"
#include "minias_source_replay_contract.h"

/* Source and Common exchange opaque sessions and typed events, never
 * the Common private assembly-session layout. */
typedef struct MiniAs MiniAs;

/* Common may drive the source pass and retain a persistent graph, but does
 * not own or inspect SourceFrame, macro/conditional stacks or SiteID tables.
 * The graph outlives each freshly allocated MiniAs semantic pass. */
MiniAsSourceGraph *minias_source_graph_create(void);
void minias_source_graph_destroy(MiniAsSourceGraph *graph);
bool minias_source_graph_is_loaded(const MiniAsSourceGraph *graph);
bool minias_source_graph_load_physical(MiniAs *as, const char *path,
                                      MiniAsSourceGraph *graph);
size_t minias_source_graph_physical_lines(const MiniAsSourceGraph *graph);
void minias_source_graph_trace(const MiniAsSourceGraph *graph,
                               size_t replay_passes, FILE *diagnostic);
bool minias_source_context_process_graph(
    MiniAs *as,MiniAsSourceGraph *graph,
    const MiniAsSourceStatementSink *statements);
bool minias_source_graph_begin_eof(MiniAs *as);
bool minias_source_graph_validate_fragment_sites(
    MiniAs *as,const MiniAsStmt *fragments,size_t fragment_count);
/* All Fragment reads arrive as borrowed, immutable spans from Common.
 * SourceGraph never owns or mutates the Common statement vector. */
bool minias_source_graph_lookup_forced_branch(MiniAs *as,
                                              const MiniAsStmt *stmt,
                                              size_t *minimum_size);
bool minias_source_graph_rebind_forced_branches(
    MiniAs *as,const MiniAsStmt *fragments,size_t fragment_count,
    MiniAsFragmentGrowthSet *growth);
bool minias_source_graph_capture_forced_branches(
    MiniAs *as,const MiniAsStmt *fragments,size_t fragment_count,
    const MiniAsFragmentGrowthSet *growth);

/* SourceContext owns the pass-local lexical state and the current SiteID.
 * Common saves/restores that ID only when materializing deferred fragments
 * at EOF, so they retain the source identity of their original directive. */
MiniAsSourceContext *minias_source_context_create(void);
void minias_source_context_destroy(MiniAsSourceContext *source);
MiniAsStableSiteId minias_source_current_site(const MiniAs *as);
/* Source records syntax whose condition may change after MCLayout; Common
 * imports the pass result without Source mutating Common layout flags. */
bool minias_source_context_layout_sensitive(const MiniAsSourceContext *source);
/* .ifdef/.ifndef consume a read-only Common symbol-defined predicate.
 * Source never allocates Symbols or inspects Common Symbol internals. */
bool minias_common_source_symbol_defined(MiniAs *as,const char *name);
void minias_source_restore_site(MiniAs *as,MiniAsStableSiteId site);
bool minias_source_try_conditional(MiniAs *as,char *text,size_t line,
                                   bool *handled);
bool minias_source_conditional_active(const MiniAs *as);
bool minias_source_try_macro_invocation(MiniAs *as,char *op,char *args,
                                        size_t line,bool *handled);
bool minias_source_try_control_directive(MiniAs *as,const char *op,
                                         const char *args,size_t line,
                                         bool *handled);
bool minias_numeric_refs_rewrite(MiniAs *as,const char *text,
                                char **out,size_t line);
/* The Source module emits statement events through the borrowed sink;
 * the Common statement lowerer remains private to assembler.c. */

#endif
