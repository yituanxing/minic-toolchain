#include "minias_internal.h"
#include "minias_common_emission.h"
#include "minias_common_context.h"
#include "minias_common_layout.h"
#include "minias_common_lowering.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Scan and final verification use one subsection-local alignment and
 * GNU max-skip calculation, with stage-specific diagnostics at each call. */
static bool fragment_alignment_padding(const MiniAsStmt *stmt,
                                       uint64_t cursor,uint64_t *padding) {
    const uint64_t alignment=stmt->payload.alignment.alignment;
    if(alignment==0U || (alignment&(alignment-1U))!=0U)return false;
    *padding=(alignment-(cursor&(alignment-1U)))&(alignment-1U);
    if(stmt->payload.alignment.has_max_skip && *padding>stmt->payload.alignment.max_skip)
        *padding=0U;
    return true;
}

static bool fragment_has_deferred_data(const MiniAsStmt *stmt) {
    switch(stmt->fragment_kind) {
    case MINIAS_FRAGMENT_BYTES:
        return stmt->payload.bytes.deferred_size;
    case MINIAS_FRAGMENT_FILL:
    case MINIAS_FRAGMENT_SPACE:
        return stmt->payload.variable.deferred_size;
    default:
        return false;
    }
}

/* Common MCLayout subsystem. Fragment identities are immutable; Target
 * hooks give ISA instruction widths and relaxation ranges. */

struct MiniAsLayoutState {
    MiniAsSubsection *subsections;
    size_t subsection_count, subsection_capacity;
    size_t *subsection_slots;
    size_t subsection_slot_capacity;
    struct MiniAsAnchorGraph *anchor_graph;
};

bool minias_layout_init(MiniAs *as) {
    if(as==NULL)return false;
    if(as->layout!=NULL) {
        minias_set_error(as,"layout-state-already-initialized");
        return false;
    }
    as->layout=calloc(1U,sizeof(*as->layout));
    if(as->layout==NULL) {
        minias_set_error(as,"out-of-memory:layout-state");
        return false;
    }
    return true;
}

/* One semantic contract between the source parser, MCLayout and emitter.
 * The sole FragmentKind owns all final layout/materialization decisions.
 * This check rejects incomplete typed payloads before emission. */
bool minias_validate_fragment_semantics(MiniAs *as) {
    size_t i;
    for(i=0U;i<as->stmt_count;++i) {
        const MiniAsStmt *stmt=&as->stmts[i];
        bool valid=false;
        switch(stmt->fragment_kind) {
        case MINIAS_FRAGMENT_INSTRUCTION:
            valid=stmt->payload.instruction.inst!=NULL;
            break;
        case MINIAS_FRAGMENT_ALIGNMENT:
            valid=stmt->payload.alignment.alignment!=0U &&
                  (stmt->payload.alignment.alignment&(stmt->payload.alignment.alignment-1U))==0U;
            break;
        case MINIAS_FRAGMENT_ORIGIN:
            valid=stmt->payload.origin.mode!=MINIAS_ORG_SOURCE &&
                  (stmt->payload.origin.mode!=MINIAS_ORG_TYPED_EXPR ||
                   stmt->payload.origin.expr!=NULL);
            break;
        case MINIAS_FRAGMENT_FIXED_DATA:
            valid=stmt->payload.fixed_data.data!=NULL;
            break;
        case MINIAS_FRAGMENT_FILL:
        case MINIAS_FRAGMENT_SPACE:
            valid=stmt->payload.variable.expr_count!=0U;
            break;
        case MINIAS_FRAGMENT_BYTES:
            valid=stmt->payload.bytes.data_size==(size_t)stmt->size &&
                  (stmt->size==0U || stmt->payload.bytes.data!=NULL) &&
                  (!stmt->payload.bytes.deferred_size ||
                   (stmt->payload.bytes.leb_mode!=0U &&
                    stmt->payload.bytes.leb_expr!=NULL));
            break;
        }
        if(!valid) {
            minias_set_error(as,
                "invalid-typed-fragment:kind=%u:line=%zu",
                (unsigned)stmt->fragment_kind,
                stmt->line);
            return false;
        }
    }
    return true;
}

/* The parse-time subsection table cannot use Section-name identity: its
 * key is the immutable SectionID plus the numbered GNU subsection. Layout
 * subsequently sorts the owned array, so the slot table is rebuilt in place
 * after that permutation and no old index/pointer may escape. */
static size_t subsection_key_hash(int section,uint32_t number) {
    uint64_t key=((uint64_t)(uint32_t)section<<32)|(uint64_t)number;
    key^=key>>33;
    key*=UINT64_C(0xff51afd7ed558ccd);
    key^=key>>33;
    key*=UINT64_C(0xc4ceb9fe1a85ec53);
    key^=key>>33;
    return (size_t)key;
}

static void subsection_index_fill(MiniAs *as) {
    size_t i;
    const size_t capacity=as->layout->subsection_slot_capacity;
    memset(as->layout->subsection_slots,0,capacity*sizeof(*as->layout->subsection_slots));
    for(i=0U;i<as->layout->subsection_count;++i) {
        const MiniAsSubsection *sub=&as->layout->subsections[i];
        size_t slot=subsection_key_hash(sub->section,sub->number)&(capacity-1U);
        while(as->layout->subsection_slots[slot]!=0U)
            slot=(slot+1U)&(capacity-1U);
        as->layout->subsection_slots[slot]=i+1U;
    }
}

static bool subsection_index_reserve(MiniAs *as) {
    size_t capacity=as->layout->subsection_slot_capacity;
    size_t *slots;
    if(capacity!=0U && as->layout->subsection_count+1U<capacity/2U)return true;
    if(capacity>SIZE_MAX/2U ||
       (capacity==0U ? 16U : capacity*2U)>SIZE_MAX/sizeof(*slots)) {
        minias_set_error(as,"subsection-index-too-large");
        return false;
    }
    capacity=capacity==0U ? 16U : capacity*2U;
    /* The index fill clears every slot before its first read. */
    slots=malloc(capacity*sizeof(*slots));
    if(slots==NULL) {
        minias_set_error(as,"out-of-memory:subsection-index");
        return false;
    }
    free(as->layout->subsection_slots);
    as->layout->subsection_slots=slots;
    as->layout->subsection_slot_capacity=capacity;
    subsection_index_fill(as);
    return true;
}

static size_t subsection_find_index(const MiniAsLayoutState *state,
                                    int section,uint32_t number) {
    size_t slot;
    const size_t capacity=state->subsection_slot_capacity;
    if(capacity==0U)return SIZE_MAX;
    slot=subsection_key_hash(section,number)&(capacity-1U);
    while(state->subsection_slots[slot]!=0U) {
        const size_t index=state->subsection_slots[slot]-1U;
        const MiniAsSubsection *sub=&state->subsections[index];
        if(sub->section==section && sub->number==number)return index;
        slot=(slot+1U)&(capacity-1U);
    }
    return SIZE_MAX;
}

const MiniAsSubsection *minias_layout_find_subsection(const MiniAs *as,
                                                      int section,
                                                      uint32_t number) {
    const MiniAsLayoutState *state=as!=NULL ? as->layout : NULL;
    size_t index;
    if(state==NULL)return NULL;
    index=subsection_find_index(state,section,number);
    return index==SIZE_MAX ? NULL : &state->subsections[index];
}

