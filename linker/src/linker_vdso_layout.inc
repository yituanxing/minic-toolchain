/* The fixed vDSO ProgramPlan owns four PHDRs, but linker-script output
 * sections still own their GNU :phdr associations.  Validate those
 * associations against the existing segment policy instead of silently
 * ignoring a script's segment assignment.  The active list inherits from
 * the preceding allocated output section unless explicitly replaced.
 *
 * This intentionally does not implement arbitrary PHDR segment layout:
 * unsupported combinations fail closed before ELF serialization. */
static bool shared_validate_vdso_script_phdr_membership(
    const MiniLdState *state,
    const MiniLdScript *script,
    const MiniLdLayout *layout) {
    enum {
        PHDR_TEXT = 1U << 0U,
        PHDR_DYNAMIC = 1U << 1U,
        PHDR_NOTE = 1U << 2U,
        PHDR_EH_FRAME_HDR = 1U << 3U
    };
    const size_t command_count = script->command_count;
    unsigned char *command_masks = NULL;
    bool *command_has_alloc = NULL;
    unsigned char inherited_mask = 0U;
    size_t i;
    bool ok = false;

    command_masks = calloc(command_count == 0U ? 1U : command_count,
                           sizeof(*command_masks));
    command_has_alloc = calloc(command_count == 0U ? 1U : command_count,
                               sizeof(*command_has_alloc));
    if (command_masks == NULL || command_has_alloc == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:shared-script-PHDR-membership\n");
        goto done;
    }

    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *section = &state->work.sections[i];
        size_t owner;

        if (section->gc_discarded || layout->work[i].discarded ||
            (section->flags & SHF_ALLOC) == 0U || section->size == 0U) {
            continue;
        }
        owner = layout->work[i].owner_command;
        if (owner >= command_count ||
            script->commands[owner].kind != MINILD_SCRIPT_OUTPUT_SECTION ||
            script->commands[owner].value.section.discard) {
            const char *name = section->input_name != NULL
                                   ? section->input_name : section->name;

            if (owner < command_count &&
                script->commands[owner].kind == MINILD_SCRIPT_OUTPUT_SECTION) {
                name = script->commands[owner].value.section.name;
            }
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-shared-script-PHDR-membership:%s\n",
                    name);
            goto done;
        }
        command_has_alloc[owner] = true;
    }

    for (i = 0U; i < command_count; ++i) {
        const MiniLdScriptCommand *command = &script->commands[i];
        const MiniLdScriptOutputSection *output;
        size_t j;

        if (command->kind != MINILD_SCRIPT_OUTPUT_SECTION ||
            !command_has_alloc[i]) {
            continue;
        }
        output = &command->value.section;
        if (output->phdr_name_count != 0U) {
            inherited_mask = 0U;
            for (j = 0U; j < output->phdr_name_count; ++j) {
                const char *name = output->phdr_names[j];

                if (strcmp(name, "text") == 0) {
                    inherited_mask |= PHDR_TEXT;
                } else if (strcmp(name, "dynamic") == 0) {
                    inherited_mask |= PHDR_DYNAMIC;
                } else if (strcmp(name, "note") == 0) {
                    inherited_mask |= PHDR_NOTE;
                } else if (strcmp(name, "eh_frame_hdr") == 0) {
                    inherited_mask |= PHDR_EH_FRAME_HDR;
                } else {
                    fprintf(state->diagnostics,
                            "minic-ld: unsupported-shared-script-PHDR-membership:%s\n",
                            output->name);
                    goto done;
                }
            }
        }
        command_masks[i] = inherited_mask;
    }

    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *section = &state->work.sections[i];
        unsigned char required_mask = PHDR_TEXT;
        size_t owner;

        if (section->gc_discarded || layout->work[i].discarded ||
            (section->flags & SHF_ALLOC) == 0U || section->size == 0U) {
            continue;
        }
        owner = layout->work[i].owner_command;
        if (section->type == SHT_NOTE) {
            required_mask |= PHDR_NOTE;
        }
        if (section->type == SHT_DYNAMIC &&
            strcmp(section->name, ".dynamic") == 0) {
            required_mask |= PHDR_DYNAMIC;
        }
        if (strcmp(section->name, ".eh_frame_hdr") == 0) {
            required_mask |= PHDR_EH_FRAME_HDR;
        }
        if (command_masks[owner] != required_mask) {
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-shared-script-PHDR-membership:%s\n",
                    script->commands[owner].value.section.name);
            goto done;
        }
    }
    ok = true;

