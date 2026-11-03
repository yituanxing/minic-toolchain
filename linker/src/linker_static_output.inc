/* Coalesce adjacent scripted PT_LOADs without moving sections or relocations.
 * A vmlinux linker script can place >10,000 individual input sections. */
typedef struct MiniLdStaticLoadSegment {
    uint64_t file_offset, vaddr, laddr, file_size, mem_size;
    uint32_t flags;
} MiniLdStaticLoadSegment;

static int static_load_segment_compare(const void *lhs, const void *rhs) {
    const MiniLdStaticLoadSegment *a = lhs, *b = rhs;

    if (a->laddr != b->laddr) {
        return a->laddr < b->laddr ? -1 : 1;
    }
    if (a->vaddr != b->vaddr) {
        return a->vaddr < b->vaddr ? -1 : 1;
    }
    if (a->file_offset != b->file_offset) {
        return a->file_offset < b->file_offset ? -1 : 1;
    }
    return 0;
}

/* The static segment policy is deliberately distinct from ET_DYN: default
 * executables leave the ELF header outside PT_LOAD, while scripted images
 * preserve LMA and merge only three-way-contiguous sections. */
static bool minild_static_program_plan_build(
    const MiniLdState *state,
    const MiniLdLayout *layout,
    const MiniLdOutputPlan *output_plan,
    MiniLdProgramPlan *plan) {
    MiniLdStaticLoadSegment *loads = NULL;
    size_t count = 0U;
    size_t i;

    memset(plan, 0, sizeof(*plan));
    if (layout->scripted) {
        size_t load_count = 0U;
        loads = calloc(output_plan->count == 0U ? 1U : output_plan->count,
                       sizeof(*loads));
        if (loads == NULL) {
            goto oom;
        }
        for (i = 0U; i < output_plan->count; ++i) {
            const MiniLdOutputSection *section = &output_plan->sections[i];
            MiniLdStaticLoadSegment *load;

            if ((section->flags & SHF_ALLOC) == 0U || section->size == 0U) {
                continue;
            }
            if (section->file_offset == SIZE_MAX ||
                section->size > UINT64_MAX - section->vaddr ||
                section->size > UINT64_MAX - section->laddr) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-script-load-range:%s\n",
                        section->name);
                goto failed;
            }
            load = &loads[load_count++];
            load->file_offset = section->file_offset;
            load->vaddr = section->vaddr;
            load->laddr = section->laddr;
            load->file_size = section->type == SHT_NOBITS ? 0U :
                              section->size;
            load->mem_size = section->size;
            load->flags = PF_R;
            if ((section->flags & SHF_WRITE) != 0U) {
                load->flags |= PF_W;
            }
            if ((section->flags & SHF_EXECINSTR) != 0U) {
                load->flags |= PF_X;
            }
        }
        qsort(loads, load_count, sizeof(*loads), static_load_segment_compare);
        for (i = 0U; i < load_count; ++i) {
            const MiniLdStaticLoadSegment *next = &loads[i];

            if (count != 0U) {
                MiniLdStaticLoadSegment *prev = &loads[count - 1U];
                if (prev->flags == next->flags &&
                    prev->file_size == prev->mem_size &&
                    prev->laddr <= UINT64_MAX - prev->mem_size &&
                    prev->vaddr <= UINT64_MAX - prev->mem_size &&
                    prev->file_offset <= UINT64_MAX - prev->file_size &&
                    prev->laddr + prev->mem_size == next->laddr &&
                    prev->vaddr + prev->mem_size == next->vaddr &&
                    prev->file_offset + prev->file_size == next->file_offset) {
                    prev->mem_size += next->mem_size;
                    prev->file_size += next->file_size;
                    continue;
                }
            }
            loads[count++] = *next;
        }
    } else {
        count = (layout->have_rx ? 1U : 0U) +
                (layout->have_rw ? 1U : 0U);
    }
    if (count > UINT16_MAX ||
        count > (SIZE_MAX - sizeof(Elf64_Ehdr)) / sizeof(Elf64_Phdr)) {
        fprintf(state->diagnostics, "minic-ld: too-many-static-segments\n");
        goto failed;
    }
    plan->entries = calloc(count == 0U ? 1U : count,
                           sizeof(*plan->entries));
    if (plan->entries == NULL) {
        goto oom;
    }
    plan->count = count;
    if (layout->scripted) {
        for (i = 0U; i < count; ++i) {
            const MiniLdStaticLoadSegment *load = &loads[i];
            Elf64_Phdr *ph = &plan->entries[i];
            ph->p_type = PT_LOAD;
            ph->p_flags = load->flags;
            ph->p_offset = load->file_offset;
            ph->p_vaddr = load->vaddr;
            ph->p_paddr = load->laddr;
            ph->p_filesz = load->file_size;
            ph->p_memsz = load->mem_size;
            ph->p_align = 1U;
        }
    } else {
        size_t index = 0U;
        if (layout->have_rx) {
            Elf64_Phdr *ph = &plan->entries[index++];
            ph->p_type = PT_LOAD;
            ph->p_flags = PF_R | PF_X;
            ph->p_offset = layout->rx_file_offset;
            ph->p_vaddr = ph->p_paddr = layout->rx_vaddr;
            ph->p_filesz = ph->p_memsz = layout->rx_file_size;
            ph->p_align = 4096U;
        }
        if (layout->have_rw) {
            Elf64_Phdr *ph = &plan->entries[index++];
            ph->p_type = PT_LOAD;
            ph->p_flags = PF_R | PF_W;
            ph->p_offset = layout->rw_file_offset;
            ph->p_vaddr = ph->p_paddr = layout->rw_vaddr;
            ph->p_filesz = layout->rw_file_size;
            ph->p_memsz = layout->rw_mem_size;
            ph->p_align = 4096U;
        }
    }
    free(loads);
    return true;