static MiniAsSubsection *minias_ensure_subsection(MiniAs *as,int section,
                                                  uint32_t number) {
    MiniAsSubsection *sub;
    size_t existing;
    size_t slot;
    if(as==NULL || as->layout==NULL) {
        if(as!=NULL)minias_set_error(as,"subsection-without-layout-state");
        return NULL;
    }
    existing=subsection_find_index(as->layout,section,number);
    if(existing!=SIZE_MAX)return &as->layout->subsections[existing];
    if(!subsection_index_reserve(as))return NULL;
    if(!minias_grow_array((void **)&as->layout->subsections,
                           &as->layout->subsection_capacity,
                           sizeof(*as->layout->subsections),
                           as->layout->subsection_count+1U)) {
        minias_set_error(as,"out-of-memory:subsection");
        return NULL;
    }
    sub=&as->layout->subsections[as->layout->subsection_count];
    memset(sub,0,sizeof(*sub));
    sub->section=section;
    sub->number=number;
    slot=subsection_key_hash(section,number)&(as->layout->subsection_slot_capacity-1U);
    while(as->layout->subsection_slots[slot]!=0U)
        slot=(slot+1U)&(as->layout->subsection_slot_capacity-1U);
    as->layout->subsection_slots[slot]=as->layout->subsection_count+1U;
    ++as->layout->subsection_count;
    return sub;
}

bool minias_layout_touch_subsection(MiniAs *as,int section,uint32_t number) {
    return minias_ensure_subsection(as,section,number)!=NULL;
}

bool minias_layout_subsection_parse_state(MiniAs *as,int section,
                                         uint32_t number,
                                         MiniAsSubsectionParseState *out) {
    MiniAsSubsection *sub;
    if(out==NULL) {
        if(as!=NULL)minias_set_error(as,"missing-subsection-parse-state");
        return false;
    }
    sub=minias_ensure_subsection(as,section,number);
    if(sub==NULL)return false;
    *out=(MiniAsSubsectionParseState){
        .cursor=sub->size,
        .pending_instruction_alignment=sub->pending_instruction_alignment
    };
    return true;
}

bool minias_layout_record_source_fragment(MiniAs *as,int section,
                                          uint32_t number,
                                          MiniAsFragmentKind kind) {
    MiniAsSubsection *sub;
    switch(kind) {
    case MINIAS_FRAGMENT_ALIGNMENT:
        return true;
    case MINIAS_FRAGMENT_INSTRUCTION:
    case MINIAS_FRAGMENT_ORIGIN:
    case MINIAS_FRAGMENT_FIXED_DATA:
    case MINIAS_FRAGMENT_FILL:
    case MINIAS_FRAGMENT_SPACE:
    case MINIAS_FRAGMENT_BYTES:
        break;
    default:
        if(as!=NULL)minias_set_error(as,"invalid-fragment-alignment-transition");
        return false;
    }
    sub=minias_ensure_subsection(as,section,number);
    if(sub==NULL)return false;
    sub->pending_instruction_alignment=kind!=MINIAS_FRAGMENT_INSTRUCTION;
    return true;
}

bool minias_layout_clear_instruction_alignment(MiniAs *as,int section,
                                               uint32_t number) {
    MiniAsSubsection *sub=minias_ensure_subsection(as,section,number);
    if(sub==NULL)return false;
    sub->pending_instruction_alignment=false;
    return true;
}

/* The parse-time location counter is a subsection cursor, not a Context
 * symbol/section-index property. MCLayout alone advances this live cursor. */
bool minias_current_location(MiniAs *as,uint64_t *value) {
    MiniAsSubsection *sub;
    if(as==NULL || value==NULL)return false;
    if(as->current_section<0 || (size_t)as->current_section>=as->section_count) {
        minias_set_error(as,"internal:bad-current-section");
        return false;
    }
    sub=minias_ensure_subsection(as,as->current_section,as->current_subsection);
    if(sub==NULL)return false;
    *value=sub->size;
    return true;
}

bool advance_current_location(MiniAs *as,uint64_t amount) {
    MiniAsSubsection *sub;
    if(as==NULL)return false;
    sub=minias_ensure_subsection(as,as->current_section,as->current_subsection);
    if(sub==NULL)return false;
    if(amount>UINT64_MAX-sub->size) {
        minias_set_error(as,"subsection-size-overflow");
        return false;
    }
    sub->size+=amount;
    return true;
}

static int compare_subsections(const void *lhs_ptr, const void *rhs_ptr) {
    const MiniAsSubsection *lhs = lhs_ptr;
    const MiniAsSubsection *rhs = rhs_ptr;

    if (lhs->section != rhs->section) {
        return lhs->section < rhs->section ? -1 : 1;
    }
    if (lhs->number != rhs->number) {
        return lhs->number < rhs->number ? -1 : 1;
    }
    return 0;
}

/* During layout the subsection array is sorted by its immutable key;
 * binary lookup remains O(log N) and does not depend on hash-table state. */
static MiniAsSubsection *find_canonical_subsection(const MiniAs *as,
                                                   int section,
                                                   uint32_t number) {
    size_t lo = 0U, hi = as->layout->subsection_count;
    while (lo < hi) {
        size_t mid = lo + (hi-lo)/2U;
        MiniAsSubsection *sub = &as->layout->subsections[mid];
        if (sub->section < section ||
            (sub->section == section && sub->number < number))
            lo = mid+1U;
        else hi = mid;
    }
    if (lo < as->layout->subsection_count &&
        as->layout->subsections[lo].section == section &&
        as->layout->subsections[lo].number == number)
        return &as->layout->subsections[lo];
    return NULL;
}

/* Context owns the persistent Symbol state; MCLayout lends it only the
 * current canonical subsection base as a scalar.  No mutable subsection
 * record or private AnchorGraph escapes the owning layout translation unit. */
static bool layout_symbol_subsection_base(void *owner,int section,
                                          uint32_t number,uint64_t *base) {
    const MiniAs *as=(const MiniAs *)owner;
    const MiniAsSubsection *sub;
    if(as==NULL || base==NULL || as->layout==NULL)return false;
    sub=find_canonical_subsection(as,section,number);
    if(sub==NULL)return false;
    *base=sub->base;
    return true;
}

bool minias_finalize_subsection_layout(MiniAs *as) {
    size_t i;
    int active_section = -1;
    uint64_t cursor = 0U;
    uint64_t *section_layout_sizes=NULL;

    /* Measure one complete SectionID-indexed snapshot before publishing it.
     * This is transient MCLayout scratch, not a second persistent graph. */
    if(as->section_count!=0U) {
        if(as->section_count>SIZE_MAX/sizeof(*section_layout_sizes)) {
            minias_set_error(as,"section-layout-overflow");
            return false;
        }
        section_layout_sizes=calloc(as->section_count,
                                    sizeof(*section_layout_sizes));
        if(section_layout_sizes==NULL) {
            minias_set_error(as,"out-of-memory:section-layout-snapshot");
            return false;
        }
    }

    if (as->layout->subsection_count != 0U) {
        qsort(as->layout->subsections,
              as->layout->subsection_count,
              sizeof(*as->layout->subsections),
              compare_subsections);
        /* qsort permutes array indices; refresh every parser hash slot
         * before any later consumer can request a subsection. */
        if(as->layout->subsection_slot_capacity!=0U)subsection_index_fill(as);
    }
    for (i = 0U; i < as->layout->subsection_count; ++i) {
        MiniAsSubsection *sub = &as->layout->subsections[i];

        if(sub->section<0 || (size_t)sub->section>=as->section_count) {
            minias_set_error(as,"internal:subsection-section-id");
            goto failed;
        }

        if (sub->section != active_section) {
            if (active_section >= 0) {
                section_layout_sizes[(size_t)active_section]=cursor;
            }
            active_section = sub->section;
            cursor = 0U;
        }
        sub->base = cursor;
        if (sub->size > UINT64_MAX - cursor) {
            minias_set_error(as, "section-layout-overflow");
            goto failed;
        }
        cursor += sub->size;
    }
    if (active_section >= 0) {
        section_layout_sizes[(size_t)active_section]=cursor;
    }

    for (i = 0U; i < as->stmt_count; ++i) {
        MiniAsStmt *stmt = &as->stmts[i];
        MiniAsSubsection *sub =
            find_canonical_subsection(as, stmt->section, stmt->subsection);
        if (sub == NULL || stmt->offset > UINT64_MAX - sub->base) {
            minias_set_error(as, "internal:stmt-subsection-layout");
            goto failed;
        }
        stmt->offset += sub->base;
    }
    /* One Context publication normalizes all source SymbolIDs, refreshes
     * their alias DAG, resolves cross-subsection .size end anchors, and
     * commits the complete Section sizes. Neither a partial Symbol update
     * nor a Section size snapshot is observable on the succeeding pass. */
    const MiniAsSubsectionBaseReader bases={
        .owner=as,
        .base=layout_symbol_subsection_base
    };
    if(!minias_common_context_finalize_symbol_layout(
            as,&bases,section_layout_sizes,as->section_count))goto failed;
    free(section_layout_sizes);
    return true;
failed:
    free(section_layout_sizes);
    return false;
}



