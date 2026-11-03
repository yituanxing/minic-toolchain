/*
 * Final-link input-section ownership and one static ET_EXEC pipeline.
 *
 * Link scripts and GC must see individual ELF input sections before layout,
 * even though the legacy reader initially concatenates same-named sections.
 * Static and shared script links reuse this owner.  The static pipeline
 * materializes only when a script or GC needs source identity, preserving
 * the ordinary no-script fast path.  ET_REL stays independent.
 */
#include "linker_core.inc"

static bool minild_gc_sections_enabled;

void minild_cli_enable_gc_sections_v0(void);
int minild_link_static_elf64_riscv_inputs_options(
    const char *output_path,
    const MiniLdInput *inputs,
    size_t input_count,
    const MiniLdStaticOptions *options,
    FILE *diagnostics);
int minild_link_static_elf64_riscv_inputs(const char *output_path,
                                          const MiniLdInput *inputs,
                                          size_t input_count,
                                          const char *entry_symbol,
                                          FILE *diagnostics);

void minild_cli_enable_gc_sections_v0(void) {
    minild_gc_sections_enabled = true;
}

/*
 * Re-materialize exact input sections for scripts and GC.  Their bytes, alignment and
 * section ownership were captured before legacy name-based concatenation.
 * Crucially, padding between concatenated inputs is NOT section content.
 *
 * This pass runs before any layout/relaxation and leaves source ownership
 * intact for output-section layout, COMDAT and link-order dependencies.
 */
