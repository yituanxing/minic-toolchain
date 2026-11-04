/* Shared pre-layout relocation reservation.  The type classification
 * is common with static linking; PIE/DSO preemption, symbol eligibility,
 * PLT/GOT/TLS allocation and TEXTREL budgeting remain shared policy. */
typedef enum MiniLdSharedRelocScanResult {
    MINILD_SHARED_RELOC_SCAN_OK,
    MINILD_SHARED_RELOC_SCAN_INVALID,
    MINILD_SHARED_RELOC_SCAN_OOM
} MiniLdSharedRelocScanResult;

static MiniLdSharedRelocScanResult
shared_scan_relocation_requirements(MiniLdState *state,
                                    MiniLdSharedImage *shared) {
    size_t i;
    const MiniLdSharedTargetAbi *abi = shared->abi;

    for (i = 0U; i < state->relocations.count; ++i) {
        MiniLdReloc *reloc = &state->relocations.entries[i];
        MiniLdResolvedSymbol *target;
        MiniLdTargetRelocClass kind =
            abi->classify_relocation(reloc->type);

        if (reloc->section >= state->work.count ||
            (state->work.sections[reloc->section].flags & SHF_ALLOC) == 0U) {
            continue;
        }
        if (kind == MINILD_TARGET_RELOC_NOOP ||
            kind == MINILD_TARGET_RELOC_ALIGN) {
            continue;
        }
        if (kind == MINILD_TARGET_RELOC_GOT) {
            if (reloc->symbol == SIZE_MAX ||
                reloc->symbol >= state->resolver.count ||
                reloc->addend != 0) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-shared-got-hi20-relocation\n");
                return MINILD_SHARED_RELOC_SCAN_INVALID;
            }
            if (shared->symbols[reloc->symbol].got_slot_offset == SIZE_MAX) {
                bool static_abs = shared_symbol_is_static_absolute(
                    state, shared, reloc->symbol);

                if (shared->got_count >
                        (SIZE_MAX / abi->got_slot_bytes) - 1U ||
                    (!static_abs && shared->rela_count == SIZE_MAX)) {
                    return MINILD_SHARED_RELOC_SCAN_OOM;
                }
                shared->symbols[reloc->symbol].got_slot_offset =
                    shared->got_count * abi->got_slot_bytes;
                ++shared->got_count;
                if (!static_abs) {
                    ++shared->rela_count;
                }
            }
            continue;
        }
        if (kind == MINILD_TARGET_RELOC_TLS_GD) {
            if (reloc->symbol == SIZE_MAX ||
                reloc->symbol >= state->resolver.count ||
                shared->symbols[reloc->symbol].dynsym_index == SIZE_MAX) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-shared-tls-gd-relocation\n");
                return MINILD_SHARED_RELOC_SCAN_INVALID;
            }
            if (shared->symbols[reloc->symbol].tls_gd_offset == SIZE_MAX) {
                if (shared->tls_gd_count >
                        (SIZE_MAX / abi->tls_gd_slot_bytes) - 1U) {
                    return MINILD_SHARED_RELOC_SCAN_OOM;
                }
                shared->symbols[reloc->symbol].tls_gd_offset =
                    shared->tls_gd_count * abi->tls_gd_slot_bytes;
                ++shared->tls_gd_count;
                if (shared->rela_count > SIZE_MAX - 2U) {
                    return MINILD_SHARED_RELOC_SCAN_OOM;
                }
                shared->rela_count += 2U;
            }
            continue;
        }
        if (abi->relocation_at_final_layout(reloc->type)) {
            continue;
        }
        if (kind == MINILD_TARGET_RELOC_CALL) {
            if (reloc->symbol == SIZE_MAX ||
                reloc->symbol >= state->resolver.count) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-shared-call-relocation\n");
                return MINILD_SHARED_RELOC_SCAN_INVALID;
            }
            target = &state->resolver.entries[reloc->symbol];
            if (shared_symbol_needs_plt(state,
                                        shared,
                                        reloc->symbol) &&
                shared->symbols[reloc->symbol].plt_index == SIZE_MAX) {
                if (shared->symbols[reloc->symbol].dynsym_index == SIZE_MAX) {
                    fprintf(state->diagnostics,
                            "minic-ld: missing-dynsym-for-plt:%s\n",
                            target->name);
                    return MINILD_SHARED_RELOC_SCAN_INVALID;
                }
                shared->symbols[reloc->symbol].plt_index = shared->plt_count++;
            }
            continue;
        }
        if (kind != MINILD_TARGET_RELOC_ABS64) {
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-shared-relocation:%u\n",
                    reloc->type);
            return MINILD_SHARED_RELOC_SCAN_INVALID;
        }
        if (reloc->symbol != SIZE_MAX &&
            shared_symbol_is_static_absolute(state, shared, reloc->symbol)) {
            continue;
        }
        /* A dynamic absolute relocation written into an RX/RO input needs
         * loader textrel permission. GOT/TLS/PLT slots themselves are RW,
         * and source-discarded relocations are pruned before this scan. */
        if ((state->work.sections[reloc->section].flags & SHF_WRITE) == 0U) {
            shared->have_textrel = true;
        }
        ++shared->rela_count;
    }

    return MINILD_SHARED_RELOC_SCAN_OK;
}