/* Stable indexed MCFragment graph. GNU subsections are sorted by number at
 * parse completion. Cache their identity ONCE, never redo a linear search for
 * every symbol and statement in every relaxation pass. Source ordinals and
 * symbol indices remain immutable even when macros expand. */
typedef struct MiniAsAnchorGraph {
    size_t *start_head, *end_head;
    size_t *start_next, *end_next;
    size_t *statement_subsection, *symbol_subsection,*end_subsection;
    /* Per-pass subsection-local candidate coordinates. Kept separate from
     * MiniAsSymbol until the entire Fragment pass commits atomically, so
     * `.org` sees one coherent previous Section-relative symbol snapshot. */
    uint64_t *start_local, *end_local, *size_candidate;
    uint64_t *candidate_base;
    uint32_t *org_next_size;
    /* Derived from the pass-local growth worklist, never a session-wide
     * source identity or a second persistent SourceGraph branch registry. */
    unsigned char *branch_membership;
    size_t stmt_count, symbol_count, subsection_count, relayout_count;
} MiniAsAnchorGraph;

static void free_anchor_graph(MiniAsAnchorGraph *graph) {
    if (graph == NULL) return;
    free(graph->start_head);
    free(graph->end_head);
    free(graph->start_next);
    free(graph->end_next);
    free(graph->statement_subsection);
    free(graph->symbol_subsection);
    free(graph->end_subsection);
    free(graph->start_local);
    free(graph->end_local);
    free(graph->size_candidate);
    free(graph->candidate_base);
    free(graph->org_next_size);
    free(graph->branch_membership);
    free(graph);
}

void minias_layout_destroy(MiniAs *as) {
    if(as==NULL || as->layout==NULL)return;
    free_anchor_graph(as->layout->anchor_graph);
    free(as->layout->subsections);
    free(as->layout->subsection_slots);
    free(as->layout);
    as->layout=NULL;
}

/* MCLayout is the only owner of symbol anchors and Section identity.
 * Every allocation/validation failure abandons the graph atomically. */
static MiniAsAnchorGraph *build_anchor_graph(MiniAs *as) {
    MiniAsAnchorGraph *graph = (MiniAsAnchorGraph *)as->layout->anchor_graph;
    const size_t n = as->stmt_count;
    const size_t symbols = as->symbol_count;
    const size_t subsections = as->layout->subsection_count;
    size_t i;

    if (graph != NULL) {
        if (graph->stmt_count != n || graph->symbol_count != symbols ||
            graph->subsection_count != subsections) {
            minias_set_error(as, "fragment-index-cardinality-mutated");
            return NULL;
        }
        return graph;
    }
    if (n == SIZE_MAX || n + 1U > SIZE_MAX / sizeof(size_t) ||
        symbols > SIZE_MAX / sizeof(size_t) ||
        symbols > SIZE_MAX / sizeof(uint64_t) ||
        subsections > SIZE_MAX / sizeof(size_t)) {
        minias_set_error(as, "fragment-index-capacity-overflow");
        return NULL;
    }
    graph = calloc(1U, sizeof(*graph));
    if (graph == NULL) {
        minias_set_error(as, "out-of-memory:anchor-graph");
        return NULL;
    }
    graph->stmt_count = n;
    graph->symbol_count = symbols;
    graph->subsection_count = subsections;
    graph->start_head = malloc((n + 1U) * sizeof(size_t));
    graph->end_head = malloc((n + 1U) * sizeof(size_t));
    graph->start_next = malloc((symbols ? symbols : 1U) * sizeof(size_t));
    graph->end_next = malloc((symbols ? symbols : 1U) * sizeof(size_t));
    graph->statement_subsection = malloc((n ? n : 1U) * sizeof(size_t));
    graph->symbol_subsection =
        malloc((symbols ? symbols : 1U) * sizeof(size_t));
    graph->end_subsection =
        malloc((symbols ? symbols : 1U) * sizeof(size_t));
    graph->start_local = malloc((symbols ? symbols : 1U) * sizeof(uint64_t));
    graph->end_local = malloc((symbols ? symbols : 1U) * sizeof(uint64_t));
    graph->size_candidate = malloc((symbols ? symbols : 1U) * sizeof(uint64_t));
    graph->candidate_base = malloc((subsections ? subsections : 1U) *
                                   sizeof(uint64_t));
    graph->org_next_size = malloc((n ? n : 1U) * sizeof(uint32_t));
    if (graph->start_head == NULL || graph->end_head == NULL ||
        graph->start_next == NULL || graph->end_next == NULL ||
        graph->statement_subsection == NULL ||
        graph->symbol_subsection == NULL ||
        graph->end_subsection == NULL ||
        graph->start_local == NULL || graph->end_local == NULL ||
        graph->size_candidate == NULL ||
        graph->candidate_base == NULL ||
        graph->org_next_size == NULL) {
        minias_set_error(as, "out-of-memory:fragment-index-storage");
        goto fail;
    }

    /* Subsection indexes are stable iff the parser already canonicalized
     * them. Reject duplicates, invalid Sections and accidental reorder. */
    for (i = 0U; i < subsections; ++i) {
        const MiniAsSubsection *sub = &as->layout->subsections[i];
        if (sub->section < 0 || (size_t)sub->section >= as->section_count ||
            (i != 0U &&
             compare_subsections(&as->layout->subsections[i-1U], sub) >= 0)) {
            minias_set_error(as, "fragment-index-noncanonical-subsection");
            goto fail;
        }
    }
    for (i = 0U; i <= n; ++i) {
        graph->start_head[i] = SIZE_MAX;
        graph->end_head[i] = SIZE_MAX;
    }
    for (i = 0U; i < n; ++i) {
        const MiniAsStmt *stmt = &as->stmts[i];
        MiniAsSubsection *sub =
            find_canonical_subsection(as, stmt->section,
                                      stmt->subsection);
        if (sub == NULL) {
            minias_set_error(as, "fragment-index-missing-stmt-subsection:%zu",
                             stmt->line);
            goto fail;
        }
        graph->statement_subsection[i] = (size_t)(sub - as->layout->subsections);
    }
    for (i = 0U; i < symbols; ++i) {
        const MiniAsSymbol *sym = &as->symbols[i];
        MiniAsSubsection *sub = NULL;
        MiniAsSubsection *end_sub = NULL;
        graph->start_next[i] = SIZE_MAX;
        graph->end_next[i] = SIZE_MAX;
        graph->symbol_subsection[i] = SIZE_MAX;
        graph->end_subsection[i] = SIZE_MAX;
        if (sym->defined && sym->section >= 0)
            sub = find_canonical_subsection(as, sym->section,
                                            sym->subsection);
        if (sub != NULL)
            graph->symbol_subsection[i] = (size_t)(sub - as->layout->subsections);
        if (sym->has_size_end_anchor)
            end_sub = find_canonical_subsection(as, sym->section,
                                                 sym->size_end_subsection);
        if (end_sub != NULL)
            graph->end_subsection[i] = (size_t)(end_sub - as->layout->subsections);
        if (sym->has_fragment_anchor && sym->defined && sym->section >= 0) {
            if (sym->fragment_anchor > n || sub == NULL) {
                minias_set_error(as, "fragment-index-invalid-start:%s",
                                 sym->name);
                goto fail;
            }
            graph->start_next[i] = graph->start_head[sym->fragment_anchor];
            graph->start_head[sym->fragment_anchor] = i;
        }
        if (sym->has_size_end_anchor) {
            if (sym->size_end_anchor > n || end_sub == NULL ||
                !sym->has_fragment_anchor) {
                minias_set_error(as, "fragment-index-invalid-end:%s",
                                 sym->name);
                goto fail;
            }
            graph->end_next[i] = graph->end_head[sym->size_end_anchor];
            graph->end_head[sym->size_end_anchor] = i;
        }
    }
    as->layout->anchor_graph = graph;
    return graph;
fail:
    free_anchor_graph(graph);
    return NULL;
}

