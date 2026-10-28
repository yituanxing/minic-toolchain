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


/* Statement ordinal is stable across re-layout; macro source lines are not. */
bool minias_force_long_branch_for_stmt(const MiniAs *as, size_t statement_id) {
    size_t i;

    for (i = 0U; i < as->forced_long_branch_count; ++i) {
        if (as->forced_long_branch_ids[i] == statement_id) {
            return true;
        }
    }
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
    size_t *start_head = NULL, *end_head = NULL;
    size_t *start_next = NULL, *end_next = NULL;
    size_t i;
    bool ok = false;

    if (n == SIZE_MAX || n + 1U > SIZE_MAX / sizeof(size_t) ||
        (nsym != 0U && nsym > SIZE_MAX / sizeof(size_t))) {
        minias_set_error(as, "fragment-anchor-overflow");
        return false;
    }
    start_head = malloc((n + 1U) * sizeof(size_t));
    end_head = malloc((n + 1U) * sizeof(size_t));
    start_next = malloc((nsym == 0U ? 1U : nsym) * sizeof(size_t));
    end_next = malloc((nsym == 0U ? 1U : nsym) * sizeof(size_t));
    if (start_head == NULL || end_head == NULL ||
        start_next == NULL || end_next == NULL) {
        minias_set_error(as, "out-of-memory:fragment-anchors");
        goto done;
    }
    for (i = 0U; i <= n; ++i) {
        start_head[i] = SIZE_MAX;
        end_head[i] = SIZE_MAX;
    }
    for (i = 0U; i < as->subsection_count; ++i)
        as->subsections[i].size = 0U;

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
    free(start_head);
    free(end_head);
    free(start_next);
    free(end_next);
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
    size_t i;
    *added = false;
    if (as->target->relax_instruction == NULL) return true;
    for (i = 0U; i < as->stmt_count; ++i) {
        const MiniAsStmt *stmt = &as->stmts[i];
        uint32_t expanded = stmt->size;
        size_t j;
        bool known = false;
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
        for (j = 0U; j < *count; ++j) {
            if ((*ids)[j] == i) { known = true; break; }
        }
        if (known) continue;
        if (!minias_grow_array((void **)ids, capacity, sizeof(**ids), *count + 1U)) {
            minias_set_error(as, "out-of-memory:branch-relaxation");
            return false;
        }
        (*ids)[(*count)++] = i;
        *added = true;
    }
    return true;
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
    if(passes!=0U && diagnostic!=NULL &&
       getenv("MINIAS_TRACE_RELAYOUT")!=NULL)
        fprintf(diagnostic,"minias-fragment-relayout:inplace-passes=%zu\n",
                passes);
    return true;
}
