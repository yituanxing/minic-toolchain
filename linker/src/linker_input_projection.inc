/* The input graph owns exact source bytes throughout archive/COMDAT
 * admission.  This one post-admission projection chooses either coalesced
 * name-based work sections or exact source fragments for script/GC.  In
 * particular a script/GC link never builds and then throws away an eager
 * merged byte buffer. All map and coordinate updates commit together. */
static bool minild_projection_fragment(const MiniLdState *state,
                                        MiniLdInputProjection projection,
                                        const MiniLdInputSection *input) {
    if (minild_target_is_attributes_section(state, input->type)) {
        return false;
    }
    return projection == MINILD_PROJECTION_SCRIPT ||
           (projection == MINILD_PROJECTION_GC &&
            (input->flags & (SHF_ALLOC | SHF_LINK_ORDER)) != 0U);
}

static bool minild_project_admitted_inputs(MiniLdState *state,
                                           MiniLdInputProjection projection) {
    MiniLdSectionWorkspace next = {0};
    size_t *source_base = NULL;
    const size_t source_count = state->inputs.section_count;
    size_t fragments = 0U;
    size_t i;
    bool ok = false;

    if (projection < MINILD_PROJECTION_RELOCATABLE ||
        projection > MINILD_PROJECTION_SCRIPT ||
        state->work.count != 0U || state->work.placement_count != 0U ||
        !relocations_require_editable(&state->relocations,
                                       state->diagnostics,
                                       "input-projection")) {
        fprintf(state->diagnostics,
                "minic-ld: invalid-input-projection-phase\n");
        return false;
    }
    if (source_count > SIZE_MAX / sizeof(*next.placements) ||
        source_count > SIZE_MAX / sizeof(*source_base)) {
        fprintf(state->diagnostics,
                "minic-ld: input-projection-count-overflow\n");
        goto done;
    }
    next.placements = malloc((source_count == 0U ? 1U : source_count) *
                             sizeof(*next.placements));
    source_base = calloc(source_count == 0U ? 1U : source_count,
                         sizeof(*source_base));
    if (next.placements == NULL || source_base == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:input-projection-map\n");
        goto done;
    }
    next.placement_count = source_count;
    next.placement_capacity = source_count;
    for (i = 0U; i < source_count; ++i) {
        const MiniLdInputSection *input = &state->inputs.sections[i];
        const bool fragment = minild_projection_fragment(state, projection,
                                                         input);
        const bool distinct =
            fragment || (input->flags & SHF_LINK_ORDER) != 0U ||
            (projection == MINILD_PROJECTION_RELOCATABLE &&
             (input->group_input != SIZE_MAX || input->link_order_target));
        const uint64_t flags = projection == MINILD_PROJECTION_RELOCATABLE
                                   ? input->flags
                                   : input->flags & ~(uint64_t)SHF_GROUP;
        MiniLdSection *output;
        const char *name = input->name;
        char *generated_name = NULL;
        size_t index;
        size_t base = 0U;

        next.placements[i].work_section = SIZE_MAX;
        /* SHT_GROUP remains an immutable source-graph node. -r synthesizes
         * its live group headers from each projected member's signature. */
        if (input->type == SHT_GROUP) {
            continue;
        }
        if (input->file >= state->inputs.file_count) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-input-projection-owner:%s\n",
                    input->name);
            goto done;
        }
        if (fragment) {
            int len = snprintf(NULL, 0, "%s.__minild_gc_%zu_%zu",
                               input->name, input->file, input->elf_index);
            if (len < 0 || (size_t)len == SIZE_MAX) {
                fprintf(state->diagnostics,
                        "minic-ld: input-projection-fragment-name\n");
                goto done;
            }
            generated_name = malloc((size_t)len + 1U);
            if (generated_name == NULL) {
                fprintf(state->diagnostics,
                        "minic-ld: out-of-memory:input-projection-name\n");
                goto done;
            }
            (void)snprintf(generated_name, (size_t)len + 1U,
                           "%s.__minild_gc_%zu_%zu",
                           input->name, input->file, input->elf_index);
            name = generated_name;
        }
        if (!work_sections_find_or_add(
                &next, state->diagnostics, name, input->type,
                flags, input->align, input->entsize, distinct, &index)) {
            free(generated_name);
            goto done;
        }
        free(generated_name);
        output = &next.sections[index];
        if (fragment) {
            /* Non-owning original source names and paths survive script
             * selection. The generated work name is never a source fact. */
            output->input_name = input->name;
            output->input_path = state->inputs.files[input->file].path;
            ++fragments;
        }
        if (projection == MINILD_PROJECTION_RELOCATABLE &&
            input->group_input != SIZE_MAX) {
            const MiniLdInputSection *group;
            size_t signature_symbol;

            if (input->group_input >= source_count) {
                goto invalid;
            }
            group = &state->inputs.sections[input->group_input];
            if (group->type != SHT_GROUP || group->file != input->file ||
                group->size < 4U || group->data == NULL ||
                group->info == 0U) {
                goto invalid;
            }
            signature_symbol = input_graph_canonical_symbol_id(
                &state->inputs, input->file, (size_t)group->info);
            if (signature_symbol == SIZE_MAX) {
                goto invalid;
            }
            output->has_group = true;
            output->group_flags = input_group_u32(group->data);
            output->group_signature_symbol = signature_symbol;
        }
        if (minild_target_is_attributes_section(state, input->type)) {
            if (!minild_target_append_attributes(output, input)) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-projected-attributes:%s\n",
                        input->name);
                goto done;
            }
        } else if (!align_up_size(output->size, input->align, &base) ||
                   !section_append_zero(output, base - output->size) ||
                   (input->type == SHT_NOBITS
                        ? !section_append_zero(output, input->size)
                        : !section_append_data(output, input->data,
                                               input->size))) {
            fprintf(state->diagnostics,
                    "minic-ld: cannot-project-input:%s\n",
                    input->name);
            goto done;
        }
        next.placements[i].work_section = index;
        source_base[i] = base;
    }

    /* Resolve each LINK_ORDER edge against its original target input ID.
     * GC source fragments require an exact live target just as the former
     * materialization did; other policies preserve their coalesced target. */
    for (i = 0U; i < source_count; ++i) {
        const MiniLdInputSection *input = &state->inputs.sections[i];
        MiniLdSection *output;
        size_t target;

        if (input->link_order_input == SIZE_MAX) {
            continue;
        }
        if (input->link_order_input >= source_count ||
            next.placements[i].work_section == SIZE_MAX ||
            (target = next.placements[input->link_order_input]
                          .work_section) == SIZE_MAX ||
            (projection == MINILD_PROJECTION_GC &&
             next.sections[target].input_name == NULL)) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-projected-link-order\n");
            goto done;
        }
        output = &next.sections[next.placements[i].work_section];
        output->has_link_order_target = true;
        output->link_order_target = target;
    }

    /* Validate the immutable-source -> mutable-coordinate projection
     * entirely before updating any canonical symbol or relocation. */
    for (i = 0U; i < state->resolver.count; ++i) {
        const MiniLdResolvedSymbol *symbol = &state->resolver.entries[i];
        const MiniLdInputSymbol *source;
        size_t id;

        if (symbol->source_symbol == SIZE_MAX) {
            continue;
        }
        if (symbol->source_symbol >= state->inputs.symbol_count) {
            goto invalid;
        }
        source = &state->inputs.symbols[symbol->source_symbol];
        if (source->canonical_symbol != i) {
            goto invalid;
        }
        id = source->input_section;
        if (id == SIZE_MAX) {
            /* A defined source may remain provisional only until its
             * owning source section has a final work placement. */
            if (symbol->section == MINILD_SECTION_PENDING) {
                goto invalid;
            }
            continue;
        }
        if (id >= source_count ||
            next.placements[id].work_section == SIZE_MAX ||
            next.placements[id].work_section > INT_MAX ||
            source->value > UINT64_MAX - source_base[id]) {
            goto invalid;
        }
    }
    for (i = 0U; i < state->relocations.count; ++i) {
        const MiniLdReloc *reloc = &state->relocations.entries[i];
        size_t id = reloc->input_section;

        if (id >= source_count ||
            next.placements[id].work_section == SIZE_MAX ||
            reloc->input_offset > UINT64_MAX - source_base[id]) {
            goto invalid;
        }
    }

    for (i = 0U; i < state->resolver.count; ++i) {
        MiniLdResolvedSymbol *symbol = &state->resolver.entries[i];
        const MiniLdInputSymbol *source;
        size_t id;

        if (symbol->source_symbol == SIZE_MAX) {
            continue;
        }
        source = &state->inputs.symbols[symbol->source_symbol];
        id = source->input_section;
        if (id != SIZE_MAX) {
            symbol->section = (int)next.placements[id].work_section;
            symbol->value = source->value + source_base[id];
        }
    }
    for (i = 0U; i < state->relocations.count; ++i) {
        MiniLdReloc *reloc = &state->relocations.entries[i];
        size_t id = reloc->input_section;

        reloc->section = next.placements[id].work_section;
        reloc->offset = reloc->input_offset + source_base[id];
    }
    work_sections_destroy(&state->work);
    state->work = next;
    memset(&next, 0, sizeof(next));

    if (projection == MINILD_PROJECTION_GC ||
        projection == MINILD_PROJECTION_SCRIPT) {
        fprintf(state->diagnostics,
                "minic-ld: projected-input-sections:source=%zu:fragments=%zu:mode=%s\n",
                source_count, fragments,
                projection == MINILD_PROJECTION_GC ? "gc" : "script");
    }
    ok = true;
    goto done;

