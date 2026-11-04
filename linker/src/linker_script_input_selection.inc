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
                size_t source_section = state->work.placements[i].work_section;
                MiniLdSection *section;

                if (source_section == SIZE_MAX ||
                    source->file >= state->inputs.file_count ||
                    !minild_script_pattern_matches_input(
                        &item->value.pattern, source->name,
                        state->inputs.files[source->file].path)) {
                    continue;
                }
                if (source_section >= state->work.count) {
                    fprintf(state->diagnostics,
                            "minic-ld: invalid-script-source-section\n");
                    return false;
                }
                section = &state->work.sections[source_section];
                if (section->input_name == NULL) {
                    continue;
                }
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
            /* COMMON and merged non-allocating metadata have no exact
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
    size_t previous_reloc_count = state->relocations.count;
    size_t remaining_relocs = 0U;
    size_t discarded_count = 0U;
    size_t i;
    bool changed;
    bool ok = false;

    if (!relocations_require_editable(&state->relocations,
                                       state->diagnostics,
                                       "shared-script-prune") ||
        !minild_select_script_inputs(state, script)) {
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

    for (i = 0U; i < state->relocations.count; ++i) {
        const MiniLdReloc *reloc = &state->relocations.entries[i];
        const MiniLdResolvedSymbol *symbol;
        if (reloc->section >= state->work.count) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-shared-script-reloc-source\n");
            goto done;
        }
        if (discarded[reloc->section] ||
            minild_target_ignored_script_reloc(reloc->type) ||
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
        if (discarded[i]) {
            section->gc_discarded = true;
            section->size = 0U;
            section->align = 1U;
            ++discarded_count;
        }
    }
    for (i = 0U; i < previous_reloc_count; ++i) {
        MiniLdReloc reloc = state->relocations.entries[i];
        if (!discarded[reloc.section]) {
            state->relocations.entries[remaining_relocs++] = reloc;
        }
    }
    state->relocations.count = remaining_relocs;
    fprintf(state->diagnostics,
            "minic-ld: shared-script-discard:sections=%zu:relocs=%zu->%zu\n",
            discarded_count, previous_reloc_count, remaining_relocs);
    ok = true;
done:
    free(discarded);
    return ok;
}
