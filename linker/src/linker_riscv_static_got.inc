/* RISC-V ET_EXEC relocation reservation and GOT materialization.
 * Only this target leaf knows the GOT_HI20 policy, slot width, or the
 * machine's generated .got bytes. Layout treats it as an ordinary section. */
typedef struct MiniLdRiscvRelaxPlan MiniLdRiscvRelaxPlan;
static void minild_riscv_relax_plan_destroy(MiniLdRiscvRelaxPlan *plan);

typedef struct MiniLdRiscvStaticGot {
    size_t section;
    size_t *slot_offsets;
    /* IE TLS slots contain S - TP, not the ordinary GOT's absolute S.
     * A symbol may legitimately need both representations. */
    size_t *tls_slot_offsets;
    size_t symbol_count;
    bool present;
    /* Same target-owned session survives generic fixed-point re-layout. */
    MiniLdRiscvRelaxPlan *relax;
} MiniLdRiscvStaticGot;

/* RISC-V TLS variant I initializes tp to the beginning of the PT_TLS
 * image.  Use the same projected TLS base that the ELF writer materializes,
 * rather than the symbol's absolute VMA or an independently counted span. */
static bool minild_riscv_static_tls_offset(
    MiniLdState *state, const MiniLdLayout *layout,
    size_t symbol_index, uint64_t *offset_out) {
    const MiniLdResolvedSymbol *symbol;
    MiniLdTlsProjection tls;
    uint64_t address;

    if (symbol_index >= state->resolver.count) {
        fprintf(state->diagnostics, "minic-ld: riscv-tls-symbol-out-of-range\n");
        return false;
    }
    symbol = &state->resolver.entries[symbol_index];
    /* An unresolved weak TLS symbol is a valid static IE/LE operand: the
     * executable has no TLS allocation for it and the linker resolves its
     * offset to zero.  This is used by glibc's optional weak TLS variables
     * (for example _nl_current_LC_COLLATE).  Ordinary undefined symbols,
     * including strong TLS, must still fail the strict section check. */
    if (symbol->section == MINILD_SECTION_UNDEF &&
        ELF64_ST_BIND(symbol->info) == STB_WEAK &&
        ELF64_ST_TYPE(symbol->info) == STT_TLS) {
        *offset_out = 0U;
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
                "minic-ld: invalid-riscv-static-tls-symbol:%s\n",
                symbol->name);
        return false;
    }
    *offset_out = address - tls.block_base;
    return true;
}

static void minild_riscv_destroy_static_relocations(
    MiniLdStaticTargetPlan *plan) {
    MiniLdRiscvStaticGot *got = plan->target_data;

    if (got == NULL) {
        return;
    }
    free(got->slot_offsets);
    free(got->tls_slot_offsets);
    minild_riscv_relax_plan_destroy(got->relax);
    free(got);
    plan->target_data = NULL;
}

static bool minild_riscv_reserve_static_relocations(
    MiniLdState *state, MiniLdStaticTargetPlan *plan) {
    MiniLdRiscvStaticGot *got;
    size_t slot_width = state->target->elf_class == ELFCLASS32 ? 4U : 8U;
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
    if (got->symbol_count > SIZE_MAX / sizeof(*got->slot_offsets)) {
        fprintf(state->diagnostics,
                "minic-ld: riscv-static-got-symbol-count-overflow\n");
        return false;
    }
    got->slot_offsets = malloc((got->symbol_count == 0U ? 1U : got->symbol_count) *
                               sizeof(*got->slot_offsets));
    got->tls_slot_offsets = malloc(
        (got->symbol_count == 0U ? 1U : got->symbol_count) *
        sizeof(*got->tls_slot_offsets));
    if (got->slot_offsets == NULL || got->tls_slot_offsets == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:static-got\n");
        return false;
    }
    for (i = 0U; i < got->symbol_count; ++i) {
        got->slot_offsets[i] = SIZE_MAX;
        got->tls_slot_offsets[i] = SIZE_MAX;
    }

    for (i = 0U; i < state->relocations.count; ++i) {
        MiniLdReloc *reloc = &state->relocations.entries[i];
        MiniLdSection *section;
        MiniLdTargetRelocClass kind = minild_target_reloc_class(reloc->type);
        size_t *offsets;
        size_t aligned;

        if (kind != MINILD_TARGET_RELOC_GOT &&
            kind != MINILD_TARGET_RELOC_TLS_IE) {
            continue;
        }
        offsets = kind == MINILD_TARGET_RELOC_TLS_IE
                      ? got->tls_slot_offsets : got->slot_offsets;
        if (reloc->symbol == SIZE_MAX ||
            reloc->symbol >= got->symbol_count ||
            reloc->addend != 0) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-riscv-static-got-relocation:%u\n",
                    reloc->type);
            return false;
        }
        if (kind == MINILD_TARGET_RELOC_TLS_IE &&
            ELF64_ST_TYPE(state->resolver.entries[reloc->symbol].info) !=
                STT_TLS) {
            fprintf(state->diagnostics,
                    "minic-ld: riscv-tls-ie-non-tls-symbol\n");
            return false;
        }
        if (offsets[reloc->symbol] != SIZE_MAX) {
            continue;
        }
        if (!got->present) {
            if (!find_or_add_section(state, ".got", SHT_PROGBITS,
                                     SHF_ALLOC | SHF_WRITE, slot_width, 0U,
                                     &got->section)) {
                return false;
            }
            /* Reused source mergers can be GC tombstones. */
            state->work.sections[got->section].gc_discarded = false;
            got->present = true;
        }

        section = &state->work.sections[got->section];
        if (!align_up_size(section->size, slot_width, &aligned) ||
            !section_append_zero(section, aligned - section->size) ||
            !section_append_zero(section, slot_width)) {
            fprintf(state->diagnostics,
                    "minic-ld: static-got-allocation-overflow\n");
            return false;
        }
        offsets[reloc->symbol] = aligned;
    }
    return true;
}

/* RISC-V whole-image CALL/ALIGN replan uses this static target's GOT plan
 * for lifetime ownership; it never changes GOT slot policies. */
#include "linker_riscv_relax_plan.inc"

static bool minild_riscv_fill_static_relocations(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdStaticTargetPlan *plan) {
    const MiniLdRiscvStaticGot *got = plan->target_data;
    size_t slot_width = state->target->elf_class == ELFCLASS32 ? 4U : 8U;
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
        size_t mode;

        for (mode = 0U; mode < 2U; ++mode) {
            bool tls = mode != 0U;
            size_t offset = tls ? got->tls_slot_offsets[i] :
                                  got->slot_offsets[i];
            uint64_t value;

            if (offset == SIZE_MAX) {
                continue;
            }
            if (!(tls ? minild_riscv_static_tls_offset(
                             state, layout, i, &value) :
                         minild_layout_resolve_symbol(
                             state, layout, i, &value)) ||
                (slot_width == 4U && value > UINT32_MAX) ||
                !range_ok(offset, slot_width, section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-riscv-static-got-value-or-range\n");
                return false;
            }
            if (slot_width == 4U) {
                store_u32le(section->data + offset, (uint32_t)value);
            } else {
                store_u64le(section->data + offset, value);
            }
        }
    }
    return true;
}
