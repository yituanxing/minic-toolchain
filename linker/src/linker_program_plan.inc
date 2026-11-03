/* Program-header storage and validation belong to both ET_EXEC and ET_DYN.
 * Each writer keeps its own segment-selection policy and ELF header; this
 * common plan owns only the final program-header array and range contract. */
typedef struct MiniLdProgramPlan {
    Elf64_Phdr *entries;
    size_t count;
} MiniLdProgramPlan;

static void minild_program_plan_destroy(MiniLdProgramPlan *plan) {
    free(plan->entries);
    memset(plan, 0, sizeof(*plan));
}

static bool minild_program_plan_validate(const MiniLdState *state,
                                         const MiniLdProgramPlan *plan,
                                         size_t image_size) {
    size_t i;
    for (i = 0U; i < plan->count; ++i) {
        const Elf64_Phdr *ph = &plan->entries[i];
        uint64_t align = ph->p_align;
        if (ph->p_filesz > ph->p_memsz ||
            ph->p_offset > image_size ||
            ph->p_filesz > image_size - (size_t)ph->p_offset ||
            ph->p_vaddr > UINT64_MAX - ph->p_memsz ||
            (align > 1U &&
             (((align & (align - 1U)) != 0U) ||
              ph->p_offset % align != ph->p_vaddr % align))) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-final-program-range:%zu:type=%u\n",
                    i, ph->p_type);
            return false;
        }
    }
    return true;
}
