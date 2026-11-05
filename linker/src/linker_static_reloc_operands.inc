/* Machine-independent ET_EXEC relocation inputs.  The final address view
 * and work-section liveness are identical for every ELF target.  ISA leaves
 * still decide whether a relocation needs S+A/P, how wide its field is,
 * which instructions to encode, and whether a second pairing pass is needed.
 *
 * Keep S+A as a 64-bit bit pattern: legitimate high-half kernel addresses
 * and negative ELF addends may wrap, while P must name a concrete byte in
 * the already-placed output and therefore cannot silently overflow. */
typedef enum MiniLdStaticRelocDisposition {
    MINILD_STATIC_RELOC_APPLY,
    /* GNU BFD zeroes the complete scalar relocation *field* when a kept
     * non-ALLOC source refers to a discarded input definition.  This is
     * not S=0: any ELF addend and PC-relative P must also be ignored. */
    MINILD_STATIC_RELOC_ZERO_DISCARDED_TARGET
} MiniLdStaticRelocDisposition;

typedef struct MiniLdStaticRelocOperands {
    uint64_t target_bits;
    uint64_t place;
    const char *symbol_name;
    MiniLdStaticRelocDisposition disposition;
} MiniLdStaticRelocOperands;

/* Final non-ALLOC symbol values are output-section-relative, irrespective
 * of their ELF binding or linker-script placement.  Allocated relocations
 * retain strict runtime VMA resolution: a text/data relocation pointing to
 * discarded/non-ALLOC storage must never become an unmapped pointer. */
static bool minild_static_reloc_resolve_symbol(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdOutputPlan *output_plan, const MiniLdReloc *reloc,
    uint64_t *value_out) {
    const MiniLdResolvedSymbol *symbol;

    if (reloc->section >= state->work.count ||
        reloc->symbol >= state->resolver.count || layout->work == NULL) {
        return minild_layout_resolve_symbol(state, layout, reloc->symbol,
                                            value_out);
    }
    symbol = &state->resolver.entries[reloc->symbol];

    if ((state->work.sections[reloc->section].flags & SHF_ALLOC) == 0U &&
        symbol->section >= 0 &&
        (size_t)symbol->section < state->work.count &&
        (state->work.sections[symbol->section].flags & SHF_ALLOC) == 0U) {
        if (!minild_output_plan_nonalloc_value(
                state, layout, output_plan, (size_t)symbol->section,
                symbol->value, value_out)) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-static-nonalloc-target:%s\n",
                    symbol->name);
            return false;
        }
        return true;
    }
    return minild_layout_resolve_symbol(state, layout, reloc->symbol,
                                        value_out);
}

static bool minild_static_reloc_source(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdOutputPlan *output_plan,
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
    *live_out = output_plan != NULL
                    ? minild_output_shndx(output_plan, reloc->section) != 0U
                    : (!layout->scripted ||
                       ((section->flags & SHF_ALLOC) != 0U &&
                        !layout->work[reloc->section].discarded));
    return true;
}

static bool minild_static_reloc_operands(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdOutputPlan *output_plan,
    const MiniLdReloc *reloc, const char *place_overflow_diagnostic,
    MiniLdStaticRelocOperands *out) {
    uint64_t symbol_value = 0U;
    uint64_t section_vaddr = layout->work[reloc->section].vaddr;

    memset(out, 0, sizeof(*out));
    if (reloc->symbol != SIZE_MAX) {
        const MiniLdResolvedSymbol *symbol;

        if (reloc->symbol >= state->resolver.count) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-static-reloc-symbol\n");
            return false;
        }
        symbol = &state->resolver.entries[reloc->symbol];
        out->symbol_name = symbol->name;
        /* Resolve the final section *identity* first, before VMA or
         * target-specific GOT/PLT transformations. Only a surviving
         * non-ALLOC source receives BFD's discarded-target treatment.
         * A genuine SHN_ABS zero remains a regular S+A relocation. */
        if (output_plan != NULL &&
            (state->work.sections[reloc->section].flags & SHF_ALLOC) == 0U &&
            symbol->section >= 0 &&
            (size_t)symbol->section < state->work.count &&
            minild_output_shndx(output_plan, (size_t)symbol->section) == 0U) {
            out->disposition = MINILD_STATIC_RELOC_ZERO_DISCARDED_TARGET;
            return true;
        }
        if (!minild_static_reloc_resolve_symbol(state, layout, output_plan, reloc,
                                                &symbol_value)) {
            return false;
        }
    }
    if ((state->work.sections[reloc->section].flags & SHF_ALLOC) == 0U) {
        if (!minild_output_plan_nonalloc_value(
                state, layout, output_plan, reloc->section, reloc->offset,
                &out->place)) {
            fprintf(state->diagnostics, "minic-ld: %s\n",
                    place_overflow_diagnostic);
            return false;
        }
    } else if (reloc->offset > UINT64_MAX - section_vaddr) {
        fprintf(state->diagnostics, "minic-ld: %s\n",
                place_overflow_diagnostic);
        return false;
    } else {
        out->place = section_vaddr + reloc->offset;
    }
    out->target_bits = symbol_value + (uint64_t)reloc->addend;
    return true;
}
