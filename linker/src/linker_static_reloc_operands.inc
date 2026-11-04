/* Machine-independent ET_EXEC relocation inputs.  The final address view
 * and work-section liveness are identical for every ELF target.  ISA leaves
 * still decide whether a relocation needs S+A/P, how wide its field is,
 * which instructions to encode, and whether a second pairing pass is needed.
 *
 * Keep S+A as a 64-bit bit pattern: legitimate high-half kernel addresses
 * and negative ELF addends may wrap, while P must name a concrete byte in
 * the already-placed output and therefore cannot silently overflow. */
typedef struct MiniLdStaticRelocOperands {
    uint64_t target_bits;
    uint64_t place;
    const char *symbol_name;
} MiniLdStaticRelocOperands;

/* ET_EXEC relocations in non-ALLOC debug/metadata sections may refer to
 * local labels in other non-ALLOC sections.  Such a section has sh_addr=0,
 * so the relocated value is its final section-relative offset, not a
 * runtime virtual address.  Keep ordinary allocated relocations on the
 * strict address resolver: a text/data reference to a discarded/non-ALLOC
 * symbol must never silently become an address in unmapped memory.
 *
 * The default (unscripted) layout keeps non-ALLOC work-section VAs at zero.
 * Script-owned non-ALLOC output offsets require an output-section projection
 * and are intentionally not admitted by this fallback. */
static bool minild_static_reloc_resolve_symbol(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdReloc *reloc, uint64_t *value_out) {
    const MiniLdResolvedSymbol *symbol;

    if (reloc->section >= state->work.count ||
        reloc->symbol >= state->resolver.count || layout->work == NULL) {
        return minild_layout_resolve_symbol(state, layout, reloc->symbol,
                                            value_out);
    }
    symbol = &state->resolver.entries[reloc->symbol];

    if (!layout->scripted &&
        (state->work.sections[reloc->section].flags & SHF_ALLOC) == 0U &&
        ELF64_ST_BIND(symbol->info) == STB_LOCAL && symbol->section >= 0 &&
        (size_t)symbol->section < state->work.count &&
        (state->work.sections[symbol->section].flags & SHF_ALLOC) == 0U &&
        !state->work.sections[symbol->section].gc_discarded &&
        !layout->work[symbol->section].discarded &&
        layout->work[symbol->section].vaddr == 0U) {
        *value_out = symbol->value;
        return true;
    }
    return minild_layout_resolve_symbol(state, layout, reloc->symbol,
                                        value_out);
}

static bool minild_static_reloc_source(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdReloc *reloc, const char *invalid_section_diagnostic,
    MiniLdSection **section_out, bool *live_out) {
    MiniLdSection *section;

    if (reloc->section >= state->work.count || layout->work == NULL) {
        fprintf(state->diagnostics, "minic-ld: %s\n",
                invalid_section_diagnostic);
        return false;
    }
    section = &state->work.sections[reloc->section];
    *section_out = section;
    *live_out = !layout->scripted ||
                ((section->flags & SHF_ALLOC) != 0U &&
                 !layout->work[reloc->section].discarded);
    return true;
}

static bool minild_static_reloc_operands(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdReloc *reloc, const char *place_overflow_diagnostic,
    MiniLdStaticRelocOperands *out) {
    uint64_t symbol_value = 0U;
    uint64_t section_vaddr = layout->work[reloc->section].vaddr;

    memset(out, 0, sizeof(*out));
    if (reloc->symbol != SIZE_MAX) {
        if (!minild_static_reloc_resolve_symbol(state, layout, reloc,
                                                &symbol_value)) {
            return false;
        }
        out->symbol_name = state->resolver.entries[reloc->symbol].name;
    }
    if (reloc->offset > UINT64_MAX - section_vaddr) {
        fprintf(state->diagnostics, "minic-ld: %s\n",
                place_overflow_diagnostic);
        return false;
    }
    out->target_bits = symbol_value + (uint64_t)reloc->addend;
    out->place = section_vaddr + reloc->offset;
    return true;
}
