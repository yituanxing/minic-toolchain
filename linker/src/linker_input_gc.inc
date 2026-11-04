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

        for (relocation_index = heads[source];
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

    for (i = 0U; i < state->work.count; ++i) {
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
        MiniLdReloc reloc = state->relocations.entries[i];

        if (reloc.section >= state->work.count ||
            !live[reloc.section]) {
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