static bool minild_materialize_input_sections(MiniLdState *state,
                                               bool include_nonalloc) {
    const size_t merged_count = state->work.count;
    bool *had_source = NULL;
    size_t restored = 0U;
    size_t i;
    size_t index_capacity = 64U;
    size_t target;
    bool ok = false;

    if (state->work.placement_count != state->inputs.section_count) {
        fprintf(state->diagnostics, "minic-ld: invalid-input-work-ownership\n");
        return false;
    }
    had_source = calloc(merged_count == 0U ? 1U : merged_count,
                        sizeof(*had_source));
    if (had_source == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:gc-source-map\n");
        return false;
    }

    for (i = 0U; i < state->inputs.section_count; ++i) {
        MiniLdInputSection *input = &state->inputs.sections[i];
        MiniLdInputPlacement *placement = &state->work.placements[i];
        MiniLdSection *output;
        size_t next_section;
        size_t name_len;
        int len;

        /* SHT_GROUP is a source-only ownership node. RISC-V attributes are
         * an intentional merged vendor record whose repeated 'A' markers
         * cannot be copied as independent output fragments. All other
         * admitted inputs, including non-ALLOC debug/comment metadata, must
         * retain file selection and stable order under linker scripts. */
        if (placement->merged_section == SIZE_MAX ||
            input->type == MINILD_SHT_RISCV_ATTRIBUTES ||
            (!include_nonalloc &&
             (input->flags & (SHF_ALLOC | SHF_LINK_ORDER)) == 0U)) {
            continue;
        }
        if (placement->merged_section >= merged_count) {
            fprintf(state->diagnostics, "minic-ld: gc-invalid-input-map\n");
            goto done;
        }
        if (input->file >= state->inputs.file_count) {
            fprintf(state->diagnostics, "minic-ld: invalid-input-file-owner\n");
            goto done;
        }
        if (!ensure_section_capacity(state)) {
            fprintf(state->diagnostics, "minic-ld: out-of-memory:gc-sections\n");
            goto done;
        }
        len = snprintf(NULL, 0, "%s.__minild_gc_%zu_%zu",
                       input->name, input->file, input->elf_index);
        if (len < 0) {
            fprintf(state->diagnostics, "minic-ld: gc-section-name\n");
            goto done;
        }
        name_len = (size_t)len;
        output = &state->work.sections[state->work.count];
        memset(output, 0, sizeof(*output));
        output->name = malloc(name_len + 1U);
        if (output->name == NULL) {
            fprintf(state->diagnostics, "minic-ld: out-of-memory:gc-name\n");
            goto done;
        }
        (void)snprintf(output->name, name_len + 1U,
                       "%s.__minild_gc_%zu_%zu",
                       input->name, input->file, input->elf_index);
        output->type = input->type;
        output->flags = input->flags & ~(uint64_t)SHF_GROUP;
        output->align = input->align;
        output->entsize = input->entsize;
        output->input_name = input->name;
        output->input_path = state->inputs.files[input->file].path;
        output->output_name = input->name;
        next_section = state->work.count++;
        if ((input->type == SHT_NOBITS &&
             !section_append_zero(output, input->size)) ||
            (input->type != SHT_NOBITS &&
             !section_append_data(output, input->data, input->size))) {
            fprintf(state->diagnostics,
                    "minic-ld: out-of-memory:gc-input-bytes:%s\n",
                    input->name);
            goto done;
        }
        had_source[placement->merged_section] = true;
        placement->current_section = next_section;
        ++restored;
    }

    for (i = 0U; i < state->inputs.section_count; ++i) {
        const MiniLdInputSection *input = &state->inputs.sections[i];
        const MiniLdInputPlacement *placement = &state->work.placements[i];
        MiniLdSection *output;

        if (placement->current_section == SIZE_MAX ||
            input->link_order_input == SIZE_MAX) {
            continue;
        }
        if (input->link_order_input >= state->inputs.section_count) {
            fprintf(state->diagnostics, "minic-ld: gc-invalid-link-order\n");
            goto done;
        }
        if (state->work.placements[input->link_order_input].current_section ==
            SIZE_MAX) {
            fprintf(state->diagnostics,
                    "minic-ld: gc-link-order-target-not-alloc\n");
            goto done;
        }
        output = &state->work.sections[placement->current_section];
        output->has_link_order_target = true;
        output->link_order_target =
            state->work.placements[input->link_order_input].current_section;
    }

    for (i = 0U; i < state->resolver.count; ++i) {
        MiniLdResolvedSymbol *symbol = &state->resolver.entries[i];
        const MiniLdInputSymbol *source;

        if (symbol->source_symbol == SIZE_MAX) {
            continue;
        }
        if (symbol->source_symbol >= state->inputs.symbol_count) {
            fprintf(state->diagnostics, "minic-ld: gc-invalid-symbol-source\n");
            goto done;
        }
        source = &state->inputs.symbols[symbol->source_symbol];
        if (source->canonical_symbol != i ||
            source->file >= state->inputs.file_count) {
            fprintf(state->diagnostics, "minic-ld: gc-invalid-symbol-owner\n");
            goto done;
        }
        if (source->input_section == SIZE_MAX) {
            continue;
        }
        if (source->input_section >= state->inputs.section_count) {
            fprintf(state->diagnostics, "minic-ld: gc-invalid-symbol-source\n");
            goto done;
        }
        if (state->work.placements[source->input_section].current_section !=
            SIZE_MAX) {
            symbol->section = (int)state->work.placements[
                source->input_section].current_section;
            symbol->value = source->value;
        }
    }
    for (i = 0U; i < state->reloc_count; ++i) {
        MiniLdReloc *reloc = &state->relocs[i];

        if (reloc->input_section == SIZE_MAX ||
            reloc->input_section >= state->inputs.section_count) {
            fprintf(state->diagnostics, "minic-ld: gc-invalid-reloc-source\n");
            goto done;
        }
        if (state->work.placements[reloc->input_section].current_section !=
            SIZE_MAX) {
            reloc->section = state->work.placements[
                reloc->input_section].current_section;
            reloc->offset = reloc->input_offset;
        }
    }

    for (i = 0U; i < merged_count; ++i) {
        if (had_source[i]) {
            /* The original merged buffer and its maximum alignment are no
             * longer layout inputs.  Leaving either in the state introduces
             * padding from an empty holder before the real input pieces. */
            free(state->work.sections[i].data);
            state->work.sections[i].data = NULL;
            state->work.sections[i].size = 0U;
            state->work.sections[i].capacity = 0U;
            state->work.sections[i].align = 1U;
            state->work.sections[i].gc_discarded = true;
        }
    }
    for (i = 0U; i < state->work.count; ++i) {
        state->work.sections[i].relocation_count = 0U;
    }
    for (i = 0U; i < state->reloc_count; ++i) {
        if (state->relocs[i].section >= state->work.count) {
            fprintf(state->diagnostics, "minic-ld: gc-reloc-section-range\n");
            goto done;
        }
        ++state->work.sections[state->relocs[i].section].relocation_count;
    }

    if (state->work.count > (SIZE_MAX - 1U) / 2U) {
        fprintf(state->diagnostics, "minic-ld: gc-index-overflow\n");
        goto done;
    }
    target = state->work.count * 2U + 1U;
    while (index_capacity < target) {
        if (index_capacity > SIZE_MAX / 2U) {
            fprintf(state->diagnostics, "minic-ld: gc-index-overflow\n");
            goto done;
        }
        index_capacity *= 2U;
    }
    if (!rebuild_section_index(state, index_capacity)) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:gc-index\n");
        goto done;
    }

    fprintf(state->diagnostics,
            "minic-ld: materialized-input-sections:source=%zu:restored=%zu\n",
            state->inputs.section_count, restored);
    ok = true;
