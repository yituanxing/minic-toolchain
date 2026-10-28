#include "minias_internal.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Common MCLayout subsystem. Fragment identities are immutable; Target
 * hooks give ISA instruction widths and relaxation ranges. */

MiniAsSubsection *minias_find_subsection(MiniAs *as, int section, uint32_t number) {
    size_t i;

    for (i = 0U; i < as->subsection_count; ++i) {
        MiniAsSubsection *sub = &as->subsections[i];
        if (sub->section == section && sub->number == number) {
            return sub;
        }
    }
    return NULL;
}

MiniAsSubsection *minias_ensure_subsection(MiniAs *as, int section, uint32_t number) {
    MiniAsSubsection *sub = minias_find_subsection(as, section, number);

    if (sub != NULL) {
        return sub;
    }
    if (!minias_grow_array((void **)&as->subsections,
                    &as->subsection_capacity,
                    sizeof(*as->subsections),
                    as->subsection_count + 1U)) {
        minias_set_error(as, "out-of-memory:subsection");
        return NULL;
    }
    sub = &as->subsections[as->subsection_count++];
    memset(sub, 0, sizeof(*sub));
    sub->section = section;
    sub->number = number;
    return sub;
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

bool minias_finalize_subsection_layout(MiniAs *as) {
    size_t i;
    int active_section = -1;
    uint64_t cursor = 0U;

    if (as->subsection_count != 0U) {
        qsort(as->subsections,
              as->subsection_count,
              sizeof(*as->subsections),
              compare_subsections);
    }
    for (i = 0U; i < as->section_count; ++i) {
        as->sections[i].layout_size = 0U;
    }
    for (i = 0U; i < as->subsection_count; ++i) {
        MiniAsSubsection *sub = &as->subsections[i];

        if (sub->section != active_section) {
            if (active_section >= 0) {
                as->sections[(size_t)active_section].layout_size = cursor;
            }
            active_section = sub->section;
            cursor = 0U;
        }
        sub->base = cursor;
        if (sub->size > UINT64_MAX - cursor) {
            minias_set_error(as, "section-layout-overflow");
            return false;
        }
        cursor += sub->size;
    }
    if (active_section >= 0) {
        as->sections[(size_t)active_section].layout_size = cursor;
    }

    for (i = 0U; i < as->stmt_count; ++i) {
        MiniAsStmt *stmt = &as->stmts[i];
        MiniAsSubsection *sub =
            minias_find_subsection(as, stmt->section, stmt->subsection);
        if (sub == NULL || stmt->offset > UINT64_MAX - sub->base) {
            minias_set_error(as, "internal:stmt-subsection-layout");
            return false;
        }
        stmt->offset += sub->base;
    }
    for (i = 0U; i < as->symbol_count; ++i) {
        MiniAsSymbol *symbol = &as->symbols[i];
        MiniAsSubsection *sub;

        if (!symbol->defined || symbol->section < 0) {
            continue;
        }
        sub = minias_find_subsection(as, symbol->section, symbol->subsection);
        if (sub == NULL || symbol->value > UINT64_MAX - sub->base) {
            minias_set_error(as, "internal:symbol-subsection-layout:%s", symbol->name);
            return false;
        }
        symbol->value += sub->base;
        /* Parse-site .size records the span, not an already normalized
         * absolute end offset. Reconstitute the exact end anchor after
         * subsection concatenation, including sources that never needed a
         * relaxation pass. A relayout iteration also reaches this path,
         * so the end is normalized only once per layout application. */
        if (symbol->has_size_end_anchor) {
            if (symbol->size > UINT64_MAX - symbol->value) {
                minias_set_error(as, "fragment-size-end-overflow:%s",
                                 symbol->name);
                return false;
            }
            symbol->size_end_value = symbol->value + symbol->size;
        }
    }
    return true;
}



/* Stable indexed MCFragment graph. GNU subsections are sorted by number at
 * parse completion. Cache their identity ONCE, never redo a linear search for
 * every symbol and statement in every relaxation pass. Source ordinals and
 * symbol indices remain immutable even when macros expand. */
typedef struct MiniAsAnchorGraph {
    size_t *start_head, *end_head;
    size_t *start_next, *end_next;
    size_t *statement_subsection, *symbol_subsection;
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
    free(graph);
}

void minias_layout_destroy(MiniAs *as) {
    free_anchor_graph((MiniAsAnchorGraph *)as->layout_anchor_graph);
    as->layout_anchor_graph = NULL;
    free(as->forced_long_branch_bitmap);
    as->forced_long_branch_bitmap = NULL;
    as->forced_long_branch_bitmap_size = 0U;
}

/* MCLayout is the only owner of symbol anchors and Section identity.
 * Every allocation/validation failure abandons the graph atomically. */
static MiniAsAnchorGraph *build_anchor_graph(MiniAs *as) {
    MiniAsAnchorGraph *graph = (MiniAsAnchorGraph *)as->layout_anchor_graph;
    const size_t n = as->stmt_count;
    const size_t symbols = as->symbol_count;
    const size_t subsections = as->subsection_count;
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
    if (graph->start_head == NULL || graph->end_head == NULL ||
        graph->start_next == NULL || graph->end_next == NULL ||
        graph->statement_subsection == NULL ||
        graph->symbol_subsection == NULL) {
        minias_set_error(as, "out-of-memory:fragment-index-storage");
        goto fail;
    }

    /* Subsection indexes are stable iff the parser already canonicalized
     * them. Reject duplicates, invalid Sections and accidental reorder. */
    for (i = 0U; i < subsections; ++i) {
        const MiniAsSubsection *sub = &as->subsections[i];
        if (sub->section < 0 || (size_t)sub->section >= as->section_count ||
            (i != 0U &&
             compare_subsections(&as->subsections[i-1U], sub) >= 0)) {
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
            minias_find_subsection(as, stmt->section, stmt->subsection);
        if (sub == NULL) {
            minias_set_error(as, "fragment-index-missing-stmt-subsection:%zu",
                             stmt->line);
            goto fail;
        }
        graph->statement_subsection[i] = (size_t)(sub - as->subsections);
    }
    for (i = 0U; i < symbols; ++i) {
        const MiniAsSymbol *sym = &as->symbols[i];
        MiniAsSubsection *sub = NULL;
        graph->start_next[i] = SIZE_MAX;
        graph->end_next[i] = SIZE_MAX;
        graph->symbol_subsection[i] = SIZE_MAX;
        if (sym->defined && sym->section >= 0)
            sub = minias_find_subsection(as, sym->section, sym->subsection);
        if (sub != NULL)
            graph->symbol_subsection[i] = (size_t)(sub - as->subsections);
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
            if (sym->size_end_anchor > n || sub == NULL ||
                !sym->has_fragment_anchor) {
                minias_set_error(as, "fragment-index-invalid-end:%s",
                                 sym->name);
                goto fail;
            }
            graph->end_next[i] = graph->end_head[sym->size_end_anchor];
            graph->end_head[sym->size_end_anchor] = i;
        }
    }
    as->layout_anchor_graph = graph;
    return graph;
fail:
    free_anchor_graph(graph);
    return NULL;
}

/* A source-level statement ordinal survives macro expansion and re-layout. */
bool minias_force_long_branch_for_stmt(const MiniAs *as, size_t statement_id) {
    size_t i;
    if (as->forced_long_branch_bitmap != NULL &&
        statement_id < as->forced_long_branch_bitmap_size)
        return as->forced_long_branch_bitmap[statement_id] != 0U;
    for (i = 0U; i < as->forced_long_branch_count; ++i)
        if (as->forced_long_branch_ids[i] == statement_id) return true;
    return false;
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
                                 uint64_t location, bool final_symbols,
                                 uint32_t *padding) {
    uint64_t target = 0U;
    uint64_t gap;
    if (stmt->org_mode == MINIAS_ORG_ABSOLUTE) {
        target = stmt->org_target;
    } else if (stmt->org_mode == MINIAS_ORG_DOT_ADDEND) {
        if (stmt->org_target > UINT64_MAX - location) {
            minias_set_error(as,"org-dot-overflow:line=%zu",stmt->line);
            return false;
        }
        target = location + stmt->org_target;
    } else if (stmt->org_mode == MINIAS_ORG_SYMBOL_ADDEND) {
        const MiniAsSymbol *symbol;
        const MiniAsSubsection *sub;
        uint64_t value;
        if (stmt->org_symbol_index >= as->symbol_count) {
            minias_set_error(as,"org-symbol-index-invalid:line=%zu",stmt->line);
            return false;
        }
        symbol = &as->symbols[stmt->org_symbol_index];
        sub = minias_find_subsection(as,symbol->section,symbol->subsection);
        if (sub == NULL || !symbol->defined ||
            !symbol->has_fragment_anchor ||
            symbol->section != stmt->section ||
            symbol->subsection > stmt->subsection) {
            minias_set_error(as,"org-symbol-owner-invalid:line=%zu",stmt->line);
            return false;
        }
        value = symbol->value;
        if (!final_symbols) {
            if (sub->base > UINT64_MAX - value) {
                minias_set_error(as,"org-symbol-base-overflow:line=%zu",stmt->line);
                return false;
            }
            value += sub->base;
        }
        if (!add_signed_u64(value,stmt->org_addend,&target)) {
            minias_set_error(as,"org-symbol-addend-overflow:line=%zu",stmt->line);
            return false;
        }
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

/* Persistent Fragment fixed-point: the Target owns alternative instruction
 * widths but cannot mutate Section placement.  Symbol-start, symbol-size
 * end and the statement itself all use the SAME prevalidated subsection
 * index.  This prevents source-order .subsection switches from binding an
 * anchor to a different output Section after a branch expands. */
static bool minias_relayout_fragments(MiniAs *as, bool *org_changed) {
    MiniAsAnchorGraph *graph = build_anchor_graph(as);
    size_t i;
    if (graph == NULL) return false;
    if (org_changed != NULL) *org_changed = false;
    ++graph->relayout_count;
    for (i = 0U; i < graph->subsection_count; ++i)
        as->subsections[i].size = 0U;

    for (i = 0U; i <= graph->stmt_count; ++i) {
        size_t symbol;
        for (symbol = graph->start_head[i]; symbol != SIZE_MAX;
             symbol = graph->start_next[symbol]) {
            const size_t subsection = graph->symbol_subsection[symbol];
            if (subsection >= graph->subsection_count) {
                minias_set_error(as, "internal:fragment-start-subsection");
                return false;
            }
            if (!add_signed_u64(as->subsections[subsection].size,
                                as->symbols[symbol].fragment_anchor_addend,
                                &as->symbols[symbol].value)) {
                minias_set_error(as,"fragment-anchor-addend-overflow:%s",
                                 as->symbols[symbol].name);
                return false;
            }
        }
        for (symbol = graph->end_head[i]; symbol != SIZE_MAX;
             symbol = graph->end_next[symbol]) {
            const size_t subsection = graph->symbol_subsection[symbol];
            if (subsection >= graph->subsection_count) {
                minias_set_error(as, "internal:fragment-end-subsection");
                return false;
            }
            as->symbols[symbol].size_end_value =
                as->subsections[subsection].size;
        }
        if (i < graph->stmt_count) {
            MiniAsStmt *stmt = &as->stmts[i];
            const size_t subsection = graph->statement_subsection[i];
            MiniAsSubsection *sub;
            if (subsection >= graph->subsection_count) {
                minias_set_error(as, "internal:fragment-stmt-subsection");
                return false;
            }
            sub = &as->subsections[subsection];
            if (sub->section != stmt->section ||
                sub->number != stmt->subsection) {
                minias_set_error(as, "fragment-subsection-identity-mutated");
                return false;
            }
            stmt->offset = sub->size;
            if (stmt->kind == MINIAS_STMT_ALIGN &&
                stmt->org_mode != MINIAS_ORG_SOURCE) {
                uint32_t pad;
                if (sub->size > UINT64_MAX - sub->base ||
                    !org_fragment_padding(as,stmt,sub->base+sub->size,
                                          false,&pad)) return false;
                if (org_changed != NULL && stmt->size != pad)
                    *org_changed = true;
                stmt->size = pad;
            } else if (stmt->kind == MINIAS_STMT_ALIGN &&
                       strcmp(stmt->op, ".org") != 0) {
                const uint64_t alignment = stmt->align;
                uint64_t pad;
                if (alignment == 0U ||
                    (alignment & (alignment - 1U)) != 0U) {
                    minias_set_error(as, "bad-fragment-alignment");
                    return false;
                }
                pad = (alignment - (sub->size & (alignment - 1U))) &
                      (alignment - 1U);
                if (stmt->align_has_max_skip &&
                    pad > stmt->align_max_skip) pad = 0U;
                if (pad > UINT32_MAX) {
                    minias_set_error(as, "fragment-alignment-overflow");
                    return false;
                }
                stmt->size = (uint32_t)pad;
            }
            if ((uint64_t)stmt->size > UINT64_MAX - sub->size) {
                minias_set_error(as, "fragment-size-overflow");
                return false;
            }
            sub->size += stmt->size;
        }
    }
    for (i = 0U; i < graph->symbol_count; ++i) {
        MiniAsSymbol *symbol = &as->symbols[i];
        if (!symbol->has_size_end_anchor) continue;
        if (!symbol->has_fragment_anchor ||
            symbol->size_end_value < symbol->value) {
            minias_set_error(as, "fragment-size-location-mismatch:%s",
                             symbol->name);
            return false;
        }
        symbol->size = symbol->size_end_value - symbol->value;
    }
    /* This pass used the PREVIOUS subsection bases to solve .org. If an
     * earlier subsection's DATA grew, the freshly computed base must itself
     * trigger another pass even when no .org padding changed YET. Otherwise
     * an apparently stable pass can validate against a different address. */
    if(org_changed != NULL) {
        int active_section=-1;
        uint64_t base=0U;
        for(i=0U;i<as->subsection_count;++i) {
            const MiniAsSubsection *sub=&as->subsections[i];
            if(sub->section!=active_section) {
                active_section=sub->section;
                base=0U;
            }
            if(sub->base!=base)*org_changed=true;
            if(sub->size>UINT64_MAX-base) {
                minias_set_error(as,"section-layout-overflow");
                return false;
            }
            base+=sub->size;
        }
    }
    return minias_finalize_subsection_layout(as);
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
    size_t pass;
    int previous_section=as->current_section;
    uint32_t previous_subsection=as->current_subsection;
    if(!as->has_deferred_data && !as->has_org_constraints)return true;
    /* .org, .fill and .space share a single fixed point.  A subsection
     * base may change as another numbered subsection grows, so resolving
     * .org only once would produce wrong final section-relative offsets. */
    for(pass=0U;pass<=as->stmt_count;++pass) {
        size_t i;
        bool changed=false;
        bool org_changed=false;
        if(!minias_relayout_fragments(as,&org_changed))goto invalid;
        for(i=0U;i<as->stmt_count;++i) {
            MiniAsStmt *stmt=&as->stmts[i];
            uint64_t n=0U;
            if(!stmt->deferred_data_size)continue;
            as->current_section=stmt->section;
            as->current_subsection=stmt->subsection;
            as->expr_dot_override=true;
            as->expr_dot_value=stmt->offset;
            if(stmt->variable_mcdata!=NULL) {
                uint64_t repeat=0U,bits=0U;
                unsigned width=0U;
                if(!minias_resolve_variable_mc(as,stmt,&repeat,&width,&bits) ||
                   (width!=0U && repeat>UINT32_MAX/width)) {
                    minias_set_error(as,"unresolved-typed-variable-data:line=%zu",
                                     stmt->line);
                    goto invalid;
                }
                n=repeat*width;
                stmt->variable_repeat=repeat;
                stmt->variable_element_width=width;
                stmt->variable_value=bits;
                stmt->variable_data_resolved=true;
            }else if(!strcmp(stmt->op,".fill")) {
                unsigned width=0U;
                uint64_t repeat=0U,bits=0U;
                if(!minias_parse_common_fill(as,stmt->args,&repeat,&width,&bits) ||
                   (width!=0U && repeat>UINT32_MAX/width)) {
                    minias_set_error(as,
                        "unresolved-deferred-fill:%s:line=%zu",
                        stmt->args,stmt->line);
                    goto invalid;
                }
                n=repeat*width;
                stmt->variable_repeat=repeat;
                stmt->variable_element_width=width;
                stmt->variable_value=bits;
                stmt->variable_data_resolved=true;
            }else {
                uint64_t amount=0U;
                uint8_t fill=0U;
                if(!minias_parse_common_space(as,stmt->args,&amount,&fill) ||
                   amount>UINT32_MAX) {
                    minias_set_error(as,
                        "unresolved-deferred-space:%s:line=%zu",
                        stmt->args,stmt->line);
                    goto invalid;
                }
                n=amount;
                stmt->variable_repeat=amount;
                stmt->variable_element_width=1U;
                stmt->variable_value=fill;
                stmt->variable_data_resolved=true;
            }
            as->expr_dot_override=false;
            if(n!=(uint64_t)stmt->size) {
                stmt->size=(uint32_t)n;
                changed=true;
            }
        }
        if(!changed && !org_changed) {
            as->current_section=previous_section;
            as->current_subsection=previous_subsection;
            return true;
        }
    }
    minias_set_error(as,"mc-fragment-layout-nonconvergent");
invalid:
    as->expr_dot_override=false;
    as->current_section=previous_section;
    as->current_subsection=previous_subsection;
    return false;
}

static bool collect_out_of_range_branches(MiniAs *as,
                                          size_t **ids,
                                          size_t *count,
                                          size_t *capacity,
                                          bool *added) {
    size_t i,j;
    *added=false;
    if(as->forced_long_branch_bitmap==NULL) {
        as->forced_long_branch_bitmap=
            calloc(as->stmt_count?as->stmt_count:1U,1U);
        if(!as->forced_long_branch_bitmap) {
            minias_set_error(as,"out-of-memory:branch-membership");
            return false;
        }
        as->forced_long_branch_bitmap_size=as->stmt_count;
        for(j=0U;j<*count;++j) {
            size_t id=(*ids)[j];
            if(id>=as->stmt_count ||
               as->stmts[id].kind!=MINIAS_STMT_INSN ||
               as->forced_long_branch_bitmap[id]) {
                minias_set_error(as,"invalid-forced-fragment-id:%zu",id);
                return false;
            }
            as->forced_long_branch_bitmap[id]=1U;
        }
    }
    as->forced_long_branch_ids=*ids;
    as->forced_long_branch_count=*count;
    if (as->target->relax_instruction == NULL) return true;
    for (i = 0U; i < as->stmt_count; ++i) {
        const MiniAsStmt *stmt = &as->stmts[i];
        uint32_t expanded = stmt->size;
        if (stmt->kind != MINIAS_STMT_INSN ||
            !as->target->relax_instruction(as, stmt, &expanded)) {
            if (as->error[0] != '\0') return false;
            continue;
        }
        if (expanded <= stmt->size) continue;
        if (as->target->expanded_instruction_size == NULL) {
            minias_set_error(as, "target-relaxation-size-unavailable:%s",
                             stmt->op);
            return false;
        }
        if(as->forced_long_branch_bitmap[i])continue;
        if (!minias_grow_array((void **)ids, capacity, sizeof(**ids), *count + 1U)) {
            minias_set_error(as, "out-of-memory:branch-relaxation");
            return false;
        }
        (*ids)[(*count)++] = i;
        as->forced_long_branch_bitmap[i]=1U;
        as->forced_long_branch_ids=*ids;
        as->forced_long_branch_count=*count;
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
    if(as->subsection_count>SIZE_MAX/sizeof(uint64_t)) {
        minias_set_error(as,"fragment-validate-capacity-overflow");
        return false;
    }
    cursor=calloc(as->subsection_count?as->subsection_count:1U,
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
                if(sub_index>=as->subsection_count) {
                    minias_set_error(as,"fragment-symbol-subsection-missing:%s",
                                     sym->name);
                    goto done;
                }
                sub=&as->subsections[sub_index];
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
                const size_t sub_index=graph->symbol_subsection[symbol];
                uint64_t expected;
                if(sub_index>=as->subsection_count ||
                   cursor[sub_index]>UINT64_MAX-as->subsections[sub_index].base) {
                    minias_set_error(as,"fragment-size-end-overflow:%s",
                                     sym->name);
                    goto done;
                }
                expected=as->subsections[sub_index].base+cursor[sub_index];
                if(sym->size_end_value!=expected ||
                   sym->value>expected ||
                   sym->size!=expected-sym->value) {
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
            if(sub_index>=as->subsection_count) {
                minias_set_error(as,"fragment-subsection-missing:line=%zu",
                                 stmt->line);
                goto done;
            }
            sub=&as->subsections[sub_index];
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
            if(stmt->kind==MINIAS_STMT_ALIGN &&
               stmt->org_mode!=MINIAS_ORG_SOURCE) {
                uint32_t padding;
                if (!org_fragment_padding(as,stmt,expected,true,&padding))
                    goto done;
                if(padding!=stmt->size) {
                    minias_set_error(as,
                        "fragment-org-target-drift:line=%zu:expected-pad=%u:actual-pad=%u:location=%llu",
                        stmt->line,(unsigned)padding,(unsigned)stmt->size,
                        (unsigned long long)expected);
                    goto done;
                }
            } else if(stmt->kind==MINIAS_STMT_ALIGN &&
                      strcmp(stmt->op,".org")!=0) {
                uint64_t alignment=stmt->align,padding;
                if(alignment==0U || (alignment&(alignment-1U))!=0U) {
                    minias_set_error(as,"invalid-layout-alignment:line=%zu",
                                     stmt->line);
                    goto done;
                }
                padding=(alignment-(cursor[sub_index]&(alignment-1U)))&
                        (alignment-1U);
                if(stmt->align_has_max_skip &&
                   padding>stmt->align_max_skip)padding=0U;
                if(padding!=stmt->size) {
                    minias_set_error(as,"fragment-alignment-drift:line=%zu",
                                     stmt->line);
                    goto done;
                }
            }
            if(stmt->variable_data_resolved &&
               (strcmp(stmt->op,".fill")==0 ||
                strcmp(stmt->op,".space")==0 ||
                strcmp(stmt->op,".skip")==0 ||
                strcmp(stmt->op,".zero")==0)) {
                uint64_t count=stmt->variable_repeat;
                unsigned width=stmt->variable_element_width;
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
    for(i=0U;i<as->subsection_count;++i) {
        const MiniAsSubsection *sub=&as->subsections[i];
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
    for(i=0U;i<as->section_count;++i) {
        size_t k;
        uint64_t end=0U;
        for(k=0U;k<as->subsection_count;++k) {
            const MiniAsSubsection *sub=&as->subsections[k];
            if(sub->section!=(int)i)continue;
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
                                  size_t **forced_ids, size_t *forced_count,
                                  size_t *forced_capacity,
                                  bool *reparse_source, FILE *diagnostic) {
    bool added=false;
    size_t passes=0U;
    *reparse_source=false;
    if(!minias_resolve_deferred_data(as) ||
       !collect_out_of_range_branches(as,forced_ids,forced_count,
                                       forced_capacity,&added))
        return false;
    if(added && as->layout_sensitive) {
        *reparse_source=true;
        return true;
    }
    while(added) {
        size_t j;
        for(j=0U;j<*forced_count;++j) {
            size_t id=(*forced_ids)[j];
            MiniAsStmt *fragment;
            uint32_t grown;
            if(id>=as->stmt_count) {
                minias_set_error(as,"internal:relaxation-fragment-id");
                return false;
            }
            fragment=&as->stmts[id];
            grown=fragment->size;
            if(as->target->expanded_instruction_size==NULL) {
                minias_set_error(as,"missing-target-fragment-growth:%s",
                                 fragment->op);
                return false;
            }
            if(as->target->expanded_instruction_size(fragment->op,
                                                      fragment->size,&grown)) {
                if(grown<=fragment->size) {
                    minias_set_error(as,"invalid-target-fragment-growth:%s",
                                     fragment->op);
                    return false;
                }
                fragment->size=grown;
            }else {
                uint32_t needed=fragment->size;
                if(as->target->relax_instruction==NULL ||
                   !as->target->relax_instruction(as,fragment,&needed) ||
                   needed>fragment->size) {
                    minias_set_error(as,"unrelaxed-target-fragment:%s",
                                     fragment->op);
                    return false;
                }
            }
        }
        if(!minias_relayout_fragments(as,NULL) ||
           !minias_resolve_deferred_data(as) ||
           !collect_out_of_range_branches(as,forced_ids,forced_count,
                                           forced_capacity,&added))
            return false;
        if(++passes>as->stmt_count+1U) {
            minias_set_error(as,"fragment-relaxation-nonconvergent");
            return false;
        }
    }
    if(!minias_validate_fragment_layout(as))return false;
    if(diagnostic!=NULL && getenv("MINIAS_TRACE_RELAYOUT")!=NULL) {
        const MiniAsAnchorGraph *g=
            (const MiniAsAnchorGraph *)as->layout_anchor_graph;
        size_t typed_variable=0U,source_variable=0U,i;
        for(i=0U;i<as->stmt_count;++i) {
            if(!as->stmts[i].deferred_data_size)continue;
            if(as->stmts[i].variable_mcdata!=NULL)++typed_variable;
            else ++source_variable;
        }
        if(passes!=0U)
            fprintf(diagnostic,"minias-fragment-relayout:inplace-passes=%zu\n",
                    passes);
        fprintf(diagnostic,
                "minias-fragment-graph:anchors_built=%u relayouts=%zu branches=%zu reparse=0\n",
                (unsigned)(g!=NULL),g?g->relayout_count:0U,*forced_count);
        fprintf(diagnostic,
                "minias-mcexpr-variable:typed=%zu fallback=%zu\n",
                typed_variable,source_variable);
    }
    return true;
}