/* Keep signed MCExpr addends in their semantic domain.  Converting a
 * negative displacement to uint64_t before checking is not a range check. */
static bool add_signed_u64(uint64_t base, int64_t addend, uint64_t *result) {
    if (addend >= 0) {
        uint64_t delta = (uint64_t)addend;
        if (delta > UINT64_MAX - base) return false;
        *result = base + delta;
    } else {
        uint64_t delta = (uint64_t)(-(addend + 1)) + 1U;
        if (delta > base) return false;
        *result = base - delta;
    }
    return true;
}

/* GNU .org is relative to the output Section start, even inside a
 * numbered .subsection.  Like LLVM MCOrgFragment, the final size is a
 * function of the Fragment's location and retained expression.  During
 * relayout an earlier Symbol anchor is subsection-local; after final
 * layout it is section-relative.  Neither case re-reads source text. */
static bool org_fragment_padding(MiniAs *as, const MiniAsStmt *stmt,
                                 uint64_t location, uint32_t *padding) {
    uint64_t target = 0U;
    uint64_t gap;
    if (stmt->payload.origin.mode == MINIAS_ORG_ABSOLUTE) {
        target = stmt->payload.origin.target;
    } else if (stmt->payload.origin.mode == MINIAS_ORG_DOT_ADDEND) {
        if (stmt->payload.origin.target > UINT64_MAX - location) {
            minias_set_error(as,"org-dot-overflow:line=%zu",stmt->line);
            return false;
        }
        target = location + stmt->payload.origin.target;
    } else if (stmt->payload.origin.mode == MINIAS_ORG_SYMBOL_ADDEND) {
        const MiniAsSymbol *symbol;
        const MiniAsSubsection *sub;
        uint64_t value;
        if (stmt->payload.origin.symbol_index >= as->symbol_count) {
            minias_set_error(as,"org-symbol-index-invalid:line=%zu",stmt->line);
            return false;
        }
        symbol = &as->symbols[stmt->payload.origin.symbol_index];
        sub = find_canonical_subsection(as,symbol->section,
                                        symbol->subsection);
        if (sub == NULL || !symbol->defined ||
            !symbol->has_fragment_anchor ||
            symbol->section != stmt->section ||
            symbol->subsection > stmt->subsection) {
            minias_set_error(as,"org-symbol-owner-invalid:line=%zu",stmt->line);
            return false;
        }
        /* MCLayout reads only a coherent Section-relative SymbolID snapshot.
         * New anchor candidates stay private until this pass commits. */
        value = symbol->value;
        if (!add_signed_u64(value,stmt->payload.origin.addend,&target)) {
            minias_set_error(as,"org-symbol-addend-overflow:line=%zu",stmt->line);
            return false;
        }
    } else if (stmt->payload.origin.mode == MINIAS_ORG_TYPED_EXPR &&
               stmt->payload.origin.expr != NULL) {
        /* A retained, stable SymbolID expression. Its dot is supplied
         * explicitly by MCLayout: there is no source reparsing and no
         * temporary mutation of lexical parser state during fixed point. */
        int64_t resolved=0;
        if(location>(uint64_t)INT64_MAX) {
            minias_set_error(as,"org-location-overflow:line=%zu",stmt->line);
            return false;
        }
        if(!minias_evaluate_deferred_expr(as,stmt->payload.origin.expr,
                                         location,stmt->section,true,&resolved) ||
           resolved<0) {
            minias_set_error(as,"org-expression-unresolved:line=%zu",stmt->line);
            return false;
        }
        target=(uint64_t)resolved;
    } else {
        minias_set_error(as,"internal:org-untyped:line=%zu",stmt->line);
        return false;
    }
    if (target < location) {
        minias_set_error(as,
            "org-backwards-after-layout:line=%zu:current=%llu:target=%llu",
            stmt->line,(unsigned long long)location,
            (unsigned long long)target);
        return false;
    }
    gap = target - location;
    if (gap > UINT32_MAX) {
        minias_set_error(as,"org-gap-too-large-after-layout:line=%zu",stmt->line);
        return false;
    }
    *padding = (uint32_t)gap;
    return true;
}

/* Rebuild one coherent Section-relative SymbolID snapshot for the CURRENT
 * Fragment sizes. This pass may recompute native alignment but deliberately
 * does NOT reevaluate `.org`: every `.org` expression must later observe
 * symbols and locations from the same completed snapshot, even when a
 * previous branch expanded or an earlier subsection was resized. */