done:
    free(had_source);
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
    for (i = 0U; i < state->reloc_count; ++i) {
        if (state->relocs[i].section >= count) {
            goto invalid;
        }
    }
    for (i = 0U; i < state->work.placement_count; ++i) {
        size_t section = state->work.placements[i].current_section;

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
    for (i = 0U; i < state->reloc_count; ++i) {
        state->relocs[i].section = remap[state->relocs[i].section];
    }
    for (i = 0U; i < count; ++i) {
        if (ordered[i].has_link_order_target) {
            ordered[i].link_order_target = remap[ordered[i].link_order_target];
        }
    }
    for (i = 0U; i < state->work.placement_count; ++i) {
        MiniLdInputPlacement *place = &state->work.placements[i];

        if (place->current_section != SIZE_MAX) {
            place->current_section = remap[place->current_section];
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
        size_t section_index = state->work.placements[i].current_section;

        if (section_index == SIZE_MAX) {
            continue;
        }
        if (section_index >= count) {
            fprintf(state->diagnostics,
                    "minic-ld: gc-input-order-source-section\n");
            goto done;
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
            "minic-ld: materialized-input-order:sections=%zu:ordered=%zu\n",
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

static bool minild_gc_name_has_prefix(const char *name, const char *prefix) {
    size_t length = strlen(prefix);

    return strcmp(name, prefix) == 0 ||
           (strncmp(name, prefix, length) == 0 && name[length] == '.');
}

/* The script selects ORIGINAL source sections before any reachability
 * decision. Only the first matching script pattern owns a section, just as
 * the layout evaluator later does. A /DISCARD/ choice prevents traversal of
 * the section; KEEP is a GC root, not an output-placement side effect. */
static bool minild_select_script_inputs(MiniLdState *state,
                                        const MiniLdScript *script) {
    size_t command_index;
    size_t kept = 0U;
    size_t discarded = 0U;

    for (command_index = 0U; command_index < script->command_count;
         ++command_index) {
        const MiniLdScriptCommand *command = &script->commands[command_index];
        const MiniLdScriptOutputSection *output;
        size_t item_index;

        if (command->kind != MINILD_SCRIPT_OUTPUT_SECTION) {
            continue;
        }
        output = &command->value.section;
        for (item_index = 0U; item_index < output->item_count; ++item_index) {
            const MiniLdScriptSectionItem *item = &output->items[item_index];
            size_t i;

            if (item->kind != MINILD_SCRIPT_SECTION_PATTERN ||
                item->value.pattern.common) {
                continue;
            }
            for (i = 0U; i < state->inputs.section_count; ++i) {
                const MiniLdInputSection *source = &state->inputs.sections[i];
                size_t current_section = state->work.placements[i].current_section;
                MiniLdSection *section;

                if (current_section == SIZE_MAX ||
                    source->file >= state->inputs.file_count ||
                    !minild_script_pattern_matches_input(
                        &item->value.pattern, source->name,
                        state->inputs.files[source->file].path)) {
                    continue;
                }
                if (current_section >= state->work.count) {
                    fprintf(state->diagnostics,
                            "minic-ld: invalid-script-source-section\n");
                    return false;
                }
                section = &state->work.sections[current_section];
                if (section->script_selected) {
                    continue;
                }
                section->script_selected = true;
                if (output->discard) {
                    section->script_discard = true;
                    ++discarded;
                } else if (item->value.pattern.keep) {
                    section->script_keep = true;
                    ++kept;
                }
            }
            /* COMMON and legacy non-allocating metadata have no restored
             * source fragment. Give those sections the same first-match
             * policy while keeping InputSection selection authoritative. */
            for (i = 0U; i < state->work.count; ++i) {
                MiniLdSection *section = &state->work.sections[i];

                if (section->gc_discarded || section->script_selected ||
                    section->input_name != NULL ||
                    !minild_script_pattern_matches_input(
                        &item->value.pattern, section->name,
                        section->input_path)) {
                    continue;
                }
                section->script_selected = true;
                if (output->discard) {
                    section->script_discard = true;
                    ++discarded;
                } else if (item->value.pattern.keep) {
                    section->script_keep = true;
                    ++kept;
                }
            }
        }
    }
    fprintf(state->diagnostics,
            "minic-ld: script-input-selection:keep=%zu:discard=%zu\n",
            kept, discarded);
    return true;
}

/* Resolve script /DISCARD/ against real source inputs before the shared
 * metadata scan. GOT, PLT, TLS and .rela.dyn sizing must only see live
 * relocations.  A removed definition is ABS zero (GNU's script-discard
 * convention), never a newly introduced dynamic undefined dependency.
 * Unlike GC, /DISCARD/ does not automatically kill other COMDAT members. */
static bool minild_shared_prune_script_inputs(MiniLdState *state,
                                               const MiniLdScript *script) {
    bool *discarded = NULL;
    size_t previous_reloc_count = state->reloc_count;
    size_t remaining_relocs = 0U;
    size_t discarded_count = 0U;
    size_t i;
    bool changed;
    bool ok = false;

    if (!minild_select_script_inputs(state, script)) {
        return false;
    }
    discarded = calloc(state->work.count == 0U
                           ? 1U : state->work.count,
                       sizeof(*discarded));
    if (discarded == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:shared-script-discard\n");
        return false;
    }
    for (i = 0U; i < state->work.count; ++i) {
        discarded[i] = state->work.sections[i].script_discard;
    }

    /* SHF_LINK_ORDER associates metadata with a target input section.
     * Discarding the target must also remove its dependent metadata so that
     * no final sh_link points into an absent output section. */
    do {
        changed = false;
        for (i = 0U; i < state->work.count; ++i) {
            const MiniLdSection *section = &state->work.sections[i];
            if (discarded[i] || !section->has_link_order_target) {
                continue;
            }
            if (section->link_order_target >= state->work.count) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-shared-link-order-target\n");
                goto done;
            }
            if (discarded[section->link_order_target]) {
                discarded[i] = true;
                changed = true;
            }
        }
    } while (changed);

    for (i = 0U; i < state->reloc_count; ++i) {
        const MiniLdReloc *reloc = &state->relocs[i];
        const MiniLdResolvedSymbol *symbol;
        if (reloc->section >= state->work.count) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-shared-script-reloc-source\n");
            goto done;
        }
        if (discarded[reloc->section] ||
            reloc->type == R_RISCV_NONE ||
            reloc->type == R_RISCV_RELAX ||
            reloc->type == R_RISCV_ALIGN ||
            reloc->symbol == SIZE_MAX) {
            continue;
        }
        if (reloc->symbol >= state->resolver.count) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-shared-script-reloc-symbol\n");
            goto done;
        }
        symbol = &state->resolver.entries[reloc->symbol];
        if (symbol->section >= 0 &&
            (size_t)symbol->section < state->work.count &&
            discarded[symbol->section]) {
            fprintf(state->diagnostics,
                    "minic-ld: shared-relocation-to-discarded-symbol:%s\n",
                    symbol->name);
            goto done;
        }
    }

    for (i = 0U; i < state->resolver.count; ++i) {
        MiniLdResolvedSymbol *symbol = &state->resolver.entries[i];
        if (symbol->section >= 0 &&
            (size_t)symbol->section < state->work.count &&
            discarded[symbol->section]) {
            symbol->section = MINILD_SECTION_ABS;
            symbol->value = 0U;
            symbol->size = 0U;
        }
    }
    for (i = 0U; i < state->work.count; ++i) {
        MiniLdSection *section = &state->work.sections[i];
        section->relocation_count = 0U;
        if (discarded[i]) {
            section->gc_discarded = true;
            section->size = 0U;
            section->align = 1U;
            ++discarded_count;
        }
    }
    for (i = 0U; i < previous_reloc_count; ++i) {
        MiniLdReloc reloc = state->relocs[i];
        if (!discarded[reloc.section]) {
            state->relocs[remaining_relocs++] = reloc;
            ++state->work.sections[reloc.section].relocation_count;
        }
    }
    state->reloc_count = remaining_relocs;
    fprintf(state->diagnostics,
            "minic-ld: shared-script-discard:sections=%zu:relocs=%zu->%zu\n",
            discarded_count, previous_reloc_count, remaining_relocs);
    ok = true;
done:
    free(discarded);
    return ok;
}

static bool minild_gc_root_section(const MiniLdSection *section) {
    const char *name = section->input_name != NULL
                           ? section->input_name : section->name;

    if ((section->flags & SHF_ALLOC) == 0U) {
        return false;
    }
#ifdef SHF_GNU_RETAIN
    if ((section->flags & SHF_GNU_RETAIN) != 0U) {
        return true;
    }
#endif
    return strcmp(name, ".init") == 0 ||
           strcmp(name, ".fini") == 0 ||
           minild_gc_name_has_prefix(name, ".preinit_array") ||
           minild_gc_name_has_prefix(name, ".init_array") ||
           minild_gc_name_has_prefix(name, ".fini_array") ||
           minild_gc_name_has_prefix(name, ".ctors") ||
           minild_gc_name_has_prefix(name, ".dtors");
}

static bool minild_gc_mark_section(const MiniLdState *state,
                                   size_t section_index,
                                   bool *live,
                                   size_t *queue,
                                   size_t *queue_size) {
    if (section_index >= state->work.count || live[section_index] ||
        state->work.sections[section_index].script_discard ||
        state->work.sections[section_index].gc_discarded ||
        (state->work.sections[section_index].flags &
         (SHF_ALLOC | SHF_LINK_ORDER)) == 0U) {
        return true;
    }
    if (*queue_size >= state->work.count) {
        return false;
    }
    live[section_index] = true;
    queue[(*queue_size)++] = section_index;
    return true;
}

static bool minild_gc_prune_static(MiniLdState *state,
                                   const char *entry_symbol) {
    bool *live = NULL;
    size_t *heads = NULL;
    size_t *next = NULL;
    size_t *queue = NULL;
    size_t *section_input = NULL;
    size_t *group_heads = NULL;
    size_t *group_next = NULL;
    size_t *dependent_heads = NULL;
    size_t *dependent_next = NULL;
    size_t queue_head = 0U;
    size_t queue_size = 0U;
    size_t old_reloc_count = state->reloc_count;
    size_t write_reloc = 0U;
    size_t discarded = 0U;
    size_t i;
    size_t entry_index;
    bool ok = false;

    live = calloc(state->work.count == 0U ? 1U : state->work.count,
                  sizeof(*live));
    heads = malloc((state->work.count == 0U ? 1U : state->work.count) *
                   sizeof(*heads));
    next = malloc((state->reloc_count == 0U ? 1U : state->reloc_count) *
                  sizeof(*next));
    queue = malloc((state->work.count == 0U ? 1U : state->work.count) *
                   sizeof(*queue));
    section_input = malloc((state->work.count == 0U ? 1U : state->work.count) *
                           sizeof(*section_input));
    dependent_heads = malloc((state->work.count == 0U ? 1U : state->work.count) *
                             sizeof(*dependent_heads));
    group_heads = malloc((state->inputs.section_count == 0U ? 1U : state->inputs.section_count) *
                         sizeof(*group_heads));
    group_next = malloc((state->inputs.section_count == 0U ? 1U : state->inputs.section_count) *
                        sizeof(*group_next));
    dependent_next = malloc((state->inputs.section_count == 0U ? 1U : state->inputs.section_count) *
                            sizeof(*dependent_next));
    if (live == NULL || heads == NULL || next == NULL || queue == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:gc-sections\n");
        goto done;
    }
    if (section_input == NULL || dependent_heads == NULL || group_heads == NULL ||
        group_next == NULL || dependent_next == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:gc-dependencies\n");
        goto done;
    }
    for (i = 0U; i < state->work.count; ++i) {
        heads[i] = SIZE_MAX;
        section_input[i] = SIZE_MAX;
        dependent_heads[i] = SIZE_MAX;
    }
    for (i = 0U; i < state->inputs.section_count; ++i) {
        group_heads[i] = SIZE_MAX;
    }
    for (i = 0U; i < state->inputs.section_count; ++i) {
        const MiniLdInputSection *input = &state->inputs.sections[i];
        size_t current_section = state->work.placements[i].current_section;

        group_next[i] = SIZE_MAX;
        dependent_next[i] = SIZE_MAX;
        if (current_section == SIZE_MAX) {
            continue;
        }
        section_input[current_section] = i;
        if (input->group_input != SIZE_MAX) {
            if (input->group_input >= state->inputs.section_count) {
                fprintf(state->diagnostics, "minic-ld: gc-invalid-group\n");
                goto done;
            }
            group_next[i] = group_heads[input->group_input];
            group_heads[input->group_input] = i;
        }
        if (input->link_order_input != SIZE_MAX) {
            size_t target;

            if (input->link_order_input >= state->inputs.section_count) {
                fprintf(state->diagnostics, "minic-ld: gc-invalid-order\n");
                goto done;
            }
            target = state->work.placements[input->link_order_input].current_section;
            if (target == SIZE_MAX) {
                fprintf(state->diagnostics, "minic-ld: gc-unmapped-order-target\n");
                goto done;
            }
            dependent_next[i] = dependent_heads[target];
            dependent_heads[target] = i;
        }
    }
    for (i = 0U; i < state->reloc_count; ++i) {
        size_t source = state->relocs[i].section;

        next[i] = SIZE_MAX;
        if (source >= state->work.count) {
            fprintf(state->diagnostics,
                    "minic-ld: gc-invalid-relocation-section\n");
            goto done;
        }
        next[i] = heads[source];
        heads[source] = i;
    }

    entry_index = symbol_resolver_lookup(&state->resolver, entry_symbol);
    if (entry_index == SIZE_MAX ||
        state->resolver.entries[entry_index].section < 0 ||
        (size_t)state->resolver.entries[entry_index].section >= state->work.count) {
        fprintf(state->diagnostics,
                "minic-ld: undefined-entry:%s\n",
                entry_symbol);
        goto done;
    }
    if (!minild_gc_mark_section(state,
                                (size_t)state->resolver.entries[entry_index].section,
                                live,
                                queue,
                                &queue_size)) {
        fprintf(state->diagnostics, "minic-ld: gc-root-overflow\n");
        goto done;
    }

    for (i = 0U; i < state->work.count; ++i) {
        if ((minild_gc_root_section(&state->work.sections[i]) ||
             state->work.sections[i].script_keep) &&
            !minild_gc_mark_section(state, i, live, queue, &queue_size)) {
            fprintf(state->diagnostics, "minic-ld: gc-root-overflow\n");
            goto done;
        }
    }

    while (queue_head < queue_size) {
        size_t source = queue[queue_head++];
        size_t relocation_index;
        size_t owner = section_input[source];

        if (owner != SIZE_MAX) {
            const MiniLdInputSection *input = &state->inputs.sections[owner];
            size_t member;

            if (input->group_input != SIZE_MAX) {
                for (member = group_heads[input->group_input];
                     member != SIZE_MAX; member = group_next[member]) {
                    if (!minild_gc_mark_section(
                            state, state->work.placements[member].current_section,
                            live, queue, &queue_size)) {
                        fprintf(state->diagnostics,
                                "minic-ld: gc-group-overflow\n");
                        goto done;
                    }
                }
            }
        }
        for (size_t dependent = dependent_heads[source];
             dependent != SIZE_MAX; dependent = dependent_next[dependent]) {
            if (!minild_gc_mark_section(
                    state, state->work.placements[dependent].current_section,
                    live, queue, &queue_size)) {
                fprintf(state->diagnostics,
                        "minic-ld: gc-link-order-overflow\n");
                goto done;
            }
        }

        for (relocation_index = heads[source];
             relocation_index != SIZE_MAX;
             relocation_index = next[relocation_index]) {
            MiniLdReloc *reloc = &state->relocs[relocation_index];
            MiniLdResolvedSymbol *target;

            if (reloc->symbol == SIZE_MAX ||
                reloc->symbol >= state->resolver.count) {
                continue;
            }
            target = &state->resolver.entries[reloc->symbol];
            if (target->section < 0 ||
                (size_t)target->section >= state->work.count) {
                continue;
            }
            if (!minild_gc_mark_section(state,
                                        (size_t)target->section,
                                        live,
                                        queue,
                                        &queue_size)) {
                fprintf(state->diagnostics, "minic-ld: gc-queue-overflow\n");
                goto done;
            }
        }
    }

    for (i = 0U; i < state->work.count; ++i) {
        state->work.sections[i].relocation_count = 0U;
        if (((state->work.sections[i].flags & (SHF_ALLOC | SHF_LINK_ORDER)) != 0U ||
             state->work.sections[i].script_discard) &&
            !live[i]) {
            state->work.sections[i].size = 0U;
            state->work.sections[i].gc_discarded = true;
            state->work.sections[i].align = 1U;
            ++discarded;
        }
    }

    for (i = 0U; i < state->resolver.count; ++i) {
        MiniLdResolvedSymbol *symbol = &state->resolver.entries[i];

        if (symbol->section >= 0 &&
            (size_t)symbol->section < state->work.count &&
            !live[symbol->section]) {
            symbol->section = MINILD_SECTION_UNDEF;
            symbol->value = 0U;
            symbol->size = 0U;
        }
    }

    for (i = 0U; i < old_reloc_count; ++i) {
        MiniLdReloc reloc = state->relocs[i];

        if (reloc.section >= state->work.count ||
            !live[reloc.section]) {
            continue;
        }
        state->relocs[write_reloc++] = reloc;
        ++state->work.sections[reloc.section].relocation_count;
    }
    state->reloc_count = write_reloc;

    fprintf(state->diagnostics,
            "minic-ld: gc-sections:live=%zu:discarded=%zu:relocs=%zu->%zu\n",
            queue_size,
            discarded,
            old_reloc_count,
            write_reloc);
    ok = true;

done:
    free(dependent_next);
    free(dependent_heads);
    free(group_next);
    free(group_heads);
    free(section_input);
    free(queue);
    free(next);
    free(heads);
    free(live);
    return ok;
}

static int minild_link_static_pipeline(
    const char *output_path,
    const MiniLdInput *inputs,
    size_t input_count,
    const MiniLdStaticOptions *options,
    bool garbage_collect,
    FILE *diagnostics) {
    MiniLdState state;
    MiniLdStaticLayout layout;
    MiniLdStaticGot got;
    MiniLdScript script;
    const char *entry_symbol;
    uint64_t entry = 0U;
    bool have_script = false;
    bool layout_ready = false;
    bool ok = false;
    clock_t phase_start = clock();

    if (output_path == NULL || inputs == NULL || input_count == 0U ||
        diagnostics == NULL) {
        return 2;
    }
    memset(&state, 0, sizeof(state));
    memset(&layout, 0, sizeof(layout));
    memset(&got, 0, sizeof(got));
    minild_script_initialize(&script);
    got.section = SIZE_MAX;
    state.diagnostics = diagnostics;
    if (options != NULL && options->script_path != NULL) {
        if (!minild_script_parse_file(options->script_path, &script,
                                      diagnostics)) {
            goto done;
        }
        if (script.output_arch != NULL &&
            strcmp(script.output_arch, "riscv") != 0) {
            fprintf(diagnostics,
                    "minic-ld: unsupported-linker-script-arch:%s\n",
                    script.output_arch);
            goto done;
        }
        if (script.phdr_count != 0U || script.version_name != NULL) {
            fprintf(diagnostics,
                    "minic-ld: unsupported-static-script-PHDRS-or-VERSION\n");
            goto done;
        }
        have_script = true;
    }
    entry_symbol = options != NULL && options->entry_symbol != NULL
                       ? options->entry_symbol
                       : have_script && script.entry_symbol != NULL
                             ? script.entry_symbol
                             : "_start";

    if (!process_input_sequence(&state, inputs, input_count)) {
        goto done;
    }
    minild_perf_phase("inputs", &phase_start, &state, diagnostics);

    if (!state.have_input ||
        ((garbage_collect || have_script) &&
         (!minild_materialize_input_sections(&state, have_script) ||
          !minild_restore_input_order(&state))) ||
        !static_allocate_common(&state) ||
        (garbage_collect && have_script &&
         !minild_select_script_inputs(&state, &script)) ||
        (garbage_collect && !minild_gc_prune_static(&state, entry_symbol)) ||
        (garbage_collect && !have_script &&
         (!minild_gc_group_default_output_classes(&state) ||
          !minild_gc_sort_default_output_alignment(&state))) ||
        !static_build_got(&state, &got) ||
        !(have_script
              ? static_build_script_layout(&state, &script, &layout, 0U)
              : static_build_layout(&state, &layout))) {
        goto done;
    }
    layout_ready = true;
    minild_perf_phase("layout", &phase_start, &state, diagnostics);

    if (!static_synthesize_runtime_boundaries(&state, &layout) ||
        !static_fill_got(&state, &layout, &got) ||
        !static_apply_relocations(&state, &layout, &got)) {
        goto done;
    }
    minild_perf_phase("relocations", &phase_start, &state, diagnostics);

    if (!static_entry_address(&state, &layout, entry_symbol, &entry) ||
        !static_write_executable(&state, &layout, output_path, entry)) {
        goto done;
    }
    minild_perf_phase("output", &phase_start, &state, diagnostics);
    ok = true;

done:
    if (layout_ready) {
        static_layout_destroy(&layout);
    }
    minild_script_destroy(&script);
    static_got_destroy(&got);
    state_destroy(&state);
    minild_perf_phase("cleanup", &phase_start, &state, diagnostics);
    return ok ? 0 : 1;
}

int minild_link_static_elf64_riscv_inputs_options(
    const char *output_path,
    const MiniLdInput *inputs,
    size_t input_count,
    const MiniLdStaticOptions *options,
    FILE *diagnostics) {
    return minild_link_static_pipeline(output_path,
                                       inputs,
                                       input_count,
                                       options,
                                       minild_gc_sections_enabled,
                                       diagnostics);
}

int minild_link_static_elf64_riscv_inputs(const char *output_path,
                                          const MiniLdInput *inputs,
                                          size_t input_count,
                                          const char *entry_symbol,
                                          FILE *diagnostics) {
    MiniLdStaticOptions options;

    options.entry_symbol = entry_symbol;
    options.script_path = NULL;
    return minild_link_static_elf64_riscv_inputs_options(output_path,
                                                          inputs,
                                                          input_count,
                                                          &options,
                                                          diagnostics);
}
