/* Static final-link pre-layout reservation.  This consumes the canonical
 * relocation workspace after all input/GC decisions, but does not apply
 * instruction fixups or decide section addresses. */

static bool static_build_got(MiniLdState *state, MiniLdStaticGot *got) {
    size_t i;

    memset(got, 0, sizeof(*got));
    got->section = SIZE_MAX;
    got->symbol_count = state->resolver.count;
    got->slot_offsets =
        malloc((got->symbol_count == 0U ? 1U : got->symbol_count) *
               sizeof(*got->slot_offsets));
    if (got->slot_offsets == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:static-got\n");
        return false;
    }
    for (i = 0U; i < got->symbol_count; ++i) {
        got->slot_offsets[i] = SIZE_MAX;
    }

    for (i = 0U; i < state->relocations.count; ++i) {
        MiniLdReloc *reloc = &state->relocations.entries[i];
        MiniLdSection *section;
        size_t aligned;

        if (minild_target_reloc_class(reloc->type) !=
            MINILD_TARGET_RELOC_GOT) {
            continue;
        }
        if (reloc->symbol == SIZE_MAX ||
            reloc->symbol >= got->symbol_count ||
            reloc->addend != 0) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-R_RISCV_GOT_HI20\n");
            static_got_destroy(got);
            return false;
        }
        if (got->slot_offsets[reloc->symbol] != SIZE_MAX) {
            continue;
        }
        if (!got->present) {
            if (!find_or_add_section(state,
                                     ".got",
                                     SHT_PROGBITS,
                                     SHF_ALLOC | SHF_WRITE,
                                     8U,
                                     0U,
                                     &got->section)) {
                static_got_destroy(got);
                return false;
            }
            /* As with COMMON, a generated GOT can reuse an emptied source
             * merger. It is live synthetic output, never a GC tombstone. */
            state->work.sections[got->section].gc_discarded = false;
            got->present = true;
        }

        section = &state->work.sections[got->section];
        if (!align_up_size(section->size, 8U, &aligned) ||
            !section_append_zero(section, aligned - section->size) ||
            !section_append_zero(section, 8U)) {
            fprintf(state->diagnostics,
                    "minic-ld: static-got-allocation-overflow\n");
            static_got_destroy(got);
            return false;
        }
        got->slot_offsets[reloc->symbol] = aligned;
    }
    return true;
}