static bool minias_scan_fragment_layout(MiniAs *as, bool *placement_changed) {
    MiniAsAnchorGraph *graph = build_anchor_graph(as);
    size_t i;
    if (graph == NULL) return false;
    *placement_changed = false;
    ++graph->relayout_count;
    for (i = 0U; i < graph->subsection_count; ++i)
        as->layout->subsections[i].size = 0U;

    for (i = 0U; i <= graph->stmt_count; ++i) {
        size_t symbol;
        for (symbol = graph->start_head[i]; symbol != SIZE_MAX;
             symbol = graph->start_next[symbol]) {
            const size_t subsection = graph->symbol_subsection[symbol];
            if (subsection >= graph->subsection_count) {
                minias_set_error(as, "internal:fragment-start-subsection");
                return false;
            }
            /* Jacobi-style relayout: staged subsection-local candidates
             * cannot mutate the previous Section-relative symbols seen by
             * expressions and aliases in later statements of this pass. */
            if (!add_signed_u64(as->layout->subsections[subsection].size,
                                as->symbols[symbol].fragment_anchor_addend,
                                &graph->start_local[symbol])) {
                minias_set_error(as,"fragment-anchor-addend-overflow:%s",
                                 as->symbols[symbol].name);
                return false;
            }
        }
        for (symbol = graph->end_head[i]; symbol != SIZE_MAX;
             symbol = graph->end_next[symbol]) {
            const size_t subsection = graph->end_subsection[symbol];
            if (subsection >= graph->subsection_count) {
                minias_set_error(as, "internal:fragment-end-subsection");
                return false;
            }
            if(!add_signed_u64(as->layout->subsections[subsection].size,
                               as->symbols[symbol].size_end_addend,
                               &graph->end_local[symbol])) {
                minias_set_error(as,"fragment-size-addend-overflow:%s",
                                 as->symbols[symbol].name);
                return false;
            }
        }
        if (i < graph->stmt_count) {
            MiniAsStmt *stmt = &as->stmts[i];
            const size_t subsection = graph->statement_subsection[i];
            MiniAsSubsection *sub;
            if (subsection >= graph->subsection_count) {
                minias_set_error(as, "internal:fragment-stmt-subsection");
                return false;
            }
            sub = &as->layout->subsections[subsection];
            if (sub->section != stmt->section ||
                sub->number != stmt->subsection) {
                minias_set_error(as, "fragment-subsection-identity-mutated");
                return false;
            }
            stmt->offset = sub->size;
            if (stmt->fragment_kind == MINIAS_FRAGMENT_ALIGNMENT) {
                uint64_t pad;
                if(!fragment_alignment_padding(stmt,sub->size,&pad)) {
                    minias_set_error(as, "bad-fragment-alignment");
                    return false;
                }
                if (pad > UINT32_MAX) {
                    minias_set_error(as, "fragment-alignment-overflow");
                    return false;
                }
                if(stmt->size!=(uint32_t)pad)
                    *placement_changed=true;
                stmt->size = (uint32_t)pad;
            }
            if ((uint64_t)stmt->size > UINT64_MAX - sub->size) {
                minias_set_error(as, "fragment-size-overflow");
                return false;
            }
            sub->size += stmt->size;
        }
    }
    /* Determine final candidate subsection bases BEFORE subtracting the
     * retained .size start/end anchors. They may belong to two different
     * numbered subsections of one ELF Section, and local offsets cannot be
     * subtracted until each has been translated into Section coordinates. */
    {
        int active_section=-1;
        uint64_t base=0U;
        for(i=0U;i<graph->subsection_count;++i) {
            const MiniAsSubsection *sub=&as->layout->subsections[i];
            if(sub->section!=active_section) {
                active_section=sub->section;
                base=0U;
            }
            graph->candidate_base[i]=base;
            if(sub->size>UINT64_MAX-base) {
                minias_set_error(as,"section-layout-overflow");
                return false;
            }
            base+=sub->size;
        }
    }
    for (i = 0U; i < graph->symbol_count; ++i) {
        const MiniAsSymbol *symbol = &as->symbols[i];
        size_t start_idx,end_idx,source;
        uint64_t start,end;
        if (!symbol->has_size_end_anchor) continue;
        source=symbol->has_size_start_symbol?
               symbol->size_start_symbol_index:i;
        if(source>=graph->symbol_count) {
            minias_set_error(as,"fragment-size-start-index:%s",symbol->name);
            return false;
        }
        start_idx=graph->symbol_subsection[source];
        end_idx=graph->end_subsection[i];
        if (!symbol->has_fragment_anchor ||
            start_idx>=graph->subsection_count ||
            end_idx>=graph->subsection_count ||
            graph->start_local[source]>
                UINT64_MAX-graph->candidate_base[start_idx] ||
            graph->end_local[i]>
                UINT64_MAX-graph->candidate_base[end_idx]) {
            minias_set_error(as, "fragment-size-location-mismatch:%s",
                             symbol->name);
            return false;
        }
        start=graph->candidate_base[start_idx]+graph->start_local[source];
        end=graph->candidate_base[end_idx]+graph->end_local[i];
        if(end<start) {
            minias_set_error(as, "fragment-size-location-mismatch:%s",
                             symbol->name);
            return false;
        }
        graph->size_candidate[i] = end-start;
    }
    /* Previous subsection bases may differ when an earlier DATA/branch
     * grew. The next fixed-point pass must reevaluate every retained `.org`
     * with the freshly normalized Section-relative symbol snapshot. */
    for(i=0U;i<as->layout->subsection_count;++i) {
        const MiniAsSubsection *sub=&as->layout->subsections[i];
        if(sub->base!=graph->candidate_base[i])
            *placement_changed=true;
    }
    /* Commit all staged anchors together. The public finalizer also serves
     * the parser and expects subsection-local values; it adds the NEW base
     * once and refreshes every retained SymbolID alias dependency. */
    const MiniAsSymbolLocalAnchorPlan anchors={
        .symbol_count=graph->symbol_count,
        .start_local=graph->start_local,
        .end_local=graph->end_local,
        .size_candidate=graph->size_candidate
    };
    if(!minias_common_context_commit_local_symbol_anchors(as,&anchors))
        return false;
    return minias_finalize_subsection_layout(as);
}

/* Two-phase persistent MCLayout fixed point:
 *   (1) rebuild ALL symbol/alias anchors and subsection bases from current
 *       Fragment widths, including already-expanded target branches;
 *   (2) evaluate ALL typed .org constraints against that immutable current
 *       Section-relative snapshot, staging every new padding width;
 *   (3) commit the padding batch, then rebuild anchors once more.
 *
 * No `.org` sees an earlier `.org`'s new padding alongside a later symbol's
 * old address. In particular `label: .org label+2` stays valid if a branch
 * before label expands in this iteration: both its location and SymbolID
 * move together BEFORE the position constraint is inspected. */
static bool minias_relayout_fragments(MiniAs *as, bool *org_changed) {
    MiniAsAnchorGraph *graph;
    size_t i;
    bool changed=false, placement_changed=false;

    if(!minias_scan_fragment_layout(as,&placement_changed))return false;
    graph=(MiniAsAnchorGraph *)as->layout->anchor_graph;
    if(graph==NULL) {
        minias_set_error(as,"internal:missing-relayout-graph");
        return false;
    }
    for(i=0U;i<graph->stmt_count;++i) {
        const MiniAsStmt *stmt=&as->stmts[i];
        if(stmt->fragment_kind!=MINIAS_FRAGMENT_ORIGIN)continue;
        if(!org_fragment_padding(as,stmt,stmt->offset,
                                 &graph->org_next_size[i]))return false;
    }
    for(i=0U;i<graph->stmt_count;++i) {
        MiniAsStmt *stmt=&as->stmts[i];
        if(stmt->fragment_kind!=MINIAS_FRAGMENT_ORIGIN)continue;
        if(stmt->size!=graph->org_next_size[i]) {
            stmt->size=graph->org_next_size[i];
            changed=true;
        }
    }
    if(changed) {
        bool resized_placement=false;
        if(!minias_scan_fragment_layout(as,&resized_placement))return false;
        placement_changed|=resized_placement;
    }
    if(org_changed!=NULL)*org_changed=changed || placement_changed;
    return true;
}


/* Resolve Common variable-size fragments with the exact post-subsection
 * symbol addresses.  Re-run shared Fragment/Layout ownership until the
 * sizes stop changing.  This handles GNU forward numeric labels and
 * expressions referencing later subsections without backend-specific
 * preprocessor passes, speculative dummy symbols, or unknown-opcode skips.
 *
 * The parser already has every source symbol at this point.  A reference
 * to a truly undefined label must remain an error, never a zero fill. */
static bool minias_resolve_deferred_data(MiniAs *as) {
    MiniAsLoweringLayoutPlan plan;
    size_t pass;
    if(!minias_common_lowering_layout_plan(as,&plan)) {
        minias_set_error(as,"layout-without-lowering-plan");
        return false;
    }
    if(!plan.has_deferred_data && !plan.has_org_constraints &&
       !plan.has_cross_subsection_size_end)return true;
    /* .org, .fill, .space and symbolic LEB share a single fixed point.
     * A subsection base may change as another numbered subsection grows, so resolving
     * .org only once would produce wrong final section-relative offsets. */
    for(pass=0U;pass<=as->stmt_count;++pass) {
        size_t i;
        bool changed=false;
        bool org_changed=false;
        if(!minias_relayout_fragments(as,&org_changed) ||
           !minias_debug_resolve_views(as,false))return false;
        for(i=0U;i<as->stmt_count;++i) {
            MiniAsStmt *stmt=&as->stmts[i];
            uint64_t n=0U;
            if(!fragment_has_deferred_data(stmt))continue;
            if(stmt->fragment_kind==MINIAS_FRAGMENT_BYTES) {
                int64_t value;
                unsigned char encoded[10];
                size_t length=0U;
                if(stmt->payload.bytes.leb_mode==0U ||
                   stmt->payload.bytes.leb_mode>2U ||
                   stmt->payload.bytes.leb_expr==NULL ||
                   !minias_evaluate_deferred_expr(as,stmt->payload.bytes.leb_expr,
                                                  stmt->offset,stmt->section,
                                                  false,&value) ||
                   !minias_encode_leb_integer(value,stmt->payload.bytes.leb_mode==2U,
                                              encoded,&length) ||
                   stmt->payload.bytes.data==NULL) {
                    minias_set_error(as,"unresolved-typed-leb-data:line=%zu",
                                     stmt->line);
                    return false;
                }
                memcpy(stmt->payload.bytes.data,encoded,length);
                stmt->payload.bytes.data_size=length;
                n=(uint64_t)length;
            }else {
                uint64_t repeat=0U,bits=0U;
                unsigned width=0U;
                if(!minias_resolve_variable_exprs(as,stmt,&repeat,&width,&bits) ||
                   (width!=0U && repeat>UINT32_MAX/width)) {
                    minias_set_error(as,"unresolved-typed-variable-data:line=%zu",
                                     stmt->line);
                    return false;
                }
                n=repeat*width;
                stmt->payload.variable.repeat=repeat;
                stmt->payload.variable.element_width=width;
                stmt->payload.variable.value=bits;
                stmt->payload.variable.resolved=true;
            }
            if(n!=(uint64_t)stmt->size) {
                stmt->size=(uint32_t)n;
                changed=true;
            }
        }
        if(!changed && !org_changed) {
            return true;
        }
    }
    minias_set_error(as,"mc-fragment-layout-nonconvergent");
    return false;
}

