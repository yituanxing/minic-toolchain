/* The final post-layout symbol value view is shared by ET_EXEC and ET_DYN.
 * Neither the static GOT nor dynamic relocation writers should own it. */
static bool minild_layout_resolve_symbol(MiniLdState *state,
                                         const MiniLdLayout *layout,
                                         size_t symbol_index,
                                         uint64_t *value_out) {
    MiniLdResolvedSymbol *symbol;

    if (symbol_index >= state->resolver.count) {
        fprintf(state->diagnostics, "minic-ld: relocation-symbol-out-of-range\n");
        return false;
    }
    symbol = &state->resolver.entries[symbol_index];
    if (symbol->section == MINILD_SECTION_UNDEF) {
        if (ELF64_ST_BIND(symbol->info) == STB_WEAK) {
            *value_out = 0U;
            return true;
        }
        fprintf(state->diagnostics,
                "minic-ld: undefined-reference:%s\n",
                symbol->name);
        return false;
    }
    if (symbol->section == MINILD_SECTION_ABS) {
        *value_out = symbol->value;
        return true;
    }
    if (symbol->section < 0 ||
        (size_t)symbol->section >= state->work.count ||
        (state->work.sections[symbol->section].flags & SHF_ALLOC) == 0U ||
        (layout->work != NULL &&
         layout->work[symbol->section].discarded)) {
        fprintf(state->diagnostics,
                "minic-ld: unsupported-static-symbol:%s\n",
                symbol->name);
        return false;
    }
    *value_out = layout->work[symbol->section].vaddr + symbol->value;
    return true;
}

static bool static_entry_address(MiniLdState *state,
                                 const MiniLdLayout *layout,
                                 const char *entry_symbol,
                                 uint64_t *entry_out) {
    size_t index = symbol_resolver_lookup(&state->resolver, entry_symbol);

    if (index == SIZE_MAX) {
        fprintf(state->diagnostics,
                "minic-ld: undefined-entry:%s\n",
                entry_symbol);
        return false;
    }
    return minild_layout_resolve_symbol(state, layout, index, entry_out);
}