invalid:
    fprintf(state->diagnostics,
            "minic-ld: invalid-input-projection-map\n");
done:
    work_sections_destroy(&next);
    free(source_base);
    return ok;
}
typedef struct MiniLdInputSectionOrder {
    size_t old_index;
    size_t order;
} MiniLdInputSectionOrder;

static int minild_compare_input_section_order(const void *lhs,
                                              const void *rhs) {
    const MiniLdInputSectionOrder *a = lhs;
    const MiniLdInputSectionOrder *b = rhs;

    if (a->order < b->order) {
        return -1;
    }
    if (a->order > b->order) {
        return 1;
    }
    if (a->old_index < b->old_index) {
        return -1;
    }
    if (a->old_index > b->old_index) {
        return 1;
    }
    return 0;
}

/* Working-section indices are deliberately transient: once a permutation is
 * chosen, every consumer must move together.  Build the new name index before
 * touching any live references so allocation failure leaves the old workspace
 * intact. Input-file, input-section and canonical-symbol IDs never move. */
static bool minild_install_work_section_order(MiniLdState *state,
                                               MiniLdSection *ordered,
                                               const size_t *remap,
                                               const char *phase) {
    const size_t count = state->work.count;
    MiniLdSectionWorkspace next = {0};
    bool *seen = NULL;
    size_t index_capacity = 64U;
    size_t index_target;
    size_t i;

    if (state->work.placement_count != state->inputs.section_count ||
        count > (SIZE_MAX - 1U) / 2U) {
        goto invalid;
    }
    if (!relocations_require_editable(&state->relocations,
                                       state->diagnostics,
                                       "work-reorder")) {
        return false;
    }
    seen = calloc(count == 0U ? 1U : count, sizeof(*seen));
    if (seen == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:work-reorder-map:%s\n", phase);
        return false;
    }
    index_target = count * 2U + 1U;
    while (index_capacity < index_target) {
        if (index_capacity > SIZE_MAX / 2U) {
            goto invalid;
        }
        index_capacity *= 2U;
    }
    for (i = 0U; i < count; ++i) {
        if (remap[i] >= count || seen[remap[i]] ||
            (ordered[i].has_link_order_target &&
             ordered[i].link_order_target >= count)) {
            goto invalid;
        }
        seen[remap[i]] = true;
    }
    for (i = 0U; i < state->resolver.count; ++i) {
        int section = state->resolver.entries[i].section;

        if (section >= 0 && (size_t)section >= count) {
            goto invalid;
        }
    }
    for (i = 0U; i < state->relocations.count; ++i) {
        if (state->relocations.entries[i].section >= count) {
            goto invalid;
        }
    }
    for (i = 0U; i < state->work.placement_count; ++i) {
        size_t section = state->work.placements[i].work_section;

        if (section != SIZE_MAX && section >= count) {
            goto invalid;
        }
    }

    next.sections = ordered;
    next.count = count;
    next.capacity = count;
    if (!work_sections_rebuild_index(&next, index_capacity)) {
        free(seen);
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:work-reorder-index:%s\n", phase);
        return false;
    }

    for (i = 0U; i < state->resolver.count; ++i) {
        MiniLdResolvedSymbol *symbol = &state->resolver.entries[i];

        if (symbol->section >= 0) {
            symbol->section = (int)remap[symbol->section];
        }
    }
    for (i = 0U; i < state->relocations.count; ++i) {
        state->relocations.entries[i].section = remap[state->relocations.entries[i].section];
    }
    for (i = 0U; i < count; ++i) {
        if (ordered[i].has_link_order_target) {
            ordered[i].link_order_target = remap[ordered[i].link_order_target];
        }
    }
    for (i = 0U; i < state->work.placement_count; ++i) {
        MiniLdInputPlacement *place = &state->work.placements[i];

        if (place->work_section != SIZE_MAX) {
            place->work_section = remap[place->work_section];
        }
    }

    free(state->work.sections);
    free(state->work.index);
    state->work.sections = next.sections;
    state->work.count = next.count;
    state->work.capacity = next.capacity;
    state->work.index = next.index;
    state->work.index_capacity = next.index_capacity;
    free(seen);
    return true;

invalid:
    free(seen);
    fprintf(state->diagnostics,
            "minic-ld: invalid-work-reorder:%s\n", phase);
    return false;
}