/* GNU link relaxation can change the distance between two already-defined
 * anchors. TargetInfo identifies which emitted relocation *permits* code
 * shrinking; Common checks the final fragment-owned fixup graph and stable
 * anchor values. CFI and ULEB must share this predicate, including source
 * subsections whose serialized order differs from parser encounter order. */
bool minias_span_has_linker_relaxation(MiniAs *as,size_t from_symbol,
                                      size_t to_symbol,bool *crosses) {
    const MiniAsSymbol *from,*to;
    uint64_t begin,end;
    size_t i;
    const MiniAsFixupSpan fixups=minias_emission_fixups(as);
    if(as==NULL || crosses==NULL || from_symbol>=as->symbol_count ||
       to_symbol>=as->symbol_count)return false;
    *crosses=false;
    from=&as->symbols[from_symbol];
    to=&as->symbols[to_symbol];
    if(!from->defined || !to->defined || from->section<0 ||
       from->section!=to->section)return false;
    if(as->target->cfi_linker_relaxation_marker==NULL)return true;
    begin=from->value<to->value?from->value:to->value;
    end=from->value>to->value?from->value:to->value;
    for(i=0U;i<fixups.count;++i) {
        const MiniAsFixup *fixup=&fixups.data[i];
        uint64_t site;
        if(fixup->section!=from->section ||
           !as->target->cfi_linker_relaxation_marker(fixup->kind))continue;
        if(fixup->origin==MINIAS_FIXUP_ORIGIN_FRAGMENT) {
            const MiniAsStmt *owner;
            if(fixup->fragment_id>=as->stmt_count)return false;
            owner=&as->stmts[fixup->fragment_id];
            if(owner->section!=fixup->section ||
               owner->offset>UINT64_MAX-fixup->fragment_offset)return false;
            site=owner->offset+fixup->fragment_offset;
        }else site=fixup->offset;
        if(site>=begin && site<end) {
            *crosses=true;
            return true;
        }
    }
    return true;
}

/* Called after *all* Target instructions have been encoded, because a
 * linker-relaxable MCInst may occur after its `.uleb128 end-start` source
 * directive (or in a section that was parsed later). All operands here are
 * stable AST SymbolIDs, not a replay of the GNU source spelling. */
bool minias_emit_deferred_leb_relocations(MiniAs *as) {
    const unsigned mode=as->target->leb128_reloc_mode;
    size_t i;
    if(mode==0U)return true;
    if(as->target->leb128_relocations_enabled!=NULL &&
       !as->target->leb128_relocations_enabled(minias_target_abi_view(as)))return true;
    if(mode>3U) {
        minias_set_error(as,"invalid-target-uleb-relocation-policy");
        return false;
    }
    for(i=0U;i<as->stmt_count;++i) {
        const MiniAsStmt *stmt=&as->stmts[i];
        const MiniAsSymbol *positive,*negative;
        size_t pos_id,neg_id;
        int64_t addend;
        bool crosses=false;
        MiniAsFixupKind positive_fixup=mode==2U ?
            MINIAS_FIXUP_LEB_SET:MINIAS_FIXUP_LEB_ADD;
        if(stmt->fragment_kind!=MINIAS_FRAGMENT_BYTES ||
           stmt->payload.bytes.leb_mode!=1U ||
           !stmt->payload.bytes.deferred_size ||
           stmt->payload.bytes.leb_expr==NULL ||
           !minias_deferred_symbol_difference(stmt->payload.bytes.leb_expr,
                                               &pos_id,&neg_id,&addend))
            continue;
        if(pos_id>=as->symbol_count || neg_id>=as->symbol_count)return false;
        positive=&as->symbols[pos_id];
        negative=&as->symbols[neg_id];
        /* The Common LEB byte solver has already rejected unrelated or
         * undefined symbols; the ELF difference must retain that invariant.
         * A separate future target contract may admit cross-section pairs. */
        if(!positive->defined || !negative->defined ||
           positive->section<0 ||
           positive->section!=negative->section) {
            minias_set_error(as,"invalid-uleb-relocation-symbols:line=%zu",
                             stmt->line);
            return false;
        }
        if(mode==1U) {
            if(!minias_span_has_linker_relaxation(as,neg_id,pos_id,&crosses))
                return false;
            if(!crosses)continue;
        }
        if(as->target->supports_fixup==NULL ||
           !as->target->supports_fixup(minias_target_abi_view(as),positive_fixup,0U) ||
           !as->target->supports_fixup(minias_target_abi_view(as),MINIAS_FIXUP_LEB_SUB,0U) ||
           stmt->section<0 || (size_t)stmt->section>=as->section_count) {
            minias_set_error(as,"unsupported-uleb-relocation:line=%zu",
                             stmt->line);
            return false;
        }
        size_t emitted=minias_emission_section_size(as,stmt->section);
        if(stmt->size==0U || stmt->offset>emitted ||
           stmt->size>emitted-stmt->offset) {
            minias_set_error(as,"uleb-relocation-field-overflow:line=%zu",
                             stmt->line);
            return false;
        }
        if(!minias_add_late_fragment_fixup(as,i,positive_fixup,pos_id,addend) ||
           !minias_add_late_fragment_fixup(as,i,MINIAS_FIXUP_LEB_SUB,
                                          neg_id,0))return false;
        /* GNU RISC-V GAS keeps the resolved value in the ULEB payload even
         * when emitting R_RISCV_SET_ULEB128/SUB_ULEB128.  Only LoongArch's
         * ADD/SUB relocation policy clears the payload while retaining the
         * LEB continuation-bit width.  Applying that rule to RISC-V changes
         * the original object bytes (e.g. 129 -> 0) before GNU ld sees it. */
        if(mode==3U && !minias_emission_clear_leb_payload(
                as,stmt->section,stmt->offset,stmt->size))return false;
    }
    return true;
}

struct MiniAsRelaxContext { MiniAs *as; };

bool minias_relax_symbol_ref_lookup(MiniAsRelaxContext *ctx,
                                    MiniAsSymbolRef ref,
                                    MiniAsSymbolView *out) {
    return minias_common_context_symbol_ref_view(ctx->as,ref,out);
}

bool minias_relax_resolve_symbol_difference_ref(
    MiniAsRelaxContext *ctx, const MiniAsSymbolDifferenceRef *pair,
    int64_t *value) {
    return minias_resolve_symbol_difference_ref(ctx->as, pair, value);
}

void minias_relax_error(MiniAsRelaxContext *ctx, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    minias_set_error_va(ctx->as, format, ap);
    va_end(ap);
}

/* The sole Common validation gate for Target encoding alternatives.
 * Source replay and MCLayout both ask the same retained typed MCInst;
 * neither side reparses a mnemonic or guesses its terminal instruction size. */
