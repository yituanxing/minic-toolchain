/* ET_DYN segment policy: TLS, PHDR layout and synthetic section links.
 * This owner consumes the prepared shared metadata and OutputPlan. */
typedef struct MiniLdSharedTlsSegment {
    bool present;
    size_t file_offset;
    uint64_t vaddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} MiniLdSharedTlsSegment;

static bool shared_tls_segment(const MiniLdLayout *layout,
                               const MiniLdOutputPlan *output_plan,
                               MiniLdSharedTlsSegment *tls) {
    const MiniLdTlsProjection *projection = &output_plan->tls;
    uint64_t base = projection->block_base;

    memset(tls, 0, sizeof(*tls));
    if (!projection->has_nonempty_section) {
        return true;
    }
    if (!projection->valid_extents ||
        !layout->have_rw || base < layout->rw_vaddr ||
        base - layout->rw_vaddr > SIZE_MAX ||
        layout->rw_file_offset >
            SIZE_MAX - (size_t)(base - layout->rw_vaddr)) {
        return false;
    }

    tls->present = true;
    tls->vaddr = base;
    tls->file_offset =
        layout->rw_file_offset + (size_t)(base - layout->rw_vaddr);
    tls->filesz = projection->has_file_contents
                      ? projection->file_end - base : 0U;
    tls->memsz = projection->memory_end - base;
    tls->align = projection->alignment;
    return true;
}

/* The ET_DYN segment policy owns the program headers before serialization.
 * PT_LOAD includes ELF headers; PIE, TLS and vDSO introduce distinct PHDRs. */
