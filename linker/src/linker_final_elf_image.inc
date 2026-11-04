/* Final ELF32/64 serialization shared by ET_EXEC and ET_DYN.  Program,
 * section and symbol plans carry semantic descriptors.  Only this boundary
 * writes ABI-sized fields, rejecting narrow ELF32 overflow. */

static bool minild_final_u32_fits(uint64_t value) {
    return value <= UINT32_MAX;
}

static bool minild_final_pack_program_header(unsigned char *out,
                                             unsigned char elf_class,
                                             const MiniElfProgramHeader *p) {
    if (elf_class == ELFCLASS64) {
        store_u32le(out, p->type);
        store_u32le(out + 4U, p->flags);
        store_u64le(out + 8U, p->offset);
        store_u64le(out + 16U, p->virtual_address);
        store_u64le(out + 24U, p->physical_address);
        store_u64le(out + 32U, p->file_size);
        store_u64le(out + 40U, p->memory_size);
        store_u64le(out + 48U, p->alignment);
        return true;
    }
    if (elf_class != ELFCLASS32 ||
        !minild_final_u32_fits(p->offset) ||
        !minild_final_u32_fits(p->virtual_address) ||
        !minild_final_u32_fits(p->physical_address) ||
        !minild_final_u32_fits(p->file_size) ||
        !minild_final_u32_fits(p->memory_size) ||
        !minild_final_u32_fits(p->alignment)) {
        return false;
    }
    store_u32le(out, p->type);
    store_u32le(out + 4U, (uint32_t)p->offset);
    store_u32le(out + 8U, (uint32_t)p->virtual_address);
    store_u32le(out + 12U, (uint32_t)p->physical_address);
    store_u32le(out + 16U, (uint32_t)p->file_size);
    store_u32le(out + 20U, (uint32_t)p->memory_size);
    store_u32le(out + 24U, p->flags);
    store_u32le(out + 28U, (uint32_t)p->alignment);
    return true;
}

static bool minild_final_pack_section_header(unsigned char *out,
                                             unsigned char elf_class,
                                             const MiniElfSection *s) {
    store_u32le(out, s->name);
    store_u32le(out + 4U, s->type);
    if (elf_class == ELFCLASS64) {
        store_u64le(out + 8U, s->flags);
        store_u64le(out + 16U, s->address);
        store_u64le(out + 24U, s->offset);
        store_u64le(out + 32U, s->size);
        store_u32le(out + 40U, s->link);
        store_u32le(out + 44U, s->info);
        store_u64le(out + 48U, s->alignment);
        store_u64le(out + 56U, s->entry_size);
        return true;
    }
    if (elf_class != ELFCLASS32 ||
        !minild_final_u32_fits(s->flags) ||
        !minild_final_u32_fits(s->address) ||
        !minild_final_u32_fits(s->offset) ||
        !minild_final_u32_fits(s->size) ||
        !minild_final_u32_fits(s->alignment) ||
        !minild_final_u32_fits(s->entry_size)) {
        return false;
    }
    store_u32le(out + 8U, (uint32_t)s->flags);
    store_u32le(out + 12U, (uint32_t)s->address);
    store_u32le(out + 16U, (uint32_t)s->offset);
    store_u32le(out + 20U, (uint32_t)s->size);
    store_u32le(out + 24U, s->link);
    store_u32le(out + 28U, s->info);
    store_u32le(out + 32U, (uint32_t)s->alignment);
    store_u32le(out + 36U, (uint32_t)s->entry_size);
    return true;
}

