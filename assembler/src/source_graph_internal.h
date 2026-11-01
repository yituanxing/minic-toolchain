#ifndef MINIAS_SOURCE_GRAPH_INTERNAL_H
#define MINIAS_SOURCE_GRAPH_INTERNAL_H

#include "source_graph_common.h"

typedef struct MiniAsSourceLine {
    char *text;
    size_t line;
    size_t source_segment;
} MiniAsSourceLine;

/* These source-only types intentionally do not appear in the Target-visible
 * MiniAs internal model. Source expansion is their lifetime owner. */
typedef struct MiniAsConditional {
    bool parent_active;
    bool condition_true;
    bool else_seen;
    MiniAsStableSiteId source_site;
} MiniAsConditional;

typedef struct MiniAsMacroParam {
    char *name;
    char *default_value;
    bool required;
    bool variadic;
} MiniAsMacroParam;

typedef struct MiniAsMacro {
    char *name;
    MiniAsMacroParam *params;
    size_t param_count;
    MiniAsSourceLine *body;
    size_t body_count;
    MiniAsStableSiteId definition_site;
} MiniAsMacro;

struct MiniAsSourceContext {
    MiniAsConditional *conditionals;
    size_t conditional_count;
    size_t conditional_capacity;
    MiniAsMacro *macros;
    size_t macro_count;
    size_t macro_capacity;
    size_t macro_expansion_counter;
    size_t macro_expansion_depth;
    bool altmacro_active;
    bool conditional_active;
    bool source_ended;
    MiniAsSourceGraph *graph; /* borrowed from assembly invocation */
    MiniAsStableSiteId current_site;
    MiniAsStableSiteId current_expansion_site;
};

/* Common's lexical expansion owns source text, while the persistent graph
 * owns cross-replay SiteIDs and forced branch identities. This private header
 * describes their shared ownership contract, never an ISA-specific grammar. */
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


/* Source-only SiteID interning and expansion/condition provenance. */
bool minias_source_graph_intern(MiniAs *as, MiniAsSourceSiteKey key,
                         MiniAsStableSiteId *site_id);
bool minias_source_graph_line_site(MiniAs *as, const MiniAsSourceLine *line,
                            MiniAsStableSiteId *site_id);
void minias_source_graph_record_condition(MiniAs *as, MiniAsStableSiteId owner,
                                   bool active);

/* Source lexical/macro/repeat expansion (fresh inside each semantic pass). */
void minias_source_lines_destroy(MiniAsSourceLine *lines, size_t count);
bool minias_source_frame_process(MiniAs *as, const MiniAsSourceFrame *frame);

#endif