/*
 * Restore the exact admission order of input sections, not an order inferred
 * from the optional STT_SECTION symbol table entries.  This avoids placing
 * large read-only fragments between two direct R_RISCV_JAL sites.
 */
static bool minild_restore_input_order(MiniLdState *state) {
    const size_t count = state->work.count;
    MiniLdInputSectionOrder *entries = NULL;
    MiniLdSection *ordered_sections = NULL;
    size_t *orders = NULL;
    size_t *remap = NULL;
    size_t ordered_count = 0U;
    size_t i;
    bool ok = false;

    entries = malloc((count == 0U ? 1U : count) * sizeof(*entries));
    ordered_sections =
        malloc((count == 0U ? 1U : count) * sizeof(*ordered_sections));
    orders = malloc((count == 0U ? 1U : count) * sizeof(*orders));
    remap = malloc((count == 0U ? 1U : count) * sizeof(*remap));
    if (entries == NULL || ordered_sections == NULL ||
        orders == NULL || remap == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:gc-input-order\n");
        goto done;
    }
    for (i = 0U; i < count; ++i) {
        orders[i] = SIZE_MAX;
    }

    for (i = 0U; i < state->inputs.section_count; ++i) {
        size_t section_index = state->work.placements[i].work_section;

        if (section_index == SIZE_MAX) {
            continue;
        }
        if (section_index >= count) {
            fprintf(state->diagnostics,
                    "minic-ld: gc-input-order-source-section\n");
            goto done;
        }
        if (state->work.sections[section_index].input_name == NULL) {
            continue;
        }
        if (orders[section_index] == SIZE_MAX) {
            orders[section_index] = i;
            ++ordered_count;
        }
    }

    for (i = 0U; i < count; ++i) {
        entries[i].old_index = i;
        entries[i].order =
            orders[i] == SIZE_MAX ? state->inputs.section_count + i : orders[i];
    }
    qsort(entries,
          count,
          sizeof(*entries),
          minild_compare_input_section_order);

    for (i = 0U; i < count; ++i) {
        const size_t old_index = entries[i].old_index;
        ordered_sections[i] = state->work.sections[old_index];
        remap[old_index] = i;
    }
    if (!minild_install_work_section_order(
            state, ordered_sections, remap, "input-order")) {
        goto done;
    }
    ordered_sections = NULL;
    fprintf(state->diagnostics,
            "minic-ld: projected-input-order:sections=%zu:ordered=%zu\n",
            count,
            ordered_count);
    ok = true;

done:
    free(ordered_sections);
    free(remap);
    free(orders);
    free(entries);
    return ok;
}


