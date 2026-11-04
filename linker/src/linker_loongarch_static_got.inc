/* LoongArch64 ET_EXEC GOT and TLS variant-I materialization.
 * A GOT relocation targets its slot's address, not the referenced symbol;
 * an IE-TLS slot instead holds its symbol's TP-relative offset.  Reserve all
 * slots before Common places sections, then fill them from the final layout. */

/* These newer psABI relocations are absent from the elf.h shipped by some
 * supported build hosts.  Keep the assigned ABI numbers here so a host libc
 * update is not required just to link LoongArch objects. */
#ifndef R_LARCH_CALL36
#define R_LARCH_CALL36 110
#endif
#ifndef R_LARCH_TLS_LE_HI20_R
#define R_LARCH_TLS_LE_HI20_R 121
#endif
#ifndef R_LARCH_TLS_LE_ADD_R
#define R_LARCH_TLS_LE_ADD_R 122
#endif
#ifndef R_LARCH_TLS_LE_LO12_R
#define R_LARCH_TLS_LE_LO12_R 123
#endif

typedef struct MiniLdLoongarchStaticGot {
    size_t section;
    size_t symbol_count;
    size_t *symbol_slots;
    size_t *tls_ie_slots;
    bool present;
} MiniLdLoongarchStaticGot;

static bool minild_loongarch_static_uses_got(uint32_t type) {
    switch (type) {
    case R_LARCH_GOT_PC_HI20:
    case R_LARCH_GOT_PC_LO12:
    case R_LARCH_GOT64_PC_LO20:
    case R_LARCH_GOT64_PC_HI12:
    case R_LARCH_GOT_HI20:
    case R_LARCH_GOT_LO12:
    case R_LARCH_GOT64_LO20:
    case R_LARCH_GOT64_HI12: return true;
    default: return false;
    }
}

static bool minild_loongarch_static_uses_tls_ie(uint32_t type) {
    switch (type) {
    case R_LARCH_TLS_IE_PC_HI20:
    case R_LARCH_TLS_IE_PC_LO12:
    case R_LARCH_TLS_IE64_PC_LO20:
    case R_LARCH_TLS_IE64_PC_HI12:
    case R_LARCH_TLS_IE_HI20:
    case R_LARCH_TLS_IE_LO12:
    case R_LARCH_TLS_IE64_LO20:
    case R_LARCH_TLS_IE64_HI12: return true;
    default: return false;
    }
}

static bool minild_loongarch_static_uses_tls_le(uint32_t type) {
    switch (type) {
    case R_LARCH_TLS_DTPREL32:
    case R_LARCH_TLS_DTPREL64:
    case R_LARCH_TLS_TPREL32:
    case R_LARCH_TLS_TPREL64:
    case R_LARCH_TLS_LE_HI20:
    case R_LARCH_TLS_LE_LO12:
    case R_LARCH_TLS_LE64_LO20:
    case R_LARCH_TLS_LE64_HI12:
    case R_LARCH_TLS_LE_HI20_R:
    case R_LARCH_TLS_LE_LO12_R:
    case R_LARCH_TLS_LE_ADD_R: return true;
    default: return false;
    }
}

static bool minild_loongarch_static_tls_offset(
    MiniLdState *state, const MiniLdLayout *layout,
    size_t symbol_index, uint64_t *offset) {
    const MiniLdResolvedSymbol *symbol;
    MiniLdTlsProjection tls;
    uint64_t address;

    if (symbol_index >= state->resolver.count) {
        fprintf(state->diagnostics,
                "minic-ld: loongarch-static-tls-symbol-out-of-range\n");
        return false;
    }
    symbol = &state->resolver.entries[symbol_index];
    /* An unresolved weak TLS reference has no thread-local definition to
     * place in PT_TLS.  Like GNU's static TLS GOT entry, its IE/LE TPREL is
     * zero.  Keep the exception narrow: defined TLS must still resolve to
     * SHF_TLS storage, and an undefined non-TLS symbol is never accepted. */
    if (symbol->section == MINILD_SECTION_UNDEF &&
        ELF64_ST_BIND(symbol->info) == STB_WEAK &&
        ELF64_ST_TYPE(symbol->info) == STT_TLS) {
        *offset = 0U;
        return true;
    }
    minild_layout_project_tls(state, layout, &tls);
    if (!tls.valid_extents || !tls.has_nonempty_section ||
        ELF64_ST_TYPE(symbol->info) != STT_TLS ||
        symbol->section < 0 ||
        (size_t)symbol->section >= state->work.count ||
        (state->work.sections[symbol->section].flags & SHF_TLS) == 0U ||
        !minild_layout_resolve_symbol(state, layout, symbol_index, &address) ||
        address < tls.block_base || address > tls.memory_end) {
        fprintf(state->diagnostics,
                "minic-ld: invalid-loongarch-static-tls-symbol:%s\n",
                symbol->name);
        return false;
    }
    /* Variant I (like RISC-V): TP precedes the PT_TLS image.  The alignment
     * residue is mandated by the psABI when p_vaddr is not p_align-aligned. */
    *offset = address - tls.block_base +
              (tls.block_base & (tls.alignment - 1U));
    return true;
}

