/* ET_REL output owns a source/work projection, not the final ELF64
 * OutputPlan used by ET_EXEC and ET_DYN.  The MiniELF relocatable writer
 * serializes this projection; input graph and canonical resolver remain
 * unchanged.  Target identity is supplied by the same admission policy. */

static bool minild_write_relocatable_output(MiniLdState *state,
                                           const char *path) {
    MiniElfWriteSection *sections = NULL;
    MiniElfWriteSymbol *symbols = NULL;
    MiniElfWriteRela *relocations = NULL;
    MiniElfRelocatableSpec spec;
    MiniElfWriteError write_error = MINIELF_WRITE_OK;
    unsigned char *image = NULL;
    size_t image_size = 0U;
    size_t i;
    FILE *file = NULL;
    bool ok = false;

    sections = calloc(state->work.count == 0U ? 1U : state->work.count,
                      sizeof(*sections));
    symbols = calloc(state->resolver.count == 0U ? 1U : state->resolver.count,
                     sizeof(*symbols));
    relocations = calloc(state->relocations.count == 0U ? 1U : state->relocations.count,
                         sizeof(*relocations));
    if (sections == NULL || symbols == NULL || relocations == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:output\n");
        goto done;
    }

    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *input = &state->work.sections[i];
        MiniElfWriteSection *output = &sections[i];

        output->name = input->name;
        output->type = input->type;
        output->flags = input->flags;
        output->alignment = input->align == 0U ? 1U : input->align;
        output->entry_size = input->entsize;
        output->data = input->data;
        output->size = input->size;
        output->has_group = input->has_group;
        output->group_flags = input->group_flags;
        output->group_signature_symbol = input->group_signature_symbol;
        output->has_link_order_target = input->has_link_order_target;
        output->linked_target_section = input->link_order_target;
    }

    for (i = 0U; i < state->resolver.count; ++i) {
        const MiniLdResolvedSymbol *input = &state->resolver.entries[i];
        MiniElfWriteSymbol *output = &symbols[i];

        output->name = input->name;
        output->info = input->info;
        output->other = input->other;
        output->value = input->value;
        output->size = input->size;
        output->group_signature_section = input->group_signature_section;
        if (input->section == MINILD_SECTION_UNDEF) {
            output->section_index = SHN_UNDEF;
        } else if (input->section == MINILD_SECTION_ABS) {
            output->section_index = SHN_ABS;
        } else if (input->section == MINILD_SECTION_COMMON) {
            output->section_index = SHN_COMMON;
        } else if (input->section < 0 ||
                   (size_t)input->section >= state->work.count ||
                   (size_t)input->section + 1U > UINT16_MAX) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-output-symbol-section:%s\n",
                    input->name);
            goto done;
        } else {
            output->section_index =
                (uint16_t)((size_t)input->section + 1U);
        }
    }

    for (i = 0U; i < state->relocations.count; ++i) {
        const MiniLdReloc *input = &state->relocations.entries[i];
        MiniElfWriteRela *output = &relocations[i];

        if (input->section >= state->work.count ||
            (input->symbol != SIZE_MAX &&
             input->symbol >= state->resolver.count)) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-output-relocation\n");
            goto done;
        }
        output->target_section = input->section;
        output->offset = input->offset;
        output->symbol_index = input->symbol;
        output->type = input->type;
        output->addend = input->addend;
    }

    memset(&spec, 0, sizeof(spec));
    minild_target_set_relocatable_identity(state->target, &spec);
    spec.flags = state->elf_flags;
    spec.sections = sections;
    spec.section_count = state->work.count;
    spec.symbols = symbols;
    spec.symbol_count = state->resolver.count;
    spec.relocations = relocations;
    spec.relocation_count = state->relocations.count;
    spec.emit_section_symbols = false;

    if (!minielf_build_relocatable(&spec,
                                   &image,
                                   &image_size,
                                   &write_error)) {
        fprintf(state->diagnostics,
                "minic-ld: cannot-build-output:%s\n",
                minielf_write_error_string(write_error));
        goto done;
    }

    file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: cannot-create:%s:%s\n",
                path,
                strerror(errno));
        goto done;
    }
    if (fwrite(image, 1U, image_size, file) != image_size ||
        fflush(file) != 0) {
        fprintf(state->diagnostics, "minic-ld: write-error:%s\n", path);
        goto done;
    }
    ok = true;

done:
    if (file != NULL && fclose(file) != 0) {
        ok = false;
    }
    if (!ok) {
        (void)remove(path);
    }
    free(image);
    free(relocations);
    free(symbols);
    free(sections);
    return ok;
}
