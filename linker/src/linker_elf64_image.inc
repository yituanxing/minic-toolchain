/* Final ELF64 file serialization shared by ET_EXEC and ET_DYN.
 * Output sections, program headers, symbol tables and format-specific shdr
 * links have already been finalized by their respective owners.  No layout,
 * relocation, symbol-resolution or output-section policy belongs here. */
static bool minild_elf64_write_image(
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
    Elf64_Ehdr header;
    const size_t total_size = tables->total_size;
    const char *oom_context = elf_type == ET_DYN
                                  ? "shared-writer" : "static-output";
    FILE *file = NULL;
    bool ok = false;

    if ((elf_type != ET_EXEC && elf_type != ET_DYN) ||
        program_plan->count > UINT16_MAX ||
        tables->count > UINT16_MAX) {
        fprintf(state->diagnostics,
                "minic-ld: invalid-final-elf64-image\n");
        return false;
    }
    image = calloc(total_size == 0U ? 1U : total_size, 1U);
    if (image == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:%s\n",
                oom_context);
        goto done;
    }

    memset(&header, 0, sizeof(header));
    memcpy(header.e_ident, ELFMAG, SELFMAG);
    header.e_ident[EI_CLASS] = ELFCLASS64;
    header.e_ident[EI_DATA] = ELFDATA2LSB;
    header.e_ident[EI_VERSION] = EV_CURRENT;
    header.e_ident[EI_OSABI] = ELFOSABI_NONE;
    header.e_type = elf_type;
    header.e_machine = EM_RISCV;
    header.e_version = EV_CURRENT;
    header.e_entry = entry;
    header.e_phoff = sizeof(Elf64_Ehdr);
    header.e_shoff = tables->header_offset;
    header.e_flags = state->elf_flags;
    header.e_ehsize = sizeof(Elf64_Ehdr);
    header.e_phentsize = sizeof(Elf64_Phdr);
    header.e_phnum = (Elf64_Half)program_plan->count;
    header.e_shentsize = sizeof(Elf64_Shdr);
    header.e_shnum = (Elf64_Half)tables->count;
    header.e_shstrndx = (Elf64_Half)tables->shstrtab_index;
    memcpy(image, &header, sizeof(header));

    if (program_plan->count != 0U) {
        memcpy(image + sizeof(Elf64_Ehdr), program_plan->entries,
               program_plan->count * sizeof(Elf64_Phdr));
    }
    if (!minild_output_plan_emit_sections(state, layout, output_plan,
                                          image, total_size)) {
        goto done;
    }
    minild_final_section_tables_emit(tables, symbols, image);

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
