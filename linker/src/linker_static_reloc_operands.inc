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
        if (!minild_layout_resolve_symbol(state, layout,
                                          reloc->symbol, &symbol_value)) {
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
