/* ET_EXEC final relocation application.  Input selection, GOT reservation,
 * layout and RISC-V instruction encoding are owned by their earlier phases.
 * This consumes final addresses and mutates work-section bytes only. */

static bool static_apply_relocations(MiniLdState *state,
                                     const MiniLdLayout *layout,
                                     const MiniLdStaticGot *got) {
    MiniLdRiscvPcrelPairs pairs;
    size_t i;

    if (!minild_riscv_pairs_init(&pairs, state->relocations.count,
                                 false, state->diagnostics)) {
        return false;
    }

    /* Keep input order for arithmetic ADD/SUB/SET and absolute writes.
     * Only the target-dependent instruction encoding and HI20 pairing
     * are owned by the common RISC-V final fixup stage. */
    for (i = 0U; i < state->relocations.count; ++i) {
        MiniLdReloc *reloc = &state->relocations.entries[i];
        MiniLdSection *section = &state->work.sections[reloc->section];
        uint64_t symbol_value = 0U;
        uint64_t place;
        int64_t target;
        const char *symbol_name = NULL;

        if (layout->scripted &&
            ((section->flags & SHF_ALLOC) == 0U ||
             layout->work[reloc->section].discarded)) {
            continue;
        }
        place = layout->work[reloc->section].vaddr + reloc->offset;
        if (reloc->type == R_RISCV_NONE ||
            reloc->type == R_RISCV_RELAX ||
            reloc->type == R_RISCV_ALIGN ||
            reloc->type == R_RISCV_PCREL_LO12_I ||
            reloc->type == R_RISCV_PCREL_LO12_S) {
            continue;
        }
        if (reloc->symbol != SIZE_MAX) {
            if (!static_resolve_symbol(state, layout,
                                       reloc->symbol, &symbol_value)) {
                goto fail;
            }
            if (reloc->symbol < state->resolver.count) {
                symbol_name = state->resolver.entries[reloc->symbol].name;
            }
        }
        target = minild_riscv_addend_bits(symbol_value, reloc->addend);

        if (minild_riscv_is_arithmetic_relocation(reloc->type)) {
            if (!minild_riscv_apply_arithmetic(
                    section, reloc, target, false, state->diagnostics)) {
                goto fail;
            }
            continue;
        }

        switch (reloc->type) {
        case R_RISCV_64:
            if (section->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
                !range_ok((size_t)reloc->offset, 8U, section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: R_RISCV_64-offset-out-of-range\n");
                goto fail;
            }
            store_u64le(section->data + (size_t)reloc->offset,
                        (uint64_t)target);
            break;
        case R_RISCV_32:
            if (section->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
                !range_ok((size_t)reloc->offset, 4U, section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: R_RISCV_32-offset-out-of-range\n");
                goto fail;
            }
            store_u32le(section->data + (size_t)reloc->offset,
                        (uint32_t)target);
            break;
        case R_RISCV_GOT_HI20:
            if (!got->present || reloc->symbol == SIZE_MAX ||
                reloc->symbol >= got->symbol_count ||
                got->slot_offsets[reloc->symbol] == SIZE_MAX ||
                reloc->addend != 0) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-R_RISCV_GOT_HI20-slot\n");
                goto fail;
            }
            target = minild_riscv_addend_bits(
                layout->work[got->section].vaddr +
                    (uint64_t)got->slot_offsets[reloc->symbol],
                0);
            /* Fall through to the target-level final instruction writer. */
            /* FALLTHROUGH */
        default:
            if (!minild_riscv_apply_final_instruction(
                    &pairs, section, reloc, target, place,
                    false, symbol_name, state->diagnostics)) {
                goto fail;
            }
            break;
        }
    }

    /* LO12 anchor coordinates are final only after ALIGN relaxation.  The
     * common pair owner performs the lookup and field patch; static output
     * retains its existing missing-anchor diagnostic. */
    for (i = 0U; i < state->relocations.count; ++i) {
        MiniLdReloc *reloc = &state->relocations.entries[i];
        MiniLdResolvedSymbol *label;
        MiniLdRiscvLo12Status result;
        uint64_t anchor_offset;

        if (reloc->type != R_RISCV_PCREL_LO12_I &&
            reloc->type != R_RISCV_PCREL_LO12_S) {
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
            !minild_riscv_checked_add_u64_s64(
                label->value, reloc->addend, &anchor_offset)) {
            fprintf(state->diagnostics,
                    "minic-ld: missing-matching-pcrel-hi20\n");
            goto fail;
        }
        result = minild_riscv_apply_pcrel_lo12(
            &pairs, &state->work.sections[reloc->section], reloc,
            (size_t)label->section, anchor_offset,
            state->diagnostics);
        if (result == MINILD_RV_LO12_MISSING) {
            fprintf(state->diagnostics,
                    "minic-ld: missing-matching-pcrel-hi20\n");
            goto fail;
        }
        if (result != MINILD_RV_LO12_OK) {
            goto fail;
        }
    }

    minild_riscv_pairs_destroy(&pairs);
    return true;

fail:
    minild_riscv_pairs_destroy(&pairs);
    return false;
}