static void minild_loongarch_destroy_static_relocations(
    MiniLdStaticTargetPlan *plan) {
    MiniLdLoongarchStaticGot *got = plan->target_data;

    if (got != NULL) {
        free(got->symbol_slots);
        free(got->tls_ie_slots);
        free(got);
        plan->target_data = NULL;
    }
}

static bool minild_loongarch_reserve_static_relocations(
    MiniLdState *state, MiniLdStaticTargetPlan *plan) {
    MiniLdLoongarchStaticGot *got;
    size_t i;

    if (plan->target_data != NULL) {
        fprintf(state->diagnostics,
                "minic-ld: loongarch-static-plan-already-reserved\n");
        return false;
    }
    got = calloc(1U, sizeof(*got));
    if (got == NULL) {
        return false;
    }
    plan->target_data = got;
    got->section = SIZE_MAX;
    got->symbol_count = state->resolver.count;
    if (got->symbol_count > SIZE_MAX / sizeof(size_t)) {
        fprintf(state->diagnostics,
                "minic-ld: loongarch-static-got-symbol-overflow\n");
        return false;
    }
    got->symbol_slots = malloc((got->symbol_count == 0U ? 1U :
                                got->symbol_count) * sizeof(size_t));
    got->tls_ie_slots = malloc((got->symbol_count == 0U ? 1U :
                                got->symbol_count) * sizeof(size_t));
    if (got->symbol_slots == NULL || got->tls_ie_slots == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:loongarch-static-got\n");
        return false;
    }
    for (i = 0U; i < got->symbol_count; ++i) {
        got->symbol_slots[i] = SIZE_MAX;
        got->tls_ie_slots[i] = SIZE_MAX;
    }
    for (i = 0U; i < state->relocations.count; ++i) {
        const MiniLdReloc *reloc = &state->relocations.entries[i];
        bool tls_ie = minild_loongarch_static_uses_tls_ie(reloc->type);
        size_t *slots;
        MiniLdSection *section;
        size_t aligned;

        if (!tls_ie && !minild_loongarch_static_uses_got(reloc->type)) {
            continue;
        }
        if (reloc->symbol == SIZE_MAX ||
            reloc->symbol >= got->symbol_count || reloc->addend != 0 ||
            (tls_ie && ELF64_ST_TYPE(
                state->resolver.entries[reloc->symbol].info) != STT_TLS)) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-loongarch-static-got-reloc:%u\n",
                    (unsigned)reloc->type);
            return false;
        }
        slots = tls_ie ? got->tls_ie_slots : got->symbol_slots;
        if (slots[reloc->symbol] != SIZE_MAX) {
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
                    "minic-ld: loongarch-static-got-allocation-overflow\n");
            return false;
        }
        slots[reloc->symbol] = aligned;
    }
    return true;
}

static bool minild_loongarch_fill_static_relocations(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdStaticTargetPlan *plan) {
    const MiniLdLoongarchStaticGot *got = plan->target_data;
    MiniLdSection *section;
    size_t i;

    if (got == NULL || layout == NULL || layout->work == NULL) {
        return false;
    }
    if (!got->present) {
        return true;
    }
    if (got->section >= state->work.count) {
        return false;
    }
    section = &state->work.sections[got->section];
    for (i = 0U; i < got->symbol_count; ++i) {
        size_t mode;

        for (mode = 0U; mode < 2U; ++mode) {
            bool tls_ie = mode != 0U;
            size_t off = tls_ie ? got->tls_ie_slots[i] :
                                  got->symbol_slots[i];
            uint64_t value;

            if (off == SIZE_MAX) {
                continue;
            }
            if (!(tls_ie ? minild_loongarch_static_tls_offset(
                                 state, layout, i, &value) :
                             minild_layout_resolve_symbol(
                                 state, layout, i, &value)) ||
                !range_ok(off, 8U, section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-loongarch-static-got-slot\n");
                return false;
            }
            store_u64le(section->data + off, value);
        }
    }
    return true;
}

static bool minild_loongarch_static_resolve_target(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdStaticTargetPlan *plan, const MiniLdReloc *reloc,
    uint64_t *target) {
    const MiniLdLoongarchStaticGot *got = plan->target_data;
    bool tls_ie = minild_loongarch_static_uses_tls_ie(reloc->type);

    if (minild_loongarch_static_uses_got(reloc->type) || tls_ie) {
        size_t slot;

        if (got == NULL || !got->present ||
            got->section >= state->work.count ||
            reloc->symbol >= got->symbol_count) {
            fprintf(state->diagnostics,
                    "minic-ld: missing-loongarch-static-got-plan\n");
            return false;
        }
        slot = tls_ie ? got->tls_ie_slots[reloc->symbol] :
                        got->symbol_slots[reloc->symbol];
        if (slot == SIZE_MAX ||
            layout->work[got->section].vaddr > UINT64_MAX - slot) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-loongarch-static-got-slot-address\n");
            return false;
        }
        *target = layout->work[got->section].vaddr + slot +
                  (uint64_t)reloc->addend;
    } else if (minild_loongarch_static_uses_tls_le(reloc->type) &&
               reloc->type != R_LARCH_TLS_LE_ADD_R) {
        uint64_t offset;

        if (!minild_loongarch_static_tls_offset(state, layout,
                                               reloc->symbol, &offset)) {
            return false;
        }
        *target = offset + (uint64_t)reloc->addend;
    }
    return true;
}
