/* Program-header storage and validation belong to both ET_EXEC and ET_DYN.
 * This owner holds class-neutral semantic headers, never packed ELF64 ABI
 * structures. The final format writer chooses ELF32/64 representation. */
typedef struct MiniLdProgramPlan {
    MiniElfProgramHeader *entries;
    size_t count;
} MiniLdProgramPlan;

static void minild_program_plan_destroy(MiniLdProgramPlan *plan) {
    free(plan->entries);
    memset(plan, 0, sizeof(*plan));
}

/* GNU_STACK input notes are object metadata, not mapped output sections.
 * Read original admitted ET_REL identities: script/GC discard and mutable
 * WorkSection coalescing must not change the requested process stack policy.
 * Explicit -z noexecstack overrides any executable-stack input note. */
static bool minild_program_plan_stack_flags(const MiniLdState *state,
                                             uint32_t *flags) {
    bool saw_note = false;
    bool executable = false;
    size_t i;

    if (!state->z_noexecstack) {
        for (i = 0U; i < state->inputs.section_count; ++i) {
            const MiniLdInputSection *input = &state->inputs.sections[i];
            if (strcmp(input->name, ".note.GNU-stack") != 0) {
                continue;
            }
            saw_note = true;
            executable = executable ||
                         (input->flags & SHF_EXECINSTR) != 0U;
        }
    }
    if (!state->z_noexecstack && !saw_note) {
        return false;
    }
    *flags = PF_R | PF_W | (executable ? PF_X : 0U);
    return true;
}

/* PT_GNU_STACK is a loader policy record, not an output section.  Keep
 * the class-neutral header in ProgramPlan; final ELF32/64 writer packs it. */
static void minild_program_plan_set_stack(MiniElfProgramHeader *ph,
                                          uint32_t flags) {
    memset(ph, 0, sizeof(*ph));
    ph->type = PT_GNU_STACK;
    ph->flags = flags;
    ph->alignment = 16U;
}

static bool minild_program_plan_validate(const MiniLdState *state,
                                         const MiniLdProgramPlan *plan,
                                         const MiniLdOutputPlan *output_plan,
                                         size_t image_size) {
    MiniLdElfAbiSizes abi = minild_elf_abi_sizes(state->target->elf_class);
    size_t header_end;
    size_t i;

    /* The ELF and PHDR records must own their complete file range.
     * Layout precedes final PHDR synthesis, so a low-alignment scripted
     * image (notably GNU -n) can otherwise place section bytes inside a
     * later-added PT_TLS/PT_NOTE header.  Fail before any output is written
     * even if a future script planner misses a header-count case. */
    if (abi.phdr == 0U || plan->count > (SIZE_MAX - abi.ehdr) / abi.phdr) {
        fprintf(state->diagnostics,
                "minic-ld: final-program-header-overflow\n");
        return false;
    }
    header_end = abi.ehdr + plan->count * abi.phdr;
    for (i = 0U; i < output_plan->count; ++i) {
        const MiniLdOutputSection *section = &output_plan->sections[i];

        if (section->type == SHT_NOBITS || section->size == 0U) {
            continue;
        }
        if (section->file_offset == SIZE_MAX ||
            section->file_offset < header_end) {
            fprintf(state->diagnostics,
                    "minic-ld: final-section-overlaps-elf-headers:%s\n",
                    section->name);
            return false;
        }
    }
    for (i = 0U; i < plan->count; ++i) {
        const MiniElfProgramHeader *ph = &plan->entries[i];
        uint64_t align = ph->alignment;
        if (ph->file_size > ph->memory_size ||
            (ph->file_size != 0U &&
             (ph->offset > image_size ||
              ph->file_size > image_size - (size_t)ph->offset)) ||
            ph->virtual_address > UINT64_MAX - ph->memory_size ||
            (align > 1U &&
             (((align & (align - 1U)) != 0U) ||
              ph->offset % align != ph->virtual_address % align))) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-final-program-range:%zu:type=%u\n",
                    i, ph->type);
            return false;
        }
    }
    return true;
}
