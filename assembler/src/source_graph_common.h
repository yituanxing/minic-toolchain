#ifndef MINIAS_SOURCE_GRAPH_COMMON_H
#define MINIAS_SOURCE_GRAPH_COMMON_H

#include "minias_internal.h"

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
bool minias_source_context_process_graph(MiniAs *as,MiniAsSourceGraph *graph);
bool minias_source_graph_begin_eof(MiniAs *as);
bool minias_source_graph_validate_fragment_sites(MiniAs *as);
bool minias_source_graph_lookup_forced_branch(MiniAs *as,
                                              const MiniAsStmt *stmt,
                                              size_t *minimum_size);
bool minias_source_graph_rebind_forced_branches(MiniAs *as, size_t **ids,
                                                 size_t *count,
                                                 size_t *capacity);
bool minias_source_graph_capture_forced_branches(MiniAs *as,
                                                  const size_t *ids,
                                                  size_t count);

/* SourceContext owns the pass-local lexical state and the current SiteID.
 * Common saves/restores that ID only when materializing deferred fragments
 * at EOF, so they retain the source identity of their original directive. */
bool minias_source_context_init(MiniAs *as);
void minias_source_context_destroy(MiniAsSourceContext *source);
MiniAsStableSiteId minias_source_current_site(const MiniAs *as);
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
/* Source expansion recursively calls the one Common statement lowerer. */
bool minias_statement_process(MiniAs *as,char *text,size_t line);

#endif