bool minias_query_target_relax_plan(MiniAs *as,const MiniAsStmt *stmt,
                                   MiniAsTargetRelaxPlan *plan) {
    MiniAsRelaxContext context={.as=as};
    if(as==NULL || stmt==NULL || plan==NULL ||
       stmt->fragment_kind!=MINIAS_FRAGMENT_INSTRUCTION ||
       stmt->payload.instruction.inst==NULL) {
        if(as!=NULL)minias_set_error(as,"invalid-target-relaxation-owner");
        return false;
    }
    plan->current_size=stmt->size;
    plan->required_size=stmt->size;
    plan->terminal_size=stmt->size;
    if(as->target->plan_instruction_relaxation!=NULL) {
        const MiniAsMCInst *source=stmt->payload.instruction.inst;
        const size_t count=source->count<=source->capacity?source->count:0U;
        MiniAsMCOperandView operands[count?count:1U];
        MiniAsMCInstView instruction;
        MiniAsInstructionView snapshot;
        if(!minias_project_instruction(stmt,&snapshot,&instruction,
                                       operands,count)) {
            minias_set_error(as,"invalid-relax-instruction:line=%zu",stmt->line);
            return false;
        }
        if(!as->target->plan_instruction_relaxation(&context,&snapshot,plan)) {
            if(as->error[0]=='\0')
                minias_set_error(as,"target-relaxation-plan-failed:line=%zu",
                                 stmt->line);
            return false;
        }
    }
    if(plan->current_size!=stmt->size ||
       plan->required_size<plan->current_size ||
       plan->terminal_size<plan->required_size ||
       plan->terminal_size==0U) {
        minias_set_error(as,"invalid-target-relaxation-plan:line=%zu",
                         stmt->line);
        return false;
    }
    return true;
}

static bool collect_out_of_range_branches(MiniAs *as,
                                          MiniAsFragmentGrowthSet *growth,
                                          bool *added) {
    MiniAsAnchorGraph *graph=build_anchor_graph(as);
    size_t i,j;
    *added=false;
    if(graph==NULL)return false;
    if(graph->branch_membership==NULL) {
        graph->branch_membership=
            calloc(as->stmt_count?as->stmt_count:1U,1U);
        if(!graph->branch_membership) {
            minias_set_error(as,"out-of-memory:branch-membership");
            return false;
        }
        for(j=0U;j<growth->count;++j) {
            size_t id=growth->ids[j];
            if(id>=as->stmt_count ||
               as->stmts[id].fragment_kind!=MINIAS_FRAGMENT_INSTRUCTION ||
               graph->branch_membership[id]) {
                minias_set_error(as,"invalid-forced-fragment-id:%zu",id);
                return false;
            }
            graph->branch_membership[id]=1U;
        }
    }
    for (i = 0U; i < as->stmt_count; ++i) {
        const MiniAsStmt *stmt = &as->stmts[i];
        MiniAsTargetRelaxPlan plan;
        if(stmt->fragment_kind!=MINIAS_FRAGMENT_INSTRUCTION)continue;
        if(!minias_query_target_relax_plan(as,stmt,&plan))return false;
        if(plan.required_size<=stmt->size)continue;
        if(graph->branch_membership[i]) {
            /* Growth is monotone, but a Target may have more than one
             * alternative (RV C16 -> B32 -> inverted B32 + JAL, for
             * example). An id already in forced_ids is not necessarily
             * at its terminal size. Keep the unique worklist entry and
             * request another fixed-point pass for its *new* range. */
            *added=true;
            continue;
        }
        if (!minias_grow_array((void **)&growth->ids,&growth->capacity,
                               sizeof(*growth->ids),growth->count + 1U)) {
            minias_set_error(as, "out-of-memory:branch-relaxation");
            return false;
        }
        growth->ids[growth->count++] = i;
        graph->branch_membership[i]=1U;
        *added=true;
    }
    return true;
}

/* MCLayout is also the final semantic authority before any target
 * emits machine bytes. Confirm that subsection concatenation exactly
 * reproduces every stable Fragment placement and that alignment, DATA
 * width and end anchors cannot drift between fixed-point and output.
 *
 * This is intentionally target-free: a new backend gets the same layout
 * proof, not a copy of an ISA-specific padding/offset validator.
 */
