/* x86-64 ET_EXEC GOT reservation and final materialization.  R_X86_64_GOTPCREL
 * addresses a GOT slot (G+A-P), unlike the direct symbol-address relaxations
 * permitted for R_X86_64_{REX_,}GOTPCRELX.  Keep the slots in an ordinary
 * writable work section so Common owns their placement and ELF serialization. */
typedef struct MiniLdX86StaticGot {
    size_t section;
    size_t *slot_offsets;
    size_t symbol_count;
    bool present;
} MiniLdX86StaticGot;

static void minild_x86_destroy_static_relocations(
    MiniLdStaticTargetPlan *plan) {
    MiniLdX86StaticGot *got = plan->target_data;

    if (got == NULL) {
        return;
    }
    free(got->slot_offsets);
    free(got);
    plan->target_data = NULL;
}

static bool minild_x86_reserve_static_relocations(
    MiniLdState *state, MiniLdStaticTargetPlan *plan) {
    MiniLdX86StaticGot *got;
    size_t i;

    if (plan->target_data != NULL) {
        fprintf(state->diagnostics,
                "minic-ld: x86-static-plan-already-reserved\n");
        return false;
    }
    got = calloc(1U, sizeof(*got));
    if (got == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:x86-static-got\n");
        return false;
    }
    plan->target_data = got;
    got->section = SIZE_MAX;
    got->symbol_count = state->resolver.count;
    if (got->symbol_count > SIZE_MAX / sizeof(*got->slot_offsets)) {
        fprintf(state->diagnostics,
                "minic-ld: x86-static-got-symbol-count-overflow\n");
        return false;
    }
    got->slot_offsets = malloc((got->symbol_count == 0U ? 1U :
                                got->symbol_count) * sizeof(*got->slot_offsets));
    if (got->slot_offsets == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:x86-static-got\n");
        return false;
    }
    for (i = 0U; i < got->symbol_count; ++i) {
        got->slot_offsets[i] = SIZE_MAX;
    }

    for (i = 0U; i < state->relocations.count; ++i) {
        const MiniLdReloc *reloc = &state->relocations.entries[i];
        MiniLdSection *section;
        size_t aligned;

        if (reloc->type != R_X86_64_GOTPCREL) {
            continue;
        }
        if (reloc->symbol == SIZE_MAX ||
            reloc->symbol >= got->symbol_count) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-R_X86_64_GOTPCREL-symbol\n");
            return false;
        }
        if (got->slot_offsets[reloc->symbol] != SIZE_MAX) {
            continue;
        }
        if (!got->present) {
            if (!find_or_add_section(state, ".got", SHT_PROGBITS,
                                     SHF_ALLOC | SHF_WRITE, 8U, 0U,
                                     &got->section)) {
                return false;
            }
            state->work.sections[got->section].gc_discarded = false;
            got->present = true;
        }
        section = &state->work.sections[got->section];
        if (!align_up_size(section->size, 8U, &aligned) ||
            !section_append_zero(section, aligned - section->size) ||
            !section_append_zero(section, 8U)) {
            fprintf(state->diagnostics,
                    "minic-ld: x86-static-got-allocation-overflow\n");
            return false;
        }
        got->slot_offsets[reloc->symbol] = aligned;
    }
    return true;
}
static bool minild_x86_fill_static_relocations(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdStaticTargetPlan *plan) {
    const MiniLdX86StaticGot *got = plan->target_data;
    MiniLdSection *section;
    size_t i;

    if (got == NULL) {
        fprintf(state->diagnostics, "minic-ld: missing-x86-static-plan\n");
        return false;
    }
    if (!got->present) {
        return true;
    }
    if (layout == NULL || layout->work == NULL ||
        got->section >= state->work.count) {
        fprintf(state->diagnostics, "minic-ld: invalid-x86-static-got-section\n");
        return false;
    }
    section = &state->work.sections[got->section];

    for (i = 0U; i < got->symbol_count; ++i) {
        uint64_t value;

        if (got->slot_offsets[i] == SIZE_MAX) {
            continue;
        }
        /* An IFUNC symbol names a resolver, not the callable implementation.
         * Supplying its resolver VA as a normal static GOT value is wrong;
         * IFUNC requires IPLT/IRELATIVE support before admitting that form. */
        if (ELF64_ST_TYPE(state->resolver.entries[i].info) == STT_GNU_IFUNC) {
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-x86-static-ifunc-got\n");
            return false;
        }
        if (!minild_layout_resolve_symbol(state, layout, i, &value) ||
            !range_ok(got->slot_offsets[i], 8U, section->size)) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-x86-static-got-slot\n");
            return false;
        }
        store_u64le(section->data + got->slot_offsets[i], value);
    }
    return true;
}
