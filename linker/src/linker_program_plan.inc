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

static bool minild_program_plan_validate(const MiniLdState *state,
                                         const MiniLdProgramPlan *plan,
                                         size_t image_size) {
    size_t i;
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
