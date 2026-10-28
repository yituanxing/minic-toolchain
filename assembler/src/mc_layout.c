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
    }
    return true;
}


/* Stable Common Fragment graph retained throughout the parse and all layout
 * iterations. Neither the Target nor the output writer owns symbol anchors.
 * Four anchor arrays are allocated once, reused across every fixed point. */
typedef struct MiniAsAnchorGraph {
    size_t *start_head,*end_head,*start_next,*end_next;
    size_t stmt_count,symbol_count,relayout_count;
} MiniAsAnchorGraph;

void minias_layout_destroy(MiniAs *as) {
    MiniAsAnchorGraph *g=(MiniAsAnchorGraph *)as->layout_anchor_graph;
    if(g) {
        free(g->start_head);free(g->end_head);
        free(g->start_next);free(g->end_next);
        free(g);
        as->layout_anchor_graph=NULL;
    }
    free(as->forced_long_branch_bitmap);
    as->forced_long_branch_bitmap=NULL;
    as->forced_long_branch_bitmap_size=0U;
}

/* Statement ordinal is stable across re-layout; macro source lines are not. */
bool minias_force_long_branch_for_stmt(const MiniAs *as, size_t statement_id) {
    size_t i;
    if(as->forced_long_branch_bitmap!=NULL &&
       statement_id<as->forced_long_branch_bitmap_size)
        return as->forced_long_branch_bitmap[statement_id]!=0U;
    for(i=0U;i<as->forced_long_branch_count;++i)
        if(as->forced_long_branch_ids[i]==statement_id)return true;
    return false;
}

/* Layout iteration owns fragment identity and growth bookkeeping. Only
 * target backends know the instruction's displacement range and expansion. */

/* Persistent fragment layout: parsed statements are the fragments. Re-layout
 * only their offsets and sizes, preserve expanded macro identities, and
 * relocate label/.size anchors without reopening or reparsing the source.
 *
 * Sources containing position-dependent conditionals, .org, .set or .equ
 * retain the conservative reparsing path until these directives acquire
 * typed deferred-expression fragments. */
