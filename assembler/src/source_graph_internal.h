#ifndef MINIAS_SOURCE_GRAPH_INTERNAL_H
#define MINIAS_SOURCE_GRAPH_INTERNAL_H

#include "minias_internal.h"

/* Common's lexical expansion owns source text, while the persistent graph
 * owns cross-replay SiteIDs and forced branch identities. This private header
 * describes their shared ownership contract, never an ISA-specific grammar. */
typedef struct MiniAsSourceLine {
    char *text;
    size_t line;
    size_t source_segment;
} MiniAsSourceLine;

/* The physical file and each expanded source body are processed by the
 * same macro/REPT/IRP collector. A SourceFrame owns only its lexical text;
 * the assembler retains typed Fragment/Symbol data independently of it.
 * Later conditional-region invalidation can retain this frame instead of
 * depending on a second physical-file parser with different state rules. */
typedef enum MiniAsSourceFrameKind {
    MINIAS_SOURCE_FRAME_FILE,
    MINIAS_SOURCE_FRAME_EXPANSION
} MiniAsSourceFrameKind;

typedef enum MiniAsSourceSiteKind {
    MINIAS_SOURCE_SITE_LINE = 1,
    MINIAS_SOURCE_SITE_SEGMENT,
    MINIAS_SOURCE_SITE_MACRO,
    MINIAS_SOURCE_SITE_REPT,
    MINIAS_SOURCE_SITE_IRP,
    MINIAS_SOURCE_SITE_INLINE_REPEAT
} MiniAsSourceSiteKind;

/* Full-key equality, not a truncated path hash, defines StableSiteID.
 * Parent is an already-interned lexical/expansion SiteID; an expansion
 * child's identity is unaffected by GNU's independently incrementing \@. */
typedef struct MiniAsSourceSiteKey {
    MiniAsStableSiteId parent;
    MiniAsStableSiteId definition;
    size_t origin_line;
    size_t origin_segment;
    size_t iteration;
    MiniAsSourceSiteKind kind;
} MiniAsSourceSiteKey;

typedef struct MiniAsSourceSiteEntry {
    MiniAsSourceSiteKey key;
    size_t last_condition_pass;
    MiniAsStableSiteId last_condition_owner;
    bool last_condition_active;
    /* Zero means that this lexical/expansion site has no forced branch.
     * The index is stable across complete semantic replays; a pass-local
     * Fragment ordinal is never persisted in this registry. */
    size_t forced_branch_index;
} MiniAsSourceSiteEntry;

/* The source site identifies one expanded instruction, while this retained
 * shape prevents a branch from being accidentally attached to a different
 * instruction after a .if/.elseif changes the emitted Fragment topology.
 * All fields here are semantic parse-time values, never an output offset. */
typedef struct MiniAsForcedOperandShape {
    MiniAsMCOperandKind kind;
    MiniAsMCRegisterClass reg_class;
    uint16_t target_reg_bank;
    unsigned reg;
    bool register_64;
} MiniAsForcedOperandShape;

typedef struct MiniAsForcedBranchSite {
    MiniAsStableSiteId source_site;
    char *op;
    uint32_t opcode;
    uint32_t target_form;
    uint32_t mode_bits;
    size_t minimum_size;
    unsigned operand_count;
    MiniAsForcedOperandShape *operands;
} MiniAsForcedBranchSite;

typedef struct MiniAsSourceFrame {
    MiniAsSourceFrameKind kind;
    MiniAsSourceLine *lines;
    size_t count;
    size_t capacity;
    MiniAsSourceSiteKind expansion_kind;
    MiniAsStableSiteId invocation_site;
    MiniAsStableSiteId definition_site;
    size_t iteration;
} MiniAsSourceFrame;

struct MiniAsSourceGraph {
    MiniAsSourceFrame physical;
    MiniAsSourceSiteEntry *sites;
    size_t site_count;
    size_t site_capacity;
    size_t *site_slots; /* SiteID, zero for unoccupied */
    size_t site_slot_capacity;
    size_t expansion_count;
    size_t file_reads;
    size_t parse_passes;
    size_t conditional_changes;
    size_t ordinal_drifts;
    MiniAsForcedBranchSite *forced_branches;
    size_t forced_branch_count;
    size_t forced_branch_capacity;
};


/* SourceGraph (persists across layout-sensitive semantic source replays). */
bool minias_source_graph_intern(MiniAs *as, MiniAsSourceSiteKey key,
                         MiniAsStableSiteId *site_id);
bool minias_source_graph_line_site(MiniAs *as, const MiniAsSourceLine *line,
                            size_t segment, MiniAsStableSiteId *site_id);
void minias_source_graph_record_condition(MiniAs *as, MiniAsStableSiteId owner,
                                   bool active);
void minias_source_graph_destroy(MiniAsSourceGraph *graph);
bool minias_source_graph_load_physical(MiniAs *as, const char *path,
                                      MiniAsSourceGraph *graph);
bool minias_source_graph_lookup_forced_branch(MiniAs *as, const MiniAsStmt *stmt,
                                       size_t *minimum_size);
bool minias_source_graph_rebind_forced_branches(MiniAs *as, size_t **ids,
                                         size_t *count, size_t *capacity);
bool minias_source_graph_capture_forced_branches(MiniAs *as, const size_t *ids,
                                          size_t count);

/* Source lexical/macro/repeat expansion (fresh inside each semantic pass). */
void minias_source_lines_destroy(MiniAsSourceLine *lines, size_t count);
void minias_macro_destroy(MiniAsMacro *macro);
void minias_source_context_destroy(MiniAsSourceContext *source);
MiniAsMacro *minias_macro_find(MiniAs *as, const char *name);
void minias_macro_purge(MiniAsSourceContext *source, const char *name);
bool minias_macro_expand(MiniAs *as, MiniAsMacro *macro,
                             const char *args, size_t line);
bool minias_source_frame_process(MiniAs *as, const MiniAsSourceFrame *frame);

/* The intentionally recursive parser boundary; both sides live in Common. */
bool minias_statement_process(MiniAs *as, char *text, size_t line);
bool minias_numeric_refs_rewrite(MiniAs *as, const char *text,
                                char **out, size_t line);

#endif