static bool minild_shared_program_plan_build(
    const MiniLdState *state,
    const MiniLdLayout *layout,
    const MiniLdOutputPlan *output_plan,
    const MiniLdSharedImage *shared,
    MiniLdProgramPlan *plan) {
    MiniLdSharedTlsSegment tls;
    size_t phnum;
    MiniLdElfAbiSizes abi = minild_elf_abi_sizes(state->target->elf_class);
    size_t program_index = 0U;
    size_t i;
    MiniElfProgramHeader *programs;

    memset(plan, 0, sizeof(*plan));
    if (!shared_tls_segment(layout, output_plan, &tls)) {
        fprintf(state->diagnostics,
                "minic-ld: invalid-shared-tls-segment-layout\n");
        return false;
    }
    phnum = (shared->have_eh_frame_hdr ||
             shared->layout_script != NULL ? 1U : 0U) +
            (shared->layout_script != NULL ? 1U : 0U) +
            (shared->pie ? 1U : 0U) +
            (shared->have_interp ? 1U : 0U) +
            (layout->have_rx ? 1U : 0U) +
            (layout->have_rw ? 1U : 0U) +
            (tls.present ? 1U : 0U) + 1U;
    if (abi.phdr == 0U || phnum > UINT16_MAX ||
        phnum > (SIZE_MAX - abi.ehdr) / abi.phdr) {
        fprintf(state->diagnostics, "minic-ld: too-many-shared-segments\n");
        return false;
    }
    plan->entries = calloc(phnum, sizeof(*plan->entries));
    if (plan->entries == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:shared-segments\n");
        return false;
    }
    plan->count = phnum;
    programs = plan->entries;

    if (shared->pie) {
        MiniElfProgramHeader *ph = &programs[program_index++];
        uint64_t header_vaddr;

        if (!layout->have_rx ||
            (uint64_t)layout->rx_file_offset > layout->rx_vaddr) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-pie-phdr-layout\n");
            goto failed;
        }
        header_vaddr =
            layout->rx_vaddr - (uint64_t)layout->rx_file_offset;
        ph->type = PT_PHDR;
        ph->flags = PF_R;
        ph->offset = abi.ehdr;
        ph->virtual_address = header_vaddr + abi.ehdr;
        ph->physical_address = ph->virtual_address;
        ph->file_size = phnum * abi.phdr;
        ph->memory_size = ph->file_size;
        ph->alignment = abi.word_alignment;
    }
    if (shared->have_interp) {
        MiniElfProgramHeader *ph = &programs[program_index++];
        const MiniLdSection *interp;

        if (shared->interp_section >= state->work.count ||
            layout->work[shared->interp_section].file_offset == SIZE_MAX) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-pie-interpreter-layout\n");
            goto failed;
        }
        interp = &state->work.sections[shared->interp_section];
        ph->type = PT_INTERP;
        ph->flags = PF_R;
        ph->offset = layout->work[shared->interp_section].file_offset;
        ph->virtual_address = layout->work[shared->interp_section].vaddr;
        ph->physical_address = ph->virtual_address;
        ph->file_size = interp->size;
        ph->memory_size = interp->size;
        ph->alignment = 1U;
    }
    if (layout->have_rx) {
        MiniElfProgramHeader *ph = &programs[program_index++];
        uint64_t header_vaddr;

        if ((uint64_t)layout->rx_file_offset > layout->rx_vaddr ||
            layout->rx_file_offset >
                SIZE_MAX - layout->rx_file_size) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-shared-header-load-layout\n");
            goto failed;
        }
        header_vaddr =
            layout->rx_vaddr - (uint64_t)layout->rx_file_offset;
        ph->type = PT_LOAD;
        ph->flags = PF_R | PF_X;
        ph->offset = 0U;
        ph->virtual_address = header_vaddr;
        ph->physical_address = header_vaddr;
        ph->file_size =
            (uint64_t)(layout->rx_file_offset + layout->rx_file_size);
        ph->memory_size = ph->file_size;
        ph->alignment = state->target->page_size;
    }
    if (layout->have_rw) {
        MiniElfProgramHeader *ph = &programs[program_index++];
        ph->type = PT_LOAD;
        ph->flags = PF_R | PF_W;
        ph->offset = layout->rw_file_offset;
        ph->virtual_address = layout->rw_vaddr;
        ph->physical_address = layout->rw_vaddr;
        ph->file_size = layout->rw_file_size;
        ph->memory_size = layout->rw_mem_size;
        ph->alignment = state->target->page_size;
    }
    if (tls.present) {
        MiniElfProgramHeader *ph = &programs[program_index++];
        ph->type = PT_TLS;
        ph->flags = PF_R;
        ph->offset = tls.file_offset;
        ph->virtual_address = tls.vaddr;
        ph->physical_address = tls.vaddr;
        ph->file_size = tls.filesz;
        ph->memory_size = tls.memsz;
        ph->alignment = tls.align;
    }
    {
        MiniElfProgramHeader *ph = &programs[program_index++];
        MiniLdSection *dynamic = &state->work.sections[shared->dynamic_section];
        size_t offset = layout->work[shared->dynamic_section].file_offset;

        if (offset == SIZE_MAX) {
            fprintf(state->diagnostics,
                    "minic-ld: missing-shared-dynamic-file-offset\n");
            goto failed;
        }
        ph->type = PT_DYNAMIC;
        ph->flags = shared->layout_script != NULL ? PF_R : PF_R | PF_W;
        ph->offset = offset;
        ph->virtual_address = layout->work[shared->dynamic_section].vaddr;
        ph->physical_address = ph->virtual_address;
        ph->file_size = dynamic->size;
        ph->memory_size = dynamic->size;
        ph->alignment = 8U;
    }

    if (shared->layout_script != NULL) {
        /* Emit only the actual GNU vDSO mapped PT_NOTE section(s).
         * A missing or out-of-load note is a hard failure. */
        size_t note_offset = SIZE_MAX;
        uint64_t note_addr = UINT64_MAX;
        uint64_t note_end = 0U;
        for (i = 0U; i < state->work.count; ++i) {
            const MiniLdSection *section = &state->work.sections[i];
            size_t offset = layout->work[i].file_offset;
            uint64_t address = layout->work[i].vaddr;
            if (section->type != SHT_NOTE ||
                (section->flags & SHF_ALLOC) == 0U ||
                layout->work[i].discarded || section->size == 0U) {
                continue;
            }
            if (offset == SIZE_MAX ||
                address > UINT64_MAX - section->size ||
                offset > SIZE_MAX - section->size) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-vdso-script-note-placement\n");
                goto failed;
            }
            if (offset < note_offset) {
                note_offset = offset;
            }
            if (address < note_addr) {
                note_addr = address;
            }
            if (address + section->size > note_end) {
                note_end = address + section->size;
            }
        }
        if (note_offset == SIZE_MAX || note_addr == UINT64_MAX ||
            note_end <= note_addr ||
            note_end - note_addr > layout->rx_file_size) {
            fprintf(state->diagnostics,
                    "minic-ld: missing-vdso-script-PT_NOTE\n");
            goto failed;
        }
        {
            MiniElfProgramHeader *ph = &programs[program_index++];
            ph->type = PT_NOTE;
            ph->flags = PF_R;
            ph->offset = note_offset;
            ph->virtual_address = note_addr;
            ph->physical_address = note_addr;
            ph->file_size = note_end - note_addr;
            ph->memory_size = ph->file_size;
            ph->alignment = 4U;
        }
    }
    if (shared->have_eh_frame_hdr ||
        shared->layout_script != NULL) {
        MiniElfProgramHeader *ph = &programs[program_index++];
        memset(ph, 0, sizeof(*ph));
        ph->type = PT_GNU_EH_FRAME;
        if (shared->have_eh_frame_hdr) {
            size_t h = shared->eh_frame_hdr_section;
            if (h >= state->work.count ||
                layout->work[h].file_offset == SIZE_MAX) {
                fprintf(state->diagnostics,
                        "minic-ld: missing-eh-frame-hdr-file-offset\n");
                goto failed;
            }
            ph->flags = PF_R;
            ph->offset = layout->work[h].file_offset;
            ph->virtual_address = layout->work[h].vaddr;
            ph->physical_address = ph->virtual_address;
            ph->file_size = state->work.sections[h].size;
            ph->memory_size = ph->file_size;
            ph->alignment = 4U;
        } else {
            /* GNU's explicit-script header when all vDSO CFI FDEs
             * disappear: 0-byte PT_GNU_EH_FRAME, no invented section. */
            ph->alignment = 8U;
        }
    }
    if (program_index != phnum) {
        fprintf(state->diagnostics, "minic-ld: shared-phdr-count-mismatch\n");
        goto failed;
    }
    return true;

