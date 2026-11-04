/* AArch64 ET_EXEC GOT ownership.  GNU-produced ADR_GOT_PAGE/LD64_GOT_LO12
 * pairs reference the address of a pointer-sized GOT entry, not S+A.  The
 * target reserves one entry per referenced canonical symbol before layout;
 * Common assigns .got an address and emits the ordinary section. */
/* Older glibc elf.h versions omit this AAELF64 psABI relocation (315). */
#ifndef R_AARCH64_GOTPCREL32
#define R_AARCH64_GOTPCREL32 315
#endif
typedef struct MiniLdAarch64StaticGot {
    size_t section;
    size_t symbol_count;
    size_t *slot_offsets;
    bool present;
} MiniLdAarch64StaticGot;

static bool minild_aarch64_static_uses_got(uint32_t type) {
    switch (type) {
    case R_AARCH64_GOTREL64:
    case R_AARCH64_GOTREL32:
    case R_AARCH64_GOT_LD_PREL19:
    case R_AARCH64_LD64_GOTOFF_LO15:
    case R_AARCH64_ADR_GOT_PAGE:
    case R_AARCH64_LD64_GOT_LO12_NC:
    case R_AARCH64_LD64_GOTPAGE_LO15:
    case R_AARCH64_GOTPCREL32:
        return true;
    default:
        return false;
    }
}

static void minild_aarch64_destroy_static_relocations(
    MiniLdStaticTargetPlan *plan) {
    MiniLdAarch64StaticGot *got = plan->target_data;

    if (got != NULL) {
        free(got->slot_offsets);
        free(got);
        plan->target_data = NULL;
    }
}

static bool minild_aarch64_reserve_static_relocations(
    MiniLdState *state, MiniLdStaticTargetPlan *plan) {
    MiniLdAarch64StaticGot *got;
    size_t i;

    if (plan->target_data != NULL) {
        fprintf(state->diagnostics,
                "minic-ld: aarch64-static-plan-already-reserved\n");
        return false;
    }
    got = calloc(1U, sizeof(*got));
    if (got == NULL) {
        return false;
    }
    plan->target_data = got;
    got->section = SIZE_MAX;
    got->symbol_count = state->resolver.count;
    if (got->symbol_count > SIZE_MAX / sizeof(*got->slot_offsets)) {
        fprintf(state->diagnostics,
                "minic-ld: aarch64-static-got-symbol-count-overflow\n");
        return false;
    }
    got->slot_offsets = malloc((got->symbol_count == 0U ? 1U :
                                got->symbol_count) * sizeof(*got->slot_offsets));
    if (got->slot_offsets == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:aarch64-static-got\n");
        return false;
    }
    for (i = 0U; i < got->symbol_count; ++i) {
        got->slot_offsets[i] = SIZE_MAX;
    }

    for (i = 0U; i < state->relocations.count; ++i) {
        const MiniLdReloc *reloc = &state->relocations.entries[i];
        MiniLdSection *section;
        size_t aligned;

        if (!minild_aarch64_static_uses_got(reloc->type)) {
            continue;
        }
        if (reloc->symbol == SIZE_MAX ||
            reloc->symbol >= got->symbol_count ||
            (reloc->type != R_AARCH64_GOTREL64 &&
             reloc->type != R_AARCH64_GOTREL32 &&
             reloc->addend != 0)) {
            /* AAELF64 GDAT(S) relocations require a zero addend. */
            fprintf(state->diagnostics,
                    "minic-ld: invalid-aarch64-static-got-relocation:%u\n",
                    (unsigned)reloc->type);
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
                    "minic-ld: aarch64-static-got-allocation-overflow\n");
            return false;
        }
        got->slot_offsets[reloc->symbol] = aligned;
    }
    return true;
}

static bool minild_aarch64_fill_static_relocations(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdStaticTargetPlan *plan) {
    const MiniLdAarch64StaticGot *got = plan->target_data;
    MiniLdSection *section;
    size_t i;

    if (got == NULL) {
        fprintf(state->diagnostics, "minic-ld: missing-aarch64-static-plan\n");
        return false;
    }
    if (!got->present) {
        return true;
    }
    if (layout == NULL || layout->work == NULL ||
        got->section >= state->work.count) {
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
            fprintf(state->diagnostics,
                    "minic-ld: invalid-aarch64-static-got-slot\n");
            return false;
        }
        store_u64le(section->data + got->slot_offsets[i], value);
    }
    return true;
}

static bool minild_aarch64_static_resolve_target(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdStaticTargetPlan *plan, const MiniLdReloc *reloc,
    uint64_t *target) {
    const MiniLdAarch64StaticGot *got = plan->target_data;
    uint64_t got_base;
    uint64_t slot;

    if (!minild_aarch64_static_uses_got(reloc->type)) {
        return true;
    }
    if (got == NULL || !got->present || got->section >= state->work.count ||
        reloc->symbol >= got->symbol_count ||
        got->slot_offsets[reloc->symbol] == SIZE_MAX ||
        layout->work[got->section].vaddr >
            UINT64_MAX - got->slot_offsets[reloc->symbol]) {
        fprintf(state->diagnostics,
                "minic-ld: missing-aarch64-static-got-entry:%u\n",
                (unsigned)reloc->type);
        return false;
    }
    got_base = layout->work[got->section].vaddr;
    slot = got_base + got->slot_offsets[reloc->symbol];
    switch (reloc->type) {
    case R_AARCH64_GOTREL64:
    case R_AARCH64_GOTREL32:
        *target -= got_base; /* S+A-GOT */
        break;
    case R_AARCH64_LD64_GOTOFF_LO15:
        *target = slot - got_base;
        break;
    case R_AARCH64_LD64_GOTPAGE_LO15:
        *target = slot - (got_base & ~UINT64_C(0xfff));
        break;
    case R_AARCH64_GOTPCREL32:
        *target = slot;
        break;
    default:
        *target = slot; /* GOT_LD_PREL19 / ADR_GOT_PAGE / LD64_LO12 */
        break;
    }
    return true;
}