static bool minias_relayout_fragments(MiniAs *as) {
    size_t n = as->stmt_count, nsym = as->symbol_count;
    MiniAsAnchorGraph *graph=(MiniAsAnchorGraph *)as->layout_anchor_graph;
    size_t *start_head = NULL, *end_head = NULL;
    size_t *start_next = NULL, *end_next = NULL;
    size_t i;
    bool ok = false;
    bool new_index=graph==NULL;
    if(new_index) {
        graph=calloc(1U,sizeof(*graph));
        if(!graph) {
            minias_set_error(as,"out-of-memory:anchor-graph");
            return false;
        }
        graph->stmt_count=n;
        graph->symbol_count=nsym;
        as->layout_anchor_graph=graph;
    }else if(graph->stmt_count!=n || graph->symbol_count!=nsym) {
        minias_set_error(as,"anchor-graph-cardinality-mutated");
        return false;
    }
    ++graph->relayout_count;
    if(new_index) {

    if (n == SIZE_MAX || n + 1U > SIZE_MAX / sizeof(size_t) ||
        (nsym != 0U && nsym > SIZE_MAX / sizeof(size_t))) {
        minias_set_error(as, "fragment-anchor-overflow");
        return false;
    }
    start_head = malloc((n + 1U) * sizeof(size_t));
    end_head = malloc((n + 1U) * sizeof(size_t));
    start_next = malloc((nsym == 0U ? 1U : nsym) * sizeof(size_t));
    end_next = malloc((nsym == 0U ? 1U : nsym) * sizeof(size_t));
    graph->start_head=start_head;
    graph->end_head=end_head;
    graph->start_next=start_next;
    graph->end_next=end_next;
    if (start_head == NULL || end_head == NULL ||
        start_next == NULL || end_next == NULL) {
        minias_set_error(as, "out-of-memory:fragment-anchors");
        goto done;
    }
    for (i = 0U; i <= n; ++i) {
        start_head[i] = SIZE_MAX;
        end_head[i] = SIZE_MAX;
    }
    for (i = 0U; i < nsym; ++i) {
        MiniAsSymbol *sym = &as->symbols[i];
        if (sym->has_fragment_anchor && sym->defined && sym->section >= 0) {
            size_t anchor = sym->fragment_anchor;
            if (anchor > n) {
                minias_set_error(as, "internal:fragment-symbol-anchor:%s",
                                 sym->name);
                goto done;
            }
            start_next[i] = start_head[anchor];
            start_head[anchor] = i;
        }
        if (sym->has_size_end_anchor) {
            size_t anchor = sym->size_end_anchor;
            if (anchor > n) {
                minias_set_error(as, "internal:fragment-size-anchor:%s",
                                 sym->name);
                goto done;
            }
            end_next[i] = end_head[anchor];
            end_head[anchor] = i;
        }
    }

    } else {
        start_head=graph->start_head;
        end_head=graph->end_head;
        start_next=graph->start_next;
        end_next=graph->end_next;
    }
    for(i=0U;i<as->subsection_count;++i)
        as->subsections[i].size=0U;
    for (i = 0U; i <= n; ++i) {
        size_t index;
        for (index = start_head[i]; index != SIZE_MAX;
             index = start_next[index]) {
            MiniAsSymbol *sym = &as->symbols[index];
            MiniAsSubsection *sub =
                minias_find_subsection(as, sym->section, sym->subsection);
            if (sub == NULL) {
                minias_set_error(as, "internal:fragment-symbol-subsection:%s",
                                 sym->name);
                goto done;
            }
            sym->value = sub->size;
        }
        for (index = end_head[i]; index != SIZE_MAX;
             index = end_next[index]) {
            MiniAsSymbol *sym = &as->symbols[index];
            MiniAsSubsection *sub =
                minias_find_subsection(as, sym->section, sym->subsection);
            if (sub == NULL) {
                minias_set_error(as, "internal:fragment-size-subsection:%s",
                                 sym->name);
                goto done;
            }
            sym->size_end_value = sub->size;
        }
        if (i < n) {
            MiniAsStmt *stmt = &as->stmts[i];
            MiniAsSubsection *sub =
                minias_find_subsection(as, stmt->section, stmt->subsection);
            if (sub == NULL) {
                minias_set_error(as, "internal:fragment-subsection");
                goto done;
            }
            stmt->offset = sub->size;
            if (stmt->kind == MINIAS_STMT_ALIGN &&
                strcmp(stmt->op, ".org") != 0) {
                uint64_t align = stmt->align;
                uint64_t pad;
                if (align == 0U || (align & (align - 1U)) != 0U) {
                    minias_set_error(as, "bad-fragment-alignment");
                    goto done;
                }
                pad = (align - (sub->size & (align - 1U))) & (align - 1U);
                if (stmt->align_has_max_skip && pad > stmt->align_max_skip)
                    pad = 0U;
                if (pad > UINT32_MAX) {
                    minias_set_error(as, "fragment-alignment-overflow");
                    goto done;
                }
                stmt->size = (uint32_t)pad;
            }
            if ((uint64_t)stmt->size > UINT64_MAX - sub->size) {
                minias_set_error(as, "fragment-size-overflow");
                goto done;
            }
            sub->size += stmt->size;
        }
    }

    for (i = 0U; i < nsym; ++i) {
        MiniAsSymbol *sym = &as->symbols[i];
        if (sym->has_size_end_anchor) {
            if (!sym->has_fragment_anchor ||
                sym->size_end_value < sym->value) {
                minias_set_error(as, "fragment-size-location-mismatch:%s",
                                 sym->name);
                goto done;
            }
            sym->size = sym->size_end_value - sym->value;
        }
    }
    ok = minias_finalize_subsection_layout(as);
done:
    return ok;
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
    if(!as->has_deferred_data)return true;
    /* The original first-pass layout is also provisional: .fill may have
     * been measured before a forward symbol's section was known. */
    if(!minias_relayout_fragments(as))return false;
    for(pass=0U;pass<=as->stmt_count;++pass) {
        size_t i;
        bool changed=false;
        for(i=0U;i<as->stmt_count;++i) {
            MiniAsStmt *stmt=&as->stmts[i];
            uint64_t n=0U;
            if(!stmt->deferred_data_size)continue;
            as->current_section=stmt->section;
            as->current_subsection=stmt->subsection;
            as->expr_dot_override=true;
            as->expr_dot_value=stmt->offset;
            if(!strcmp(stmt->op,".fill")) {
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
        if(!changed) {
            as->current_section=previous_section;
            as->current_subsection=previous_subsection;
            return true;
        }
        if(!minias_relayout_fragments(as))goto invalid;
    }
    minias_set_error(as,"deferred-fragment-layout-nonconvergent");
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
    MiniAsAnchorGraph *graph=(MiniAsAnchorGraph *)as->layout_anchor_graph;
    uint64_t *cursor=NULL;
    size_t i;
    bool ok=false;
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
        size_t k;
        /* Symbol anchors must be checked before the next Fragment advances
         * its subsection cursor; this also covers trailing section labels. */
        if(graph!=NULL) {
            size_t symbol;
            for(symbol=graph->start_head[i];symbol!=SIZE_MAX;
                symbol=graph->start_next[symbol]) {
                const MiniAsSymbol *sym=&as->symbols[symbol];
                MiniAsSubsection *sub=minias_find_subsection(
                    as,sym->section,sym->subsection);
                size_t sub_index;
                uint64_t expected;
                if(!sub) {
                    minias_set_error(as,"fragment-symbol-subsection-missing:%s",
                                     sym->name);
                    goto done;
                }
                sub_index=(size_t)(sub-as->subsections);
                if(cursor[sub_index]>UINT64_MAX-sub->base) {
                    minias_set_error(as,"fragment-symbol-offset-overflow:%s",
                                     sym->name);
                    goto done;
                }
                expected=sub->base+cursor[sub_index];
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
        if(i==as->stmt_count)break;
        {
            const MiniAsStmt *stmt=&as->stmts[i];
            MiniAsSubsection *sub=minias_find_subsection(
                as,stmt->section,stmt->subsection);
            size_t sub_index;
            uint64_t expected;
            if(!sub) {
                minias_set_error(as,"fragment-subsection-missing:line=%zu",
                                 stmt->line);
                goto done;
            }
            sub_index=(size_t)(sub-as->subsections);
            if(cursor[sub_index]>UINT64_MAX-sub->base) {
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
            if(sub->base>end) {
                minias_set_error(as,"fragment-section-hole:%s",
                                 as->sections[i].name);
                goto done;
            }
            if(sub->size>UINT64_MAX-sub->base) {
                minias_set_error(as,"fragment-section-overflow:%s",
                                 as->sections[i].name);
                goto done;
            }
            if(sub->base+sub->size>end)end=sub->base+sub->size;
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
 * subsection, deferred DATA fragment and alignment adjustment.
 * Layout-sensitive .if/.org/.set source may change fragment cardinality,
 * so those statements deliberately retain a conservative reparse fallback.
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
        if(!minias_relayout_fragments(as) ||
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
        if(passes!=0U)
            fprintf(diagnostic,"minias-fragment-relayout:inplace-passes=%zu\n",
                    passes);
        fprintf(diagnostic,
                "minias-fragment-graph:anchors_built=%u relayouts=%zu branches=%zu reparse=0\n",
                (unsigned)(g!=NULL),g?g->relayout_count:0U,*forced_count);
    }
    return true;
}