oom:
    fprintf(state->diagnostics, "minic-ld: out-of-memory:static-segments\n");
failed:
    free(loads);
    minild_program_plan_destroy(plan);
    return false;
}

static bool static_write_executable(MiniLdState *state,
                                    const MiniLdLayout *layout,
                                    const char *path,
                                    uint64_t entry) {
    MiniLdOutputPlan output_plan = {0};
    MiniLdProgramPlan program_plan = {0};
    MiniLdFinalSectionTables final_tables = {0};
    size_t load_image_size;
    size_t cursor;
    size_t total_size;
    MiniLdFinalSymbols final_symbols = {0};
    unsigned char *image = NULL;
    Elf64_Ehdr header;
    size_t i;
    FILE *file = NULL;
    bool ok = false;

    if (layout->scripted &&
        !minild_output_plan_build(state, layout,
                                  layout->section_file_offset,
                                  &output_plan)) {
        goto done;
    }
    if (!minild_static_program_plan_build(state, layout, &output_plan,
                                          &program_plan)) {
        goto done;
    }
    load_image_size = sizeof(Elf64_Ehdr) +
                      program_plan.count * sizeof(Elf64_Phdr);

    if (layout->scripted) {
        for (i = 0U; i < output_plan.count; ++i) {
            const MiniLdOutputSection *section = &output_plan.sections[i];
            size_t end;

            if ((section->flags & SHF_ALLOC) == 0U ||
                section->type == SHT_NOBITS || section->size == 0U) {
                continue;
            }
            if (section->file_offset == SIZE_MAX ||
                section->size > SIZE_MAX ||
                !add_size(section->file_offset, (size_t)section->size, &end)) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-script-section-file-offset:%s\n",
                        section->name);
                goto done;
            }
            if (end > load_image_size) {
                load_image_size = end;
            }
        }
    } else {
        if (layout->have_rx &&
            layout->rx_file_offset + layout->rx_file_size > load_image_size) {
            load_image_size = layout->rx_file_offset + layout->rx_file_size;
        }
        if (layout->have_rw &&
            layout->rw_file_offset + layout->rw_file_size > load_image_size) {
            load_image_size = layout->rw_file_offset + layout->rw_file_size;
        }
    }

    if (!layout->scripted &&
        !minild_output_plan_build(state, layout,
                                  layout->section_file_offset,
                                  &output_plan)) {
        goto done;
    }
    if (output_plan.count > UINT16_MAX - 4U) {
        fprintf(state->diagnostics,
                "minic-ld: too-many-static-output-sections\n");
        goto done;
    }

    if (!minild_output_plan_place_nonalloc(state, layout, &output_plan,
                                           load_image_size, &cursor)) {
        goto done;
    }
    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *section = &state->work.sections[i];
        if (output_plan.work_shndx[i] != 0U &&
            (section->flags & SHF_ALLOC) != 0U &&
            section->type != SHT_NOBITS && section->size != 0U &&
            (output_plan.work_file_offsets[i] == SIZE_MAX ||
             !range_ok(output_plan.work_file_offsets[i], section->size,
                       load_image_size))) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-static-section-layout:%s\n",
                    section->name);
            goto done;
        }
    }
    if (!minild_final_symbols_build(state, layout, &output_plan,
                                    &final_symbols)) {
        goto oom;
    }
    if (!minild_final_section_tables_build(&output_plan, &final_symbols,
                                           cursor, MINILD_FINAL_SYMTAB_FIRST,
                                           &final_tables)) {
        goto oom;
    }
    if (!minild_output_plan_section_headers(
            state, &output_plan, final_tables.output_name_offsets,
            final_tables.headers)) {
        goto done;
    }
    total_size = final_tables.total_size;
    if (!minild_program_plan_validate(state, &program_plan, total_size)) {
        goto done;
    }
    image = calloc(total_size == 0U ? 1U : total_size, 1U);
    if (image == NULL) {
        goto oom;
    }

    memset(&header, 0, sizeof(header));
    memcpy(header.e_ident, ELFMAG, SELFMAG);
    header.e_ident[EI_CLASS] = ELFCLASS64;
    header.e_ident[EI_DATA] = ELFDATA2LSB;
    header.e_ident[EI_VERSION] = EV_CURRENT;
    header.e_ident[EI_OSABI] = ELFOSABI_NONE;
    header.e_type = ET_EXEC;
    header.e_machine = EM_RISCV;
    header.e_version = EV_CURRENT;
    header.e_entry = entry;
    header.e_phoff = sizeof(Elf64_Ehdr);
    header.e_shoff = final_tables.header_offset;
    header.e_flags = state->elf_flags;
    header.e_ehsize = sizeof(Elf64_Ehdr);
    header.e_phentsize = sizeof(Elf64_Phdr);
    header.e_phnum = (Elf64_Half)program_plan.count;
    header.e_shentsize = sizeof(Elf64_Shdr);
    header.e_shnum = (Elf64_Half)final_tables.count;
    header.e_shstrndx = (Elf64_Half)final_tables.shstrtab_index;
    memcpy(image, &header, sizeof(header));

    if (program_plan.count != 0U) {
        memcpy(image + sizeof(Elf64_Ehdr), program_plan.entries,
               program_plan.count * sizeof(Elf64_Phdr));
    }

    if (!minild_output_plan_emit_sections(state, layout, &output_plan,
                                          image, total_size)) {
        goto done;
    }

    minild_final_section_tables_emit(&final_tables, &final_symbols, image);

    file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: cannot-create:%s:%s\n",
                path,
                strerror(errno));
        goto done;
    }
    if (fwrite(image, 1U, total_size, file) != total_size ||
        fflush(file) != 0) {
        fprintf(state->diagnostics,
                "minic-ld: write-error:%s\n",
                path);
        goto done;
    }
    ok = true;
    goto done;

oom:
    fprintf(state->diagnostics,
            "minic-ld: out-of-memory:static-output\n");

done:
    if (file != NULL && fclose(file) != 0) {
        ok = false;
    }
    if (ok) {
        if (chmod(path, 0755) != 0) {
            fprintf(state->diagnostics,
                    "minic-ld: chmod-error:%s:%s\n",
                    path,
                    strerror(errno));
            ok = false;
        }
    }
    if (!ok) {
        (void)remove(path);
    }
    free(image);
    minild_final_section_tables_destroy(&final_tables);
    minild_program_plan_destroy(&program_plan);
    minild_output_plan_destroy(&output_plan);
    minild_final_symbols_destroy(&final_symbols);
    return ok;
}
