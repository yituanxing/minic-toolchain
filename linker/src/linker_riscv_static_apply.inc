/* RISC-V ET_EXEC final relocation pass.  The common driver owns admission,
 * section selection and placement; the machine leaf owns the relocation
 * classes, instruction bytes and the HI20/LO12 pairing contract. */
static bool minild_riscv_apply_static_relocations(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdStaticTargetPlan *plan) {
    const MiniLdRiscvStaticGot *got = plan->target_data;
    MiniLdTargetRelocPairs pairs;
    size_t i;

    if (got == NULL) {
        fprintf(state->diagnostics, "minic-ld: missing-riscv-static-plan\n");
        return false;
    }
    if (!minild_target_reloc_pairs_init(&pairs, state->relocations.count,
                                       false, state->diagnostics)) {
        return false;
    }

    /* Keep input order for arithmetic ADD/SUB/SET and absolute writes.
     * Instruction encoding and HI20 pairing remain target-local. */
    for (i = 0U; i < state->relocations.count; ++i) {
        MiniLdReloc *reloc = &state->relocations.entries[i];
        MiniLdSection *section = &state->work.sections[reloc->section];
        uint64_t symbol_value = 0U;
        uint64_t place;
        int64_t target;
        const char *symbol_name = NULL;
        MiniLdTargetRelocClass kind = minild_target_reloc_class(reloc->type);

        if (layout->scripted &&
            ((section->flags & SHF_ALLOC) == 0U ||
             layout->work[reloc->section].discarded)) {
            continue;
        }
        place = layout->work[reloc->section].vaddr + reloc->offset;
        if (kind == MINILD_TARGET_RELOC_NOOP ||
            kind == MINILD_TARGET_RELOC_ALIGN ||
            kind == MINILD_TARGET_RELOC_PCREL_LO) {
            continue;
        }
        if (reloc->symbol != SIZE_MAX) {
            if (!minild_layout_resolve_symbol(state, layout,
                                              reloc->symbol, &symbol_value)) {
                goto fail;
            }
            if (reloc->symbol < state->resolver.count) {
                symbol_name = state->resolver.entries[reloc->symbol].name;
            }
        }
        target = minild_target_addend_bits(symbol_value, reloc->addend);

        if (kind == MINILD_TARGET_RELOC_ARITHMETIC) {
            if (!minild_target_apply_arithmetic(
                    section, reloc, target, false, state->diagnostics)) {
                goto fail;
            }
            continue;
        }

        switch (kind) {
        case MINILD_TARGET_RELOC_ABS64:
        case MINILD_TARGET_RELOC_ABS32:
            if (section->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
                !range_ok((size_t)reloc->offset,
                          minild_target_absolute_width(kind), section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: %s\n",
                        kind == MINILD_TARGET_RELOC_ABS64
                            ? "R_RISCV_64-offset-out-of-range"
                            : "R_RISCV_32-offset-out-of-range");
                goto fail;
            }
            minild_target_store_absolute(section->data + (size_t)reloc->offset,
                                         kind, (uint64_t)target);
            break;
        case MINILD_TARGET_RELOC_GOT:
            if (!got->present || reloc->symbol == SIZE_MAX ||
                reloc->symbol >= got->symbol_count ||
                got->slot_offsets[reloc->symbol] == SIZE_MAX ||
                reloc->addend != 0) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-R_RISCV_GOT_HI20-slot\n");
                goto fail;
            }
            target = minild_target_addend_bits(
                layout->work[got->section].vaddr +
                    (uint64_t)got->slot_offsets[reloc->symbol],
                0);
            /* Fall through to the target-level final instruction writer. */
            /* FALLTHROUGH */
        default:
            if (!minild_target_apply_final_instruction(
                    &pairs, section, reloc, target, place,
                    false, symbol_name, state->diagnostics)) {
                goto fail;
            }
            break;
        }
    }

    /* LO12 anchors can only be resolved after R_RISCV_ALIGN relaxation. */
    for (i = 0U; i < state->relocations.count; ++i) {
        MiniLdReloc *reloc = &state->relocations.entries[i];
        MiniLdResolvedSymbol *label;
        MiniLdTargetLo12Status result;
        uint64_t anchor_offset;

        if (minild_target_reloc_class(reloc->type) !=
            MINILD_TARGET_RELOC_PCREL_LO) {
            continue;
        }
        if (layout->scripted &&
            ((state->work.sections[reloc->section].flags & SHF_ALLOC) == 0U ||
             layout->work[reloc->section].discarded)) {
            continue;
        }
        if (reloc->symbol == SIZE_MAX ||
            reloc->symbol >= state->resolver.count) {
            fprintf(state->diagnostics, "minic-ld: invalid-pcrel-lo-symbol\n");
            goto fail;
        }
        label = &state->resolver.entries[reloc->symbol];
        if (label->section < 0 ||
            (size_t)label->section >= state->work.count ||
            !minild_target_checked_add_u64_s64(
                label->value, reloc->addend, &anchor_offset)) {
            fprintf(state->diagnostics,
                    "minic-ld: missing-matching-pcrel-hi20\n");
            goto fail;
        }
        result = minild_target_apply_pcrel_lo12(
            &pairs, &state->work.sections[reloc->section], reloc,
            (size_t)label->section, anchor_offset,
            state->diagnostics);
        if (result == MINILD_TARGET_LO12_MISSING) {
            fprintf(state->diagnostics,
                    "minic-ld: missing-matching-pcrel-hi20\n");
            goto fail;
        }
        if (result != MINILD_TARGET_LO12_OK) {
            goto fail;
        }
    }

    minild_target_reloc_pairs_destroy(&pairs);
    return true;

fail:
    minild_target_reloc_pairs_destroy(&pairs);
    return false;
}
