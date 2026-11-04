/* RISC-V ET_EXEC relocation reservation and GOT materialization.
 * Only this target leaf knows the GOT_HI20 policy, slot width, or the
 * machine's generated .got bytes. Layout treats it as an ordinary section. */
typedef struct MiniLdRiscvStaticGot {
    size_t section;
    size_t *slot_offsets;
    size_t symbol_count;
    bool present;
} MiniLdRiscvStaticGot;

static void minild_riscv_destroy_static_relocations(
    MiniLdStaticTargetPlan *plan) {
    MiniLdRiscvStaticGot *got = plan->target_data;

    if (got == NULL) {
        return;
    }
    free(got->slot_offsets);
    free(got);
    plan->target_data = NULL;
}

static bool minild_riscv_reserve_static_relocations(
    MiniLdState *state, MiniLdStaticTargetPlan *plan) {
    MiniLdRiscvStaticGot *got;
    size_t i;

    if (plan->target_data != NULL) {
        fprintf(state->diagnostics, "minic-ld: static-plan-already-reserved\n");
        return false;
    }
    got = calloc(1U, sizeof(*got));
    if (got == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:static-got\n");
        return false;
    }
    plan->target_data = got;
    got->section = SIZE_MAX;
    got->symbol_count = state->resolver.count;
    got->slot_offsets = malloc((got->symbol_count == 0U ? 1U : got->symbol_count) *
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
            fprintf(state->diagnostics, "minic-ld: invalid-R_RISCV_GOT_HI20\n");
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
            /* Reused source mergers can be GC tombstones. */
            state->work.sections[got->section].gc_discarded = false;
            got->present = true;
        }

        section = &state->work.sections[got->section];
        if (!align_up_size(section->size, 8U, &aligned) ||
            !section_append_zero(section, aligned - section->size) ||
            !section_append_zero(section, 8U)) {
            fprintf(state->diagnostics,
                    "minic-ld: static-got-allocation-overflow\n");
            return false;
        }
        got->slot_offsets[reloc->symbol] = aligned;
    }
    return true;
}

static bool minild_riscv_fill_static_relocations(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdStaticTargetPlan *plan) {
    const MiniLdRiscvStaticGot *got = plan->target_data;
    MiniLdSection *section;
    size_t i;

    if (got == NULL) {
        fprintf(state->diagnostics, "minic-ld: missing-riscv-static-plan\n");
        return false;
    }
    if (!got->present) {
        return true;
    }
    if (got->section >= state->work.count) {
        fprintf(state->diagnostics, "minic-ld: invalid-static-got-section\n");
        return false;
    }
    section = &state->work.sections[got->section];

    for (i = 0U; i < got->symbol_count; ++i) {
        uint64_t value;

        if (got->slot_offsets[i] == SIZE_MAX) {
            continue;
        }
        if (!minild_layout_resolve_symbol(state, layout, i, &value) ||
            !range_ok(got->slot_offsets[i], 8U, section->size)) {
            return false;
        }
        store_u64le(section->data + got->slot_offsets[i], value);
    }
    return true;
}