static bool minild_final_pack_symbol(unsigned char *out,
                                     unsigned char elf_class,
                                     const MiniElfSymbol *s) {
    store_u32le(out, s->name);
    if (elf_class == ELFCLASS64) {
        out[4U] = s->info;
        out[5U] = s->other;
        store_u16le(out + 6U, s->section_index);
        store_u64le(out + 8U, s->value);
        store_u64le(out + 16U, s->size);
        return true;
    }
    if (elf_class != ELFCLASS32 ||
        !minild_final_u32_fits(s->value) ||
        !minild_final_u32_fits(s->size)) {
        return false;
    }
    store_u32le(out + 4U, (uint32_t)s->value);
    store_u32le(out + 8U, (uint32_t)s->size);
    out[12U] = s->info;
    out[13U] = s->other;
    store_u16le(out + 14U, s->section_index);
    return true;
}

static bool minild_final_pack_elf_header(unsigned char *out,
                                         const MiniLdState *state,
                                         uint16_t elf_type,
                                         uint64_t entry,
                                         const MiniLdProgramPlan *program,
                                         const MiniLdFinalSectionTables *tables) {
    unsigned char elf_class = state->target->elf_class;
    size_t header_size = elf_class == ELFCLASS32
                             ? sizeof(Elf32_Ehdr) : sizeof(Elf64_Ehdr);
    size_t program_size = elf_class == ELFCLASS32
                              ? sizeof(Elf32_Phdr) : sizeof(Elf64_Phdr);
    size_t section_size = elf_class == ELFCLASS32
                              ? sizeof(Elf32_Shdr) : sizeof(Elf64_Shdr);

    if ((elf_class != ELFCLASS32 && elf_class != ELFCLASS64) ||
        state->target->data_encoding != ELFDATA2LSB ||
        program->count > UINT16_MAX || tables->count > UINT16_MAX ||
        tables->shstrtab_index > UINT16_MAX ||
        (elf_class == ELFCLASS32 &&
         (!minild_final_u32_fits(entry) ||
          !minild_final_u32_fits(tables->header_offset)))) {
        return false;
    }
    memcpy(out, ELFMAG, SELFMAG);
    out[EI_CLASS] = elf_class;
    out[EI_DATA] = state->target->data_encoding;
    out[EI_VERSION] = EV_CURRENT;
    out[EI_OSABI] = ELFOSABI_NONE;
    store_u16le(out + 16U, elf_type);
    store_u16le(out + 18U, state->target->machine);
    store_u32le(out + 20U, EV_CURRENT);
    if (elf_class == ELFCLASS64) {
        store_u64le(out + 24U, entry);
        store_u64le(out + 32U, header_size);
        store_u64le(out + 40U, tables->header_offset);
        store_u32le(out + 48U, state->elf_flags);
        store_u16le(out + 52U, (uint16_t)header_size);
        store_u16le(out + 54U, (uint16_t)program_size);
        store_u16le(out + 56U, (uint16_t)program->count);
        store_u16le(out + 58U, (uint16_t)section_size);
        store_u16le(out + 60U, (uint16_t)tables->count);
        store_u16le(out + 62U, (uint16_t)tables->shstrtab_index);
    } else {
        store_u32le(out + 24U, (uint32_t)entry);
        store_u32le(out + 28U, (uint32_t)header_size);
        store_u32le(out + 32U, (uint32_t)tables->header_offset);
        store_u32le(out + 36U, state->elf_flags);
        store_u16le(out + 40U, (uint16_t)header_size);
        store_u16le(out + 42U, (uint16_t)program_size);
        store_u16le(out + 44U, (uint16_t)program->count);
        store_u16le(out + 46U, (uint16_t)section_size);
        store_u16le(out + 48U, (uint16_t)tables->count);
        store_u16le(out + 50U, (uint16_t)tables->shstrtab_index);
    }
    return true;
}