/*
 * GNU's default linker script groups executable input sections into
 * the text output region before read-only data.  The GC frontier
 * reconstructs original input-section identity, so preserving one
 * global input order across both code and rodata can incorrectly put
 * megabytes of live rodata between two direct R_RISCV_JAL sites.
 * Stable-partition the default non-script layout into output classes:
 * executable RX first, non-executable RX second, then everything
 * else.  Relative order inside each class remains the recovered input
 * order, so the earlier GC input-order contract is preserved.
 */
static int minild_gc_default_output_class(const MiniLdSection *section) {
    if ((section->flags & SHF_ALLOC) != 0U &&
        (section->flags & SHF_WRITE) == 0U) {
        return (section->flags & SHF_EXECINSTR) != 0U ? 0 : 1;
    }
    return 2;
}

static bool minild_gc_group_default_output_classes(MiniLdState *state) {
    const size_t count = state->work.count;
    MiniLdSection *ordered_sections = NULL;
    size_t *remap = NULL;
    size_t next = 0U;
    size_t executable_rx = 0U;
    size_t readonly_rx = 0U;
    size_t i;
    int output_class;
    bool ok = false;

    ordered_sections =
        malloc((count == 0U ? 1U : count) * sizeof(*ordered_sections));
    remap = malloc((count == 0U ? 1U : count) * sizeof(*remap));
    if (ordered_sections == NULL || remap == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:gc-output-classes\n");
        goto done;
    }

    for (output_class = 0; output_class < 3; ++output_class) {
        for (i = 0U; i < count; ++i) {
            if (minild_gc_default_output_class(&state->work.sections[i]) !=
                output_class) {
                continue;
            }
            ordered_sections[next] = state->work.sections[i];
            remap[i] = next;
            if (output_class == 0) {
                ++executable_rx;
            } else if (output_class == 1) {
                ++readonly_rx;
            }
            ++next;
        }
    }
    if (next != count) {
        fprintf(state->diagnostics,
                "minic-ld: gc-output-class-count-mismatch\n");
        goto done;
    }

    if (!minild_install_work_section_order(
            state, ordered_sections, remap, "output-classes")) {
        goto done;
    }
    ordered_sections = NULL;
    fprintf(state->diagnostics,
            "minic-ld: gc-output-classes:exec-rx=%zu:ro-rx=%zu\n",
            executable_rx,
            readonly_rx);
    ok = true;

done:
    free(ordered_sections);
    free(remap);
    return ok;
}


