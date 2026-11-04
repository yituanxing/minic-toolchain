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
    size_t elf_header_size = state->target->elf_class == ELFCLASS32
                                 ? sizeof(Elf32_Ehdr) : sizeof(Elf64_Ehdr);
    size_t program_header_size = state->target->elf_class == ELFCLASS32
                                     ? sizeof(Elf32_Phdr) : sizeof(Elf64_Phdr);
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
        count > (SIZE_MAX - elf_header_size) / program_header_size) {
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
            MiniElfProgramHeader *ph = &plan->entries[i];
            ph->type = PT_LOAD;
            ph->flags = load->flags;
            ph->offset = load->file_offset;
            ph->virtual_address = load->vaddr;
            ph->physical_address = load->laddr;
            ph->file_size = load->file_size;
            ph->memory_size = load->mem_size;
            ph->alignment = 1U;
        }
    } else {
        size_t index = 0U;
        if (layout->have_rx) {
            MiniElfProgramHeader *ph = &plan->entries[index++];
            ph->type = PT_LOAD;
            ph->flags = PF_R | PF_X;
            ph->offset = layout->rx_file_offset;
            ph->virtual_address = ph->physical_address = layout->rx_vaddr;
            ph->file_size = ph->memory_size = layout->rx_file_size;
            ph->alignment = state->target->page_size;
        }
        if (layout->have_rw) {
            MiniElfProgramHeader *ph = &plan->entries[index++];
            ph->type = PT_LOAD;
            ph->flags = PF_R | PF_W;
            ph->offset = layout->rw_file_offset;
            ph->virtual_address = ph->physical_address = layout->rw_vaddr;
            ph->file_size = layout->rw_file_size;
            ph->memory_size = layout->rw_mem_size;
            ph->alignment = state->target->page_size;
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
                                    MiniLdOutputPlan *output_plan,
                                    const char *path,
                                    uint64_t entry) {
    MiniLdFinalImage final = {0};
    size_t elf_header_size = state->target->elf_class == ELFCLASS32
                                 ? sizeof(Elf32_Ehdr) : sizeof(Elf64_Ehdr);
    size_t program_header_size = state->target->elf_class == ELFCLASS32
                                     ? sizeof(Elf32_Phdr) : sizeof(Elf64_Phdr);
    size_t load_image_size;
    size_t i;
    bool ok = false;

    if (!minild_static_program_plan_build(state, layout, output_plan,
                                          &final.program)) {
        goto done;
    }
    load_image_size = elf_header_size +
                      final.program.count * program_header_size;

    if (layout->scripted) {
        for (i = 0U; i < output_plan->count; ++i) {
            const MiniLdOutputSection *section = &output_plan->sections[i];
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

    if (output_plan->count > UINT16_MAX - 4U) {
        fprintf(state->diagnostics,
                "minic-ld: too-many-static-output-sections\n");
        goto done;
    }

    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *section = &state->work.sections[i];
        if (output_plan->work_shndx[i] != 0U &&
            (section->flags & SHF_ALLOC) != 0U &&
            section->type != SHT_NOBITS && section->size != 0U &&
            (output_plan->work_file_offsets[i] == SIZE_MAX ||
             !range_ok(output_plan->work_file_offsets[i], section->size,
                       load_image_size))) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-static-section-layout:%s\n",
                    section->name);
            goto done;
        }
    }
    if (!minild_final_image_prepare(state, layout, output_plan,
                                          &final, load_image_size, NULL,
                                          MINILD_FINAL_SYMTAB_FIRST,
                                          ET_EXEC)) {
        goto done;
    }
    ok = minild_final_write_image(state, layout, output_plan,
                                  &final.program, &final.tables,
                                  &final.symbols, ET_EXEC, entry, path);

done:
    /* Retain the prior fail-closed contract even if preparation failed
     * before the common ELF serializer was entered. */
    if (!ok) {
        (void)remove(path);
    }
    minild_final_image_destroy(&final);
    return ok;
}