static bool minias_validate_fragment_layout(MiniAs *as) {
    MiniAsAnchorGraph *graph=build_anchor_graph(as);
    uint64_t *cursor=NULL;
    size_t i;
    bool ok=false;
    if(graph==NULL)return false;
    if(as->layout->subsection_count>SIZE_MAX/sizeof(uint64_t)) {
        minias_set_error(as,"fragment-validate-capacity-overflow");
        return false;
    }
    cursor=calloc(as->layout->subsection_count?as->layout->subsection_count:1U,
                  sizeof(*cursor));
    if(!cursor) {
        minias_set_error(as,"out-of-memory:validate-fragment-layout");
        return false;
    }
    for(i=0U;i<=as->stmt_count;++i) {
        /* Symbol anchors must be checked before the next Fragment advances
         * its subsection cursor; this also covers trailing section labels. */
        {
            size_t symbol;
            for(symbol=graph->start_head[i];symbol!=SIZE_MAX;
                symbol=graph->start_next[symbol]) {
                const MiniAsSymbol *sym=&as->symbols[symbol];
                size_t sub_index=graph->symbol_subsection[symbol];
                MiniAsSubsection *sub;
                uint64_t expected;
                if(sub_index>=as->layout->subsection_count) {
                    minias_set_error(as,"fragment-symbol-subsection-missing:%s",
                                     sym->name);
                    goto done;
                }
                sub=&as->layout->subsections[sub_index];
                if(sub->section!=sym->section ||
                   sub->number!=sym->subsection ||
                   cursor[sub_index]>UINT64_MAX-sub->base) {
                    minias_set_error(as,"fragment-symbol-offset-overflow:%s",
                                     sym->name);
                    goto done;
                }
                expected=sub->base+cursor[sub_index];
                if (!add_signed_u64(expected,sym->fragment_anchor_addend,
                                    &expected)) {
                    minias_set_error(as,"fragment-symbol-addend-overflow:%s",
                                     sym->name);
                    goto done;
                }
                if(sym->value!=expected) {
                    minias_set_error(as,
                                     "fragment-symbol-anchor-drift:%s:expected=%llu:actual=%llu",
                                     sym->name,
                                     (unsigned long long)expected,
                                     (unsigned long long)sym->value);
                    goto done;
                }
            }
        }
        /* .size symbol,.-symbol is an anchored range, not merely a
         * recomputed scalar. Detect drifting end points after relaxation. */
        {
            size_t symbol;
            for(symbol=graph->end_head[i];symbol!=SIZE_MAX;
                symbol=graph->end_next[symbol]) {
                const MiniAsSymbol *sym=&as->symbols[symbol];
                const size_t sub_index=graph->end_subsection[symbol];
                uint64_t expected;
                if(sub_index>=as->layout->subsection_count ||
                   cursor[sub_index]>UINT64_MAX-as->layout->subsections[sub_index].base) {
                    minias_set_error(as,"fragment-size-end-overflow:%s",
                                     sym->name);
                    goto done;
                }
                expected=as->layout->subsections[sub_index].base+cursor[sub_index];
                if(!add_signed_u64(expected,sym->size_end_addend,
                                   &expected)) {
                    minias_set_error(as,"fragment-size-addend-overflow:%s",
                                     sym->name);
                    goto done;
                }
                const MiniAsSymbol *start=sym->has_size_start_symbol?
                    &as->symbols[sym->size_start_symbol_index]:sym;
                if(sym->size_end_value!=expected ||
                   start->value>expected ||
                   sym->size!=expected-start->value) {
                    minias_set_error(as,"fragment-size-end-anchor-drift:%s",
                                     sym->name);
                    goto done;
                }
            }
        }
        if(i==as->stmt_count)break;
        {
            const MiniAsStmt *stmt=&as->stmts[i];
            size_t sub_index=graph->statement_subsection[i];
            MiniAsSubsection *sub;
            uint64_t expected;
            if(sub_index>=as->layout->subsection_count) {
                minias_set_error(as,"fragment-subsection-missing:line=%zu",
                                 stmt->line);
                goto done;
            }
            sub=&as->layout->subsections[sub_index];
            if(sub->section!=stmt->section ||
               sub->number!=stmt->subsection ||
               cursor[sub_index]>UINT64_MAX-sub->base) {
                minias_set_error(as,"fragment-offset-overflow:line=%zu",
                                 stmt->line);
                goto done;
            }
            expected=sub->base+cursor[sub_index];
            if(expected!=stmt->offset) {
                minias_set_error(as,
                                 "fragment-offset-drift:line=%zu:expected=%llu:actual=%llu",
                                 stmt->line,
                                 (unsigned long long)expected,
                                 (unsigned long long)stmt->offset);
                goto done;
            }
            if(stmt->fragment_kind==MINIAS_FRAGMENT_ORIGIN) {
                uint32_t padding;
                if (!org_fragment_padding(as,stmt,expected,&padding))
                    goto done;
                if(padding!=stmt->size) {
                    minias_set_error(as,
                        "fragment-org-target-drift:line=%zu:expected-pad=%u:actual-pad=%u:location=%llu",
                        stmt->line,(unsigned)padding,(unsigned)stmt->size,
                        (unsigned long long)expected);
                    goto done;
                }
            } else if(stmt->fragment_kind==MINIAS_FRAGMENT_ALIGNMENT) {
                uint64_t padding;
                if(!fragment_alignment_padding(stmt,cursor[sub_index],&padding)) {
                    minias_set_error(as,"invalid-layout-alignment:line=%zu",
                                     stmt->line);
                    goto done;
                }
                if(padding!=stmt->size) {
                    minias_set_error(as,"fragment-alignment-drift:line=%zu",
                                     stmt->line);
                    goto done;
                }
            }
            if((stmt->fragment_kind==MINIAS_FRAGMENT_FILL ||
                stmt->fragment_kind==MINIAS_FRAGMENT_SPACE) &&
               stmt->payload.variable.resolved) {
                uint64_t count=stmt->payload.variable.repeat;
                unsigned width=stmt->payload.variable.element_width;
                if(width>8U || (width!=0U &&
                   (count>UINT32_MAX/width ||
                    count*(uint64_t)width!=stmt->size)) ||
                   (width==0U && stmt->size!=0U)) {
                    minias_set_error(as,
                                     "fragment-variable-width-drift:line=%zu",
                                     stmt->line);
                    goto done;
                }
            }
            if(stmt->size>UINT64_MAX-cursor[sub_index]) {
                minias_set_error(as,"fragment-size-overflow:line=%zu",
                                 stmt->line);
                goto done;
            }
            cursor[sub_index]+=stmt->size;
        }
    }
    for(i=0U;i<as->layout->subsection_count;++i) {
        const MiniAsSubsection *sub=&as->layout->subsections[i];
        if(cursor[i]!=sub->size) {
            minias_set_error(as,
                             "fragment-subsection-size-drift:section=%d:sub=%u",
                             sub->section,(unsigned)sub->number);
            goto done;
        }
    }
    /* Ensure final sections do not overlap/overflow the accumulated
     * subsections; different source-order subsections are concatenated by
     * their numeric index, never by the time they were first encountered. */
    {
        size_t k=0U;
        /* build_anchor_graph already checked canonical (section,subsection)
         * order. Consume each subsection once instead of rescanning the
         * entire array for every Section (O(S+Sub), not O(S*Sub)). */
        for(i=0U;i<as->section_count;++i) {
            uint64_t end=0U;
            while(k<as->layout->subsection_count &&
                  (size_t)as->layout->subsections[k].section==i) {
                const MiniAsSubsection *sub=&as->layout->subsections[k++];
                if(sub->base!=end) {
                    minias_set_error(as,"fragment-section-gap-or-overlap:%s",
                                     as->sections[i].name);
                    goto done;
                }
                if(sub->size>UINT64_MAX-sub->base) {
                    minias_set_error(as,"fragment-section-overflow:%s",
                                     as->sections[i].name);
                    goto done;
                }
                end=sub->base+sub->size;
            }
            if(end!=as->sections[i].layout_size) {
                minias_set_error(as,"fragment-section-layout-drift:%s",
                                 as->sections[i].name);
                goto done;
            }
        }
        if(k!=as->layout->subsection_count) {
            minias_set_error(as,"fragment-index-noncanonical-subsection");
            goto done;
        }
    }
    ok=true;
done:
    free(cursor);
    return ok;
}

/* One persistent MCFragment fixed point; Target only owns the branch
 * range predicate and its expansion size. Common owns every label anchor,
 * subsection, deferred DATA fragment, typed .org and alignment adjustment.
 * Lexical conditionals and dot-anchored .set preserve the Fragment graph;
 * unsupported location-dependent parser control retains source fallback.
 */
bool minias_solve_fragment_layout(MiniAs *as,
                                  MiniAsFragmentGrowthSet *growth,
                                  bool *reparse_source, FILE *diagnostic) {
    MiniAsLoweringLayoutPlan lowering_plan;
    bool added=false;
    size_t passes=0U;
    *reparse_source=false;
    if(!minias_common_lowering_layout_plan(as,&lowering_plan)) {
        minias_set_error(as,"layout-without-lowering-plan");
        return false;
    }
    if(!minias_validate_fragment_semantics(as) ||
       !minias_resolve_deferred_data(as) ||
       !collect_out_of_range_branches(as,growth,&added))
        return false;
    if(added && lowering_plan.layout_sensitive) {
        *reparse_source=true;
        return true;
    }
    while(added) {
        size_t j;
        for(j=0U;j<growth->count;++j) {
            size_t id=growth->ids[j];
            MiniAsStmt *fragment;
            MiniAsTargetRelaxPlan plan;
            if(id>=as->stmt_count) {
                minias_set_error(as,"internal:relaxation-fragment-id");
                return false;
            }
            fragment=&as->stmts[id];
            if(!minias_query_target_relax_plan(as,fragment,&plan))return false;
            if(plan.terminal_size>fragment->size)
                fragment->size=plan.terminal_size;
            else if(plan.required_size>fragment->size) {
                minias_set_error(as,"unrelaxed-target-fragment:line=%zu",
                                 fragment->line);
                return false;
            }
        }
        if(!minias_relayout_fragments(as,NULL) ||
           !minias_resolve_deferred_data(as) ||
           !collect_out_of_range_branches(as,growth,&added))
            return false;
        if(++passes>as->stmt_count+1U) {
            minias_set_error(as,"fragment-relaxation-nonconvergent");
            return false;
        }
    }
    if(!minias_validate_fragment_layout(as) ||
       !minias_debug_resolve_views(as,true))return false;
    if(diagnostic!=NULL && getenv("MINIAS_TRACE_RELAYOUT")!=NULL) {
        const MiniAsAnchorGraph *g=
            (const MiniAsAnchorGraph *)as->layout->anchor_graph;
        size_t typed_variable=0U,source_variable=0U,i;
        for(i=0U;i<as->stmt_count;++i) {
            const MiniAsStmt *stmt=&as->stmts[i];
            if(!fragment_has_deferred_data(stmt))continue;
            if((stmt->fragment_kind==MINIAS_FRAGMENT_BYTES &&
                stmt->payload.bytes.leb_expr!=NULL) ||
               ((stmt->fragment_kind==MINIAS_FRAGMENT_FILL ||
                 stmt->fragment_kind==MINIAS_FRAGMENT_SPACE) &&
                stmt->payload.variable.expr_count!=0U))++typed_variable;
            else ++source_variable;
        }
        if(passes!=0U)
            fprintf(diagnostic,"minias-fragment-relayout:inplace-passes=%zu\n",
                    passes);
        fprintf(diagnostic,
                "minias-fragment-graph:anchors_built=%u relayouts=%zu branches=%zu reparse=0\n",
                (unsigned)(g!=NULL),g?g->relayout_count:0U,growth->count);
        fprintf(diagnostic,
                "minias-mcexpr-variable:typed=%zu fallback=%zu\n",
                typed_variable,source_variable);
    }
    return true;
}