static bool minild_final_write_image(
    const MiniLdState *state,
    const MiniLdLayout *layout,
    const MiniLdOutputPlan *output_plan,
    const MiniLdProgramPlan *program_plan,
    const MiniLdFinalSectionTables *tables,
    const MiniLdFinalSymbols *symbols,
    uint16_t elf_type,
    uint64_t entry,
    const char *path) {
    unsigned char *image = NULL;
    unsigned char elf_class = state->target->elf_class;
    const size_t header_size = elf_class == ELFCLASS32
                                   ? sizeof(Elf32_Ehdr) : sizeof(Elf64_Ehdr);
    const size_t program_size = elf_class == ELFCLASS32
                                    ? sizeof(Elf32_Phdr) : sizeof(Elf64_Phdr);
    const size_t section_size = elf_class == ELFCLASS32
                                    ? sizeof(Elf32_Shdr) : sizeof(Elf64_Shdr);
    const size_t symbol_size = elf_class == ELFCLASS32
                                   ? sizeof(Elf32_Sym) : sizeof(Elf64_Sym);
    const size_t total_size = tables->total_size;
    const char *oom_context = elf_type == ET_DYN
                                  ? "shared-writer" : "static-output";
    FILE *file = NULL;
    bool ok = false;
    size_t i;

    if ((elf_type != ET_EXEC && elf_type != ET_DYN) ||
        (elf_class != ELFCLASS32 && elf_class != ELFCLASS64) ||
        program_plan->count > UINT16_MAX || tables->count > UINT16_MAX ||
        !range_ok(0U, header_size, total_size) ||
        program_plan->count > (total_size - header_size) / program_size ||
        !range_ok(tables->header_offset,
                  tables->count * section_size, total_size) ||
        symbols->entry_count > SIZE_MAX / symbol_size ||
        !range_ok(tables->symtab_offset,
                  symbols->entry_count * symbol_size, total_size)) {
        fprintf(state->diagnostics,
                "minic-ld: invalid-final-elf-image\n");
        return false;
    }
    image = calloc(total_size == 0U ? 1U : total_size, 1U);
    if (image == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:%s\n",
                oom_context);
        goto done;
    }
    if (!minild_final_pack_elf_header(image, state, elf_type, entry,
                                      program_plan, tables)) {
        fprintf(state->diagnostics, "minic-ld: final-elf-header-overflow\n");
        goto done;
    }
    for (i = 0U; i < program_plan->count; ++i) {
        if (!minild_final_pack_program_header(
                image + header_size + i * program_size, elf_class,
                &program_plan->entries[i])) {
            fprintf(state->diagnostics,
                    "minic-ld: final-elf-program-header-overflow:%zu\n", i);
            goto done;
        }
    }
    if (!minild_output_plan_emit_sections(state, layout, output_plan,
                                          image, total_size)) {
        goto done;
    }
    minild_final_section_tables_emit(tables, symbols, image);
    for (i = 0U; i < symbols->entry_count; ++i) {
        if (!minild_final_pack_symbol(
                image + tables->symtab_offset + i * symbol_size, elf_class,
                &symbols->entries[i])) {
            fprintf(state->diagnostics,
                    "minic-ld: final-elf-symbol-overflow:%zu\n", i);
            goto done;
        }
    }
    for (i = 0U; i < tables->count; ++i) {
        if (!minild_final_pack_section_header(
                image + tables->header_offset + i * section_size, elf_class,
                &tables->headers[i])) {
            fprintf(state->diagnostics,
                    "minic-ld: final-elf-section-header-overflow:%zu\n", i);
            goto done;
        }
    }

    file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: cannot-create:%s:%s\n", path, strerror(errno));
        goto done;
    }
    if (fwrite(image, 1U, total_size, file) != total_size ||
        fflush(file) != 0) {
        fprintf(state->diagnostics, "minic-ld: write-error:%s\n", path);
        goto done;
    }
    ok = true;

done:
    if (file != NULL && fclose(file) != 0) {
        ok = false;
    }
    if (ok && chmod(path, 0755) != 0) {
        fprintf(state->diagnostics,
                "minic-ld: chmod-error:%s:%s\n", path, strerror(errno));
        ok = false;
    }
    if (!ok) {
        (void)remove(path);
    }
    free(image);
    return ok;
}
