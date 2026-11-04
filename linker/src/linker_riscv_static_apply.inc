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
        MiniLdSection *section;
        MiniLdStaticRelocOperands operands;
        int64_t target;
        bool live;
        MiniLdTargetRelocClass kind = minild_target_reloc_class(reloc->type);

        if (!minild_static_reloc_source(
                state, layout, reloc,
                "riscv-invalid-relocation-section", &section, &live)) {
            goto fail;
        }
        if (!live || kind == MINILD_TARGET_RELOC_NOOP ||
            kind == MINILD_TARGET_RELOC_ALIGN ||
            kind == MINILD_TARGET_RELOC_PCREL_LO) {
            continue;
        }
        if (!minild_static_reloc_operands(
                state, layout, reloc, "riscv-relocation-place-overflow",
                &operands)) {
            goto fail;
        }
        target = minild_target_signed_bits(operands.target_bits);

        /* Local-exec TLS relocations encode S - TP (+ A), not S + A.  The
         * TPREL_ADD record is a non-mutating binding marker for the add tp
         * instruction, which is verified by the target instruction writer. */
        if (reloc->type == R_RISCV_TPREL_HI20 ||
            reloc->type == R_RISCV_TPREL_LO12_I ||
            reloc->type == R_RISCV_TPREL_LO12_S ||
            reloc->type == R_RISCV_TPREL_ADD) {
            uint64_t tls_offset;

            if (reloc->symbol == SIZE_MAX ||
                !minild_riscv_static_tls_offset(
                    state, layout, reloc->symbol, &tls_offset) ||
                (state->target->elf_class == ELFCLASS32 &&
                 tls_offset > UINT32_MAX)) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-riscv-static-tprel\n");
                goto fail;
            }
            target = minild_target_addend_bits(tls_offset, reloc->addend);
        }

        if (kind == MINILD_TARGET_RELOC_ARITHMETIC) {
            if (state->target->elf_class == ELFCLASS32 &&
                (reloc->type == R_RISCV_ADD64 ||
                 reloc->type == R_RISCV_SUB64)) {
                fprintf(state->diagnostics,
                        "minic-ld: rv32-unsupported-64bit-arithmetic\n");
                goto fail;
            }
            if (!minild_target_apply_arithmetic(
                    section, reloc, target, false, state->diagnostics)) {
                goto fail;
            }
            continue;
        }

        switch (kind) {
        case MINILD_TARGET_RELOC_ABS64:
        case MINILD_TARGET_RELOC_ABS32:
            if (kind == MINILD_TARGET_RELOC_ABS64 &&
                state->target->elf_class == ELFCLASS32) {
                fprintf(state->diagnostics,
                        "minic-ld: rv32-unsupported-R_RISCV_64\n");
                goto fail;
            }
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
        case MINILD_TARGET_RELOC_TLS_IE: {
            size_t offset;

            if (reloc->symbol >= got->symbol_count ||
                reloc->symbol == SIZE_MAX) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-riscv-static-got-symbol\n");
                goto fail;
            }
            offset = kind == MINILD_TARGET_RELOC_TLS_IE
                         ? got->tls_slot_offsets[reloc->symbol]
                         : got->slot_offsets[reloc->symbol];
            if (!got->present || reloc->symbol == SIZE_MAX ||
                reloc->symbol >= got->symbol_count ||
                offset == SIZE_MAX ||
                got->section >= state->work.count ||
                layout->work[got->section].vaddr > UINT64_MAX - offset ||
                reloc->addend != 0) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-riscv-static-got-slot:%u\n",
                        reloc->type);
                goto fail;
            }
            target = minild_target_addend_bits(
                layout->work[got->section].vaddr +
                    (uint64_t)offset,
                0);
            if (!minild_target_apply_final_instruction(
                    &pairs, section, reloc, target, operands.place,
                    false, operands.symbol_name, state->diagnostics)) {
                goto fail;
            }
            break;
        }
        default:
            if (!minild_target_apply_final_instruction(
                    &pairs, section, reloc, target, operands.place,
                    false, operands.symbol_name, state->diagnostics)) {
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
        MiniLdSection *section;
        bool live;

        if (minild_target_reloc_class(reloc->type) !=
            MINILD_TARGET_RELOC_PCREL_LO) {
            continue;
        }
        if (!minild_static_reloc_source(
                state, layout, reloc,
                "riscv-invalid-relocation-section", &section, &live)) {
            goto fail;
        }
        if (!live) {
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
            &pairs, section, reloc,
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