typedef struct MiniLdGcOutputAlignmentOrder {
    size_t old_index;
    int output_class;
    uint64_t align;
} MiniLdGcOutputAlignmentOrder;

static int minild_gc_compare_output_alignment(const void *lhs_ptr,
                                              const void *rhs_ptr) {
    const MiniLdGcOutputAlignmentOrder *lhs = lhs_ptr;
    const MiniLdGcOutputAlignmentOrder *rhs = rhs_ptr;

    if (lhs->output_class != rhs->output_class) {
        return lhs->output_class < rhs->output_class ? -1 : 1;
    }
    if (lhs->output_class < 2 && lhs->align != rhs->align) {
        return lhs->align > rhs->align ? -1 : 1;
    }
    if (lhs->old_index != rhs->old_index) {
        return lhs->old_index < rhs->old_index ? -1 : 1;
    }
    return 0;
}

/*
 * BusyBox's GNU reference link uses --sort-section=alignment.
 * Preserve the recovered input order for equal-alignment sections,
 * but sort executable and read-only alloc sections by decreasing
 * alignment inside their default output classes.  This matches the
 * range-relevant GNU wildcard ordering without disturbing writable
 * or metadata section order.
 */
static bool minild_gc_sort_default_output_alignment(MiniLdState *state) {
    const size_t count = state->work.count;
    MiniLdGcOutputAlignmentOrder *entries = NULL;
    MiniLdSection *ordered_sections = NULL;
    size_t *remap = NULL;
    size_t i;
    bool ok = false;

    entries = malloc((count == 0U ? 1U : count) * sizeof(*entries));
    ordered_sections =
        malloc((count == 0U ? 1U : count) * sizeof(*ordered_sections));
    remap = malloc((count == 0U ? 1U : count) * sizeof(*remap));
    if (entries == NULL || ordered_sections == NULL || remap == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:gc-output-alignment\n");
        goto done;
    }

    for (i = 0U; i < count; ++i) {
        entries[i].old_index = i;
        entries[i].output_class =
            minild_gc_default_output_class(&state->work.sections[i]);
        entries[i].align = state->work.sections[i].align;
    }
    qsort(entries,
          count,
          sizeof(*entries),
          minild_gc_compare_output_alignment);

    for (i = 0U; i < count; ++i) {
        size_t old_index = entries[i].old_index;
        ordered_sections[i] = state->work.sections[old_index];
        remap[old_index] = i;
    }
    if (!minild_install_work_section_order(
            state, ordered_sections, remap, "output-alignment")) {
        goto done;
    }
    ordered_sections = NULL;
    fprintf(state->diagnostics,
            "minic-ld: gc-output-alignment:sections=%zu\n",
            count);
    ok = true;

done:
    free(ordered_sections);
    free(remap);
    free(entries);
    return ok;
}
