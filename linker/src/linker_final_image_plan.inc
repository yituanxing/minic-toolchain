/* ET_EXEC and ET_DYN differ in segment policy and dynamic metadata, but
 * their final ELF32/64 tables have one lifecycle. Keep the ProgramPlan,
 * final symbol projection and synthetic section-table projection under one
 * owner; the caller supplies its already-decided segment and table policies.
 * This stage does not mutate canonical input or resolver identities. */
typedef struct MiniLdFinalImage {
    MiniLdProgramPlan program;
    MiniLdFinalSymbols symbols;
    MiniLdFinalSectionTables tables;
} MiniLdFinalImage;

static void minild_final_image_destroy(MiniLdFinalImage *final) {
    minild_final_section_tables_destroy(&final->tables);
    minild_program_plan_destroy(&final->program);
    minild_final_symbols_destroy(&final->symbols);
    memset(final, 0, sizeof(*final));
}

/* The program-header producer and the load-image end are mode-specific.
 * Everything after those decisions and before dynamic sh_link policy is a
 * single final-file projection for both output modes.  ET_DYN may still
 * refine dynamic section-header links before calling the serializer. */
static bool minild_final_image_prepare(
    MiniLdState *state,
    const MiniLdLayout *layout,
    MiniLdOutputPlan *output_plan,
    MiniLdFinalImage *final,
    size_t load_image_end,
    const MiniLdScript *version_script,
    MiniLdFinalSymbolTableOrder table_order,
    uint16_t elf_type) {
    size_t end_nonalloc;

    if (elf_type != ET_EXEC && elf_type != ET_DYN) {
        fprintf(state->diagnostics,
                "minic-ld: invalid-final-image\n");
        return false;
    }
    if (!minild_output_plan_place_nonalloc(state, layout, output_plan,
                                           load_image_end, &end_nonalloc)) {
        return false;
    }
    if (!(version_script == NULL
              ? minild_final_symbols_build(state, layout, output_plan,
                                           &final->symbols)
              : minild_final_symbols_build_with_version(
                    state, layout, output_plan, version_script,
                    &final->symbols))) {
        if (elf_type == ET_DYN) {
            fprintf(state->diagnostics,
                    "minic-ld: cannot-build-shared-static-symbols\n");
        }
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:%s\n",
                elf_type == ET_DYN ? "shared-writer" : "static-output");
        return false;
    }
    if (!minild_final_section_tables_build(state, output_plan, &final->symbols,
                                           end_nonalloc, table_order,
                                           &final->tables)) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:%s\n",
                elf_type == ET_DYN ? "shared-writer" : "static-output");
        return false;
    }
    if (!minild_output_plan_section_headers(
            state, output_plan, final->tables.output_name_offsets,
            final->tables.headers)) {
        return false;
    }
    return minild_program_plan_validate(state, &final->program,
                                        final->tables.total_size);
}
