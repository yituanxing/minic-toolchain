static bool minild_allocate_common(MiniLdState *state) {
    size_t bss_index = SIZE_MAX;
    size_t i;

    for (i = 0U; i < state->resolver.count; ++i) {
        MiniLdResolvedSymbol *symbol = &state->resolver.entries[i];
        size_t aligned;
        uint64_t alignment;

        if (symbol->section != MINILD_SECTION_COMMON) {
            continue;
        }
        if (bss_index == SIZE_MAX &&
            !find_or_add_section(state,
                                 ".bss",
                                 SHT_NOBITS,
                                 SHF_ALLOC | SHF_WRITE,
                                 16U,
                                 0U,
                                 &bss_index)) {
            return false;
        }
        /* Source-exact GC/script projections do not create an empty merged
         * .bss holder. COMMON owns this indexable work section independently
         * of any original input fragments, and it must survive GC. */
        state->work.sections[bss_index].gc_discarded = false;
        alignment = symbol->value == 0U ? 1U : symbol->value;
        if (!align_up_size(state->work.sections[bss_index].size,
                           alignment,
                           &aligned) ||
            !section_append_zero(&state->work.sections[bss_index],
                                 aligned - state->work.sections[bss_index].size) ||
            !section_append_zero(&state->work.sections[bss_index],
                                 (size_t)symbol->size)) {
            fprintf(state->diagnostics, "minic-ld: common-allocation-overflow\n");
            return false;
        }
        symbol->section = (int)bss_index;
        symbol->value = aligned;
    }
    return true;
}
