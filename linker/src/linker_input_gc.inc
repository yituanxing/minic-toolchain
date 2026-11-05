static bool minild_gc_name_has_prefix(const char *name, const char *prefix) {
    size_t length = strlen(prefix);

    return strcmp(name, prefix) == 0 ||
           (strncmp(name, prefix, length) == 0 && name[length] == '.');
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
        state->work.sections[section_index].gc_discarded) {
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
                                   const char *entry_symbol,
                                   const MiniLdStaticOptions *options) {
    bool *live = NULL;
    bool *input_file_live = NULL;
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
    size_t old_reloc_count = state->relocations.count;
    size_t write_reloc = 0U;
    size_t discarded = 0U;
    size_t i;
    size_t entry_index;
    bool ok = false;

    if (!relocations_require_editable(&state->relocations,
                                       state->diagnostics,
                                       "gc-prune")) {
        return false;
    }
    live = calloc(state->work.count == 0U ? 1U : state->work.count,
                  sizeof(*live));
    input_file_live = calloc(state->inputs.file_count == 0U ? 1U :
                             state->inputs.file_count,
                             sizeof(*input_file_live));
    heads = malloc((state->work.count == 0U ? 1U : state->work.count) *
                   sizeof(*heads));
    next = malloc((state->relocations.count == 0U ? 1U : state->relocations.count) *
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
    if (live == NULL || input_file_live == NULL || heads == NULL ||
        next == NULL || queue == NULL) {
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
        size_t source_section = state->work.placements[i].work_section;

        group_next[i] = SIZE_MAX;
        dependent_next[i] = SIZE_MAX;
        if (source_section == SIZE_MAX) {
            continue;
        }
        if (source_section >= state->work.count ||
            state->work.sections[source_section].input_name == NULL) {
            if (source_section >= state->work.count) {
                fprintf(state->diagnostics,
                        "minic-ld: gc-invalid-source-section\n");
                goto done;
            }
            continue;
        }
        section_input[source_section] = i;
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
            target = state->work.placements[input->link_order_input].work_section;
            if (target == SIZE_MAX || target >= state->work.count ||
                state->work.sections[target].input_name == NULL) {
                fprintf(state->diagnostics, "minic-ld: gc-unmapped-order-target\n");
                goto done;
            }
            dependent_next[i] = dependent_heads[target];
            dependent_heads[target] = i;
        }
    }
    for (i = 0U; i < state->relocations.count; ++i) {
        size_t source = state->relocations.entries[i].section;

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

    /* GNU -u roots are retained even without an incoming relocation.
     * Absence of a definition is legal until actually referenced. */
    if (options != NULL) {
        for (i = 0U; i < options->forced_undefined_count; ++i) {
            const char *name = options->forced_undefined_symbols[i];
            size_t index = symbol_resolver_lookup(&state->resolver, name);

            if (index != SIZE_MAX &&
                state->resolver.entries[index].section >= 0 &&
                (size_t)state->resolver.entries[index].section <
                    state->work.count &&
                !minild_gc_mark_section(
                    state,
                    (size_t)state->resolver.entries[index].section,
                    live, queue, &queue_size)) {
                fprintf(state->diagnostics, "minic-ld: gc-root-overflow\n");
                goto done;
            }
        }
    }

    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *section = &state->work.sections[i];
        bool retained = false;
#ifdef SHF_GNU_RETAIN
        retained = (section->flags & SHF_GNU_RETAIN) != 0U;
#endif
        if ((minild_gc_root_section(section) ||
             section->script_keep || retained) &&
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

            /* Explicitly retained LINK_ORDER metadata roots its target.
             * Conversely, a target roots its dependent metadata below.
             * Ordinary unretained metadata is only marked from the target,
             * so it cannot bring a dead target back into the image. */
            if (input->link_order_input != SIZE_MAX) {
                size_t target = state->work.placements[
                    input->link_order_input].work_section;

                if (!minild_gc_mark_section(state, target, live, queue,
                                            &queue_size)) {
                    fprintf(state->diagnostics,
                            "minic-ld: gc-order-root-overflow\n");
                    goto done;
                }
            }

            if (input->group_input != SIZE_MAX) {
                for (member = group_heads[input->group_input];
                     member != SIZE_MAX; member = group_next[member]) {
                    if (!minild_gc_mark_section(
                            state, state->work.placements[member].work_section,
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
                    state, state->work.placements[dependent].work_section,
                    live, queue, &queue_size)) {
                fprintf(state->diagnostics,
                        "minic-ld: gc-link-order-overflow\n");
                goto done;
            }
        }

        /* SHF_ALLOC is the only ordinary relocation reachability edge.
         * Debug references are annotations, not runtime roots. */
        for (relocation_index =
                 (state->work.sections[source].flags & SHF_ALLOC) != 0U
                     ? heads[source] : SIZE_MAX;
             relocation_index != SIZE_MAX;
             relocation_index = next[relocation_index]) {
            MiniLdReloc *reloc = &state->relocations.entries[relocation_index];
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

    /* Only SHF_ALLOC and SHF_LINK_ORDER participate in the relocation
     * reachability traversal.  A debug relocation must never resurrect dead
     * executable code.  Once that graph is settled, derive ordinary
     * non-ALLOC section liveness from its original ET_REL object: GNU BFD
     * retains the object's metadata if any allocated member survived.
     * Explicit KEEP is an independent root for non-ALLOC sections, while
     * SHF_LINK_ORDER continues to obey its target-dependency graph. */
    for (i = 0U; i < state->work.count; ++i) {
        size_t input_index = section_input[i];

        if (!live[i] || input_index == SIZE_MAX ||
            (state->work.sections[i].flags & SHF_ALLOC) == 0U) {
            continue;
        }
        if (state->inputs.sections[input_index].file >=
            state->inputs.file_count) {
            fprintf(state->diagnostics, "minic-ld: gc-invalid-input-file\n");
            goto done;
        }
        input_file_live[state->inputs.sections[input_index].file] = true;
    }
    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *section = &state->work.sections[i];
        size_t input_index = section_input[i];

        if (live[i] || section->script_discard ||
            (section->flags & (SHF_ALLOC | SHF_LINK_ORDER)) != 0U) {
            continue;
        }
        if (input_index != SIZE_MAX &&
            state->inputs.sections[input_index].group_input != SIZE_MAX) {
            /* COMDAT membership beats same-object metadata retention: a
             * dead function's debug group dies even when another function
             * in the same ET_REL was reachable. KEEP and GNU_RETAIN above
             * explicitly root the group before this ordinary-file pass. */
            continue;
        }
        if ((input_index != SIZE_MAX &&
             state->inputs.sections[input_index].file <
                 state->inputs.file_count &&
             input_file_live[state->inputs.sections[input_index].file]) ||
            (input_index == SIZE_MAX && section->input_name == NULL)) {
            live[i] = true;
        }
    }

    for (i = 0U; i < state->work.count; ++i) {
        if (!live[i]) {
            state->work.sections[i].size = 0U;
            state->work.sections[i].gc_discarded = true;
            state->work.sections[i].align = 1U;
            ++discarded;
        }
    }

    /* Preserve resolver section identity even for discarded definitions.
     * The final ELF symbol projection maps absent outputs to SHN_UNDEF;
     * surviving non-ALLOC sources need the original target section to
     * distinguish GNU's whole-field zeroing from a genuine ABS-zero addend. */

    for (i = 0U; i < old_reloc_count; ++i) {
        MiniLdReloc reloc = state->relocations.entries[i];

        if (reloc.section >= state->work.count ||
            state->work.sections[reloc.section].gc_discarded) {
            continue;
        }
        state->relocations.entries[write_reloc++] = reloc;
    }
    state->relocations.count = write_reloc;

    fprintf(state->diagnostics,
            "minic-ld: gc-sections:live=%zu:discarded=%zu:relocs=%zu->%zu\n",
            queue_size,
            discarded,
            old_reloc_count,
            write_reloc);
    ok = true;

done:
    free(input_file_live);
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

/* GC requires per-input-file fragments until dead-object metadata has been
 * removed. A default (non-script) ELF output must then collect surviving
 * ordinary non-ALLOC fragments by output-section name, just like the merged
 * input projection used by a link without --gc-sections. Do this only after
 * liveness is frozen, preserving the original input graph as provenance. */
static bool minild_gc_coalesce_nonalloc(MiniLdState *state) {
    const size_t count = state->work.count;
    size_t *leaders = NULL;
    size_t *offsets = NULL;
    size_t *slots = NULL;
    size_t slot_capacity = 16U;
    size_t merged = 0U;
    size_t i;
    bool ok = false;

    if (count > SIZE_MAX / 2U || count > SIZE_MAX / sizeof(*leaders)) {
        goto done;
    }
    while (slot_capacity <= count * 2U) {
        if (slot_capacity > SIZE_MAX / 2U) {
            goto done;
        }
        slot_capacity *= 2U;
    }
    leaders = malloc((count == 0U ? 1U : count) * sizeof(*leaders));
    offsets = calloc(count == 0U ? 1U : count, sizeof(*offsets));
    slots = calloc(slot_capacity, sizeof(*slots));
    if (leaders == NULL || offsets == NULL || slots == NULL) {
        goto done;
    }
    for (i = 0U; i < count; ++i) {
        MiniLdSection *part = &state->work.sections[i];
        size_t at;
        size_t leader;
        uint64_t hash;

        leaders[i] = i;
        if (part->gc_discarded || part->script_discard ||
            (part->flags & (SHF_ALLOC | SHF_LINK_ORDER)) != 0U ||
            part->input_name == NULL ||
            part->has_link_order_target) {
            continue;
        }
        hash = hash_text(part->input_name) ^
               ((uint64_t)part->type << 32U) ^ part->flags ^
               (part->entsize * UINT64_C(0x9e3779b97f4a7c15));
        at = (size_t)hash & (slot_capacity - 1U);
        while (slots[at] != 0U) {
            MiniLdSection *candidate =
                &state->work.sections[slots[at] - 1U];

            if (candidate->type == part->type &&
                candidate->flags == part->flags &&
                candidate->entsize == part->entsize &&
                strcmp(candidate->input_name, part->input_name) == 0) {
                break;
            }
            at = (at + 1U) & (slot_capacity - 1U);
        }
        if (slots[at] == 0U) {
            slots[at] = i + 1U;
            continue;
        }
        leader = slots[at] - 1U;
        {
            MiniLdSection *primary = &state->work.sections[leader];
            size_t aligned;

            if (!align_up_size(primary->size, part->align, &aligned) ||
                !section_append_zero(primary, aligned - primary->size) ||
                (part->type == SHT_NOBITS
                     ? !section_append_zero(primary, part->size)
                     : !section_append_data(primary, part->data, part->size))) {
                goto done;
            }
            if (part->align > primary->align) {
                primary->align = part->align;
            }
            leaders[i] = leader;
            offsets[i] = aligned;
            part->gc_discarded = true;
            part->size = 0U;
            part->align = 1U;
            ++merged;
        }
    }
    for (i = 0U; i < state->resolver.count; ++i) {
        MiniLdResolvedSymbol *symbol = &state->resolver.entries[i];
        size_t section;

        if (symbol->section < 0 ||
            (section = (size_t)symbol->section) >= count ||
            leaders[section] == section) {
            continue;
        }
        if (symbol->value > UINT64_MAX - offsets[section] ||
            leaders[section] > INT_MAX) {
            goto done;
        }
        symbol->section = (int)leaders[section];
        symbol->value += offsets[section];
    }
    for (i = 0U; i < state->relocations.count; ++i) {
        MiniLdReloc *reloc = &state->relocations.entries[i];
        size_t section = reloc->section;

        if (section >= count || leaders[section] == section) {
            continue;
        }
        if (reloc->offset > UINT64_MAX - offsets[section]) {
            goto done;
        }
        reloc->section = leaders[section];
        reloc->offset += offsets[section];
    }
    for (i = 0U; i < state->work.placement_count; ++i) {
        size_t section = state->work.placements[i].work_section;

        if (section < count) {
            state->work.placements[i].work_section = leaders[section];
        }
    }
    fprintf(state->diagnostics,
            "minic-ld: gc-coalesced-nonalloc:%zu\n", merged);
    ok = true;

done:
    if (!ok) {
        fprintf(state->diagnostics,
                "minic-ld: gc-nonalloc-coalesce-failed\n");
    }
    free(slots);
    free(offsets);
    free(leaders);
    return ok;
}