failed:
    minild_program_plan_destroy(plan);
    return false;
}

/* Only ET_DYN synthetic sections add dynamic sh_link/sh_info policy.
 * All generic section identity and LINK_ORDER links are already finalized. */
static void minild_shared_output_section_links(
    const MiniLdOutputPlan *output_plan,
    const MiniLdSharedImage *shared,
    MiniElfSection *section_headers) {
    size_t i;
    for (i = 0U; i < output_plan->count; ++i) {
        MiniElfSection *sh = &section_headers[i + 1U];

        if (i + 1U == minild_output_shndx(output_plan,
                                           shared->dynsym_section)) {
            sh->link = (uint32_t)
                minild_output_shndx(output_plan, shared->dynstr_section);
            sh->info = 1U;
        } else if (shared->version_script != NULL &&
                   i + 1U == minild_output_shndx(output_plan,
                                                  shared->versym_section)) {
            sh->link = (uint32_t)
                minild_output_shndx(output_plan, shared->dynsym_section);
        } else if (shared->version_script != NULL &&
                   i + 1U == minild_output_shndx(output_plan,
                                                  shared->verdef_section)) {
            sh->link = (uint32_t)
                minild_output_shndx(output_plan, shared->dynstr_section);
            sh->info = 2U;
        } else if (i + 1U == minild_output_shndx(output_plan,
                                                  shared->hash_section) ||
                   (shared->have_gnu_hash &&
                    i + 1U == minild_output_shndx(output_plan,
                                                   shared->gnu_hash_section)) ||
                   i + 1U == minild_output_shndx(output_plan,
                                                  shared->rela_section) ||
                   i + 1U == minild_output_shndx(output_plan,
                                                  shared->rela_plt_section)) {
            sh->link = (uint32_t)
                minild_output_shndx(output_plan, shared->dynsym_section);
        } else if (i + 1U == minild_output_shndx(output_plan,
                                                  shared->dynamic_section)) {
            sh->link = (uint32_t)
                minild_output_shndx(output_plan, shared->dynstr_section);
        }
    }
}