done:
    free(command_has_alloc);
    free(command_masks);
    return ok;
}

/* Shared -T uses the proven static output-section and PROVIDE evaluator.
 * In Linux's four-header single-PT_LOAD layout, the PT_LOAD starts at
 * file offset 0 and VMA 0, and every alloc section must lie after the
 * ELF and PHDR tables with file offset identical to its VMA.
 */
static bool shared_build_vdso_script_layout(MiniLdState *state,
                                            const MiniLdScript *script,
                                            MiniLdLayout *layout) {
    size_t headers_size;
    size_t i;
    size_t end;

    if (script->phdr_count != 4U ||
        strcmp(script->phdrs[0].name, "text") != 0 ||
        strcmp(script->phdrs[1].name, "dynamic") != 0 ||
        strcmp(script->phdrs[2].name, "note") != 0 ||
        strcmp(script->phdrs[3].name, "eh_frame_hdr") != 0 ||
        script->phdrs[1].flags != 4U ||
        !script->phdrs[1].have_flags ||
        script->phdrs[2].flags != 4U ||
        !script->phdrs[2].have_flags ||
        strcmp(script->phdrs[0].type_name, "PT_LOAD") != 0 ||
        !script->phdrs[0].filehdr || !script->phdrs[0].phdrs ||
        !script->phdrs[0].have_flags || script->phdrs[0].flags != 5U ||
        strcmp(script->phdrs[1].type_name, "PT_DYNAMIC") != 0 ||
        strcmp(script->phdrs[2].type_name, "PT_NOTE") != 0 ||
        strcmp(script->phdrs[3].type_name, "PT_GNU_EH_FRAME") != 0) {
        fprintf(state->diagnostics,
                "minic-ld: unsupported-shared-script-PHDRS-layout\n");
        return false;
    }
    headers_size = sizeof(Elf64_Ehdr) +
                   script->phdr_count * sizeof(Elf64_Phdr);
    if (!static_build_script_layout(state, script, layout, headers_size)) {
        return false;
    }
    /* Every shared-script declaration was registered before metadata
     * sizing.  A remaining planned UND is never a valid runtime import:
     * its final definition was promised by the script and any dynamic
     * symbol/relocation reservation depended on that promise. */
    for (i = 0U; i < state->resolver.count; ++i) {
        const MiniLdResolvedSymbol *symbol = &state->resolver.entries[i];

        if (symbol->script_planned_defined &&
            (symbol->script_placeholder ||
             symbol->section == MINILD_SECTION_UNDEF)) {
            fprintf(state->diagnostics,
                    "minic-ld: unresolved-shared-script-definition:%s\n",
                    symbol->name);
            return false;
        }
    }
    if (!shared_validate_vdso_script_phdr_membership(state, script, layout)) {
        return false;
    }
    end = headers_size;
    for (i = 0U; i < state->work.count; ++i) {
        MiniLdSection *section = &state->work.sections[i];
        uint64_t address;
        if (layout->work[i].discarded ||
            (section->flags & SHF_ALLOC) == 0U ||
            section->size == 0U) {
            continue;
        }
        address = layout->work[i].vaddr;
        if (address < headers_size ||
            address > SIZE_MAX ||
            section->size > SIZE_MAX - (size_t)address) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-vdso-script-file-placement:%s\n",
                    section->name);
            return false;
        }
        layout->work[i].file_offset = (size_t)address;
        if (section->type != SHT_NOBITS &&
            end < (size_t)address + section->size) {
            end = (size_t)address + section->size;
        }
    }
    /* vDSO PT_LOAD includes FILEHDR/PHDRS: its file and virtual bases are
     * both zero. The script allocator's temporary packed file bases are
     * replaced above, so the output owner bases must follow suit. */
    for (i = 0U; i < layout->script_command_count; ++i) {
        if (layout->script_outputs[i].file_base != SIZE_MAX) {
            if (layout->script_outputs[i].start > SIZE_MAX) {
                fprintf(state->diagnostics,
                        "minic-ld: vdso-output-base-overflow\n");
                return false;
            }
            layout->script_outputs[i].file_base =
                (size_t)layout->script_outputs[i].start;
        }
    }
    layout->have_rx = true;
    layout->have_rw = false;
    layout->rx_file_offset = 0U;
    layout->rx_vaddr = 0U;
    layout->rx_file_size = end;
    return true;
}
