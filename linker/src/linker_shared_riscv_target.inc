/* RV64 ET_DYN instruction and PLT backend.
 * Relocation slot reservation, symbol preemption and dynamic RELA projection
 * are mode-owned and remain in the shared pre-layout/relocation passes.
 * This leaf only patches psABI-specific bytes after final placement. */

static bool riscv_shared_patch_arithmetic_relocations(
    MiniLdState *state,
    const MiniLdLayout *layout,
    const MiniLdOutputPlan *output_plan) {
    size_t i;

    for (i = 0U; i < state->relocations.count; ++i) {
        MiniLdReloc *reloc = &state->relocations.entries[i];
        MiniLdSection *section;
        uint64_t symbol_value = 0U;
        int64_t target;
        bool live;

        if (minild_target_reloc_class(reloc->type) !=
            MINILD_TARGET_RELOC_ARITHMETIC) {
            continue;
        }
        if (!shared_relocation_live_source(state, output_plan,
                                            reloc->section, &live)) {
            return false;
        }
        if (!live) {
            continue;
        }
        section = &state->work.sections[reloc->section];
        if (!shared_resolve_static_relocation_target(
                state, layout, output_plan, reloc,
                (section->flags & SHF_ALLOC) == 0U, &symbol_value)) {
            return false;
        }
        target = minild_target_addend_bits(symbol_value, reloc->addend);

        if (reloc->type == R_RISCV_SET_ULEB128) {
            const MiniLdReloc *sub_reloc;
            bool sub_live;
            uint64_t sub_symbol_value;

            if (i + 1U >= state->relocations.count) {
                fprintf(state->diagnostics,
                        "minic-ld: riscv-uleb-unpaired-relocation\n");
                return false;
            }
            sub_reloc = &state->relocations.entries[i + 1U];
            if (!shared_relocation_live_source(
                    state, output_plan, sub_reloc->section, &sub_live) ||
                !sub_live || sub_reloc->section != reloc->section ||
                !shared_resolve_static_relocation_target(
                    state, layout, output_plan, sub_reloc,
                    (section->flags & SHF_ALLOC) == 0U,
                    &sub_symbol_value) ||
                !minild_riscv_apply_uleb128_pair(
                    section, reloc, sub_reloc, (uint64_t)target,
                    (uint64_t)minild_target_addend_bits(
                        sub_symbol_value, sub_reloc->addend),
                    state->diagnostics)) {
                return false;
            }
            ++i;
            continue;
        }
        if (reloc->type == R_RISCV_SUB_ULEB128) {
            fprintf(state->diagnostics,
                    "minic-ld: riscv-uleb-unpaired-relocation\n");
            return false;
        }

        if (!minild_target_apply_arithmetic(
                section, reloc, target, true, state->diagnostics)) {
            return false;
        }
    }
    return true;
}

static bool riscv_shared_patch_final_layout_relocations(
    MiniLdState *state,
    const MiniLdLayout *layout,
    const MiniLdOutputPlan *output_plan,
    const MiniLdSharedImage *shared) {
    MiniLdTargetRelocPairs pairs = {0};
    size_t i;
    bool ok = false;

    if (!riscv_shared_patch_arithmetic_relocations(state, layout, output_plan)) {
        return false;
    }
    if (!minild_target_reloc_pairs_init(&pairs, state->relocations.count, true,
                                state->diagnostics)) {
        return false;
    }

    for (i = 0U; i < state->relocations.count; ++i) {
        MiniLdReloc *reloc = &state->relocations.entries[i];
        MiniLdSection *source;
        uint64_t symbol_value = 0U;
        uint64_t place;
        int64_t target;
        bool live;
        bool nonalloc;
        MiniLdTargetRelocClass kind = minild_target_reloc_class(reloc->type);

        if (!shared_relocation_live_source(state, output_plan,
                                           reloc->section, &live)) {
            goto done;
        }
        if (!live || kind == MINILD_TARGET_RELOC_NOOP ||
            kind == MINILD_TARGET_RELOC_ALIGN ||
            kind == MINILD_TARGET_RELOC_ABS64 ||
            kind == MINILD_TARGET_RELOC_CALL ||
            kind == MINILD_TARGET_RELOC_PCREL_LO ||
            kind == MINILD_TARGET_RELOC_ARITHMETIC) {
            continue;
        }
        if (!minild_target_reloc_at_final_layout(reloc->type)) {
            continue;
        }
        source = &state->work.sections[reloc->section];
        nonalloc = (source->flags & SHF_ALLOC) == 0U;
        if (nonalloc && (kind == MINILD_TARGET_RELOC_GOT ||
                         kind == MINILD_TARGET_RELOC_TLS_GD)) {
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-shared-nonalloc-relocation:%u\n",
                    reloc->type);
            goto done;
        }
        if (reloc->symbol != SIZE_MAX &&
            kind != MINILD_TARGET_RELOC_GOT &&
            kind != MINILD_TARGET_RELOC_TLS_GD &&
            !shared_resolve_static_relocation_target(
                state, layout, output_plan, reloc, nonalloc,
                &symbol_value)) {
            goto done;
        }

        if (nonalloc) {
            if (!shared_nonalloc_output_offset(state, layout, output_plan,
                                                reloc->section, reloc->offset,
                                                &place)) {
                goto done;
            }
        } else {
            place = layout->work[reloc->section].vaddr + reloc->offset;
        }
        target = minild_target_addend_bits(symbol_value, reloc->addend);
        /* The shared linker chooses GOT/TLSGD slot addresses and output
         * liveness; the RISC-V target owner encodes all instruction types. */
        if (kind == MINILD_TARGET_RELOC_GOT) {
            if (reloc->symbol == SIZE_MAX ||
                reloc->symbol >= state->resolver.count ||
                shared->symbols[reloc->symbol].got_slot_offset == SIZE_MAX) {
                fprintf(state->diagnostics,
                        "minic-ld: missing-shared-got-slot\n");
                goto done;
            }
            target = minild_target_signed_bits(
                layout->work[shared->got_section].vaddr +
                shared->symbols[reloc->symbol].got_slot_offset);
        } else if (kind == MINILD_TARGET_RELOC_TLS_GD) {
            if (reloc->symbol == SIZE_MAX ||
                reloc->symbol >= state->resolver.count ||
                shared->symbols[reloc->symbol].tls_gd_offset == SIZE_MAX) {
                fprintf(state->diagnostics,
                        "minic-ld: missing-shared-tls-gd-slot\n");
                goto done;
            }
            target = minild_target_addend_bits(
                layout->work[shared->got_section].vaddr +
                shared->symbols[reloc->symbol].tls_gd_offset, reloc->addend);
        }
        if (!minild_target_apply_final_instruction(
                &pairs, source, reloc, target, place, true,
                reloc->symbol < state->resolver.count
                    ? state->resolver.entries[reloc->symbol].name : "<none>",
                state->diagnostics)) {
            goto done;
        }
    }

    for (i = 0U; i < state->relocations.count; ++i) {
        MiniLdReloc *reloc = &state->relocations.entries[i];
        MiniLdResolvedSymbol *anchor;
        MiniLdSection *source;
        uint64_t anchor_offset;
        MiniLdTargetLo12Status status;
        bool live;

        if (minild_target_reloc_class(reloc->type) !=
            MINILD_TARGET_RELOC_PCREL_LO) {
            continue;
        }
        if (!shared_relocation_live_source(state, output_plan,
                                           reloc->section, &live)) {
            goto done;
        }
        if (!live) {
            continue;
        }
        if (reloc->symbol == SIZE_MAX ||
            reloc->symbol >= state->resolver.count) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-shared-pcrel-lo-symbol\n");
            goto done;
        }
        anchor = &state->resolver.entries[reloc->symbol];
        if (anchor->section < 0 ||
            (size_t)anchor->section >= state->work.count) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-shared-pcrel-lo-anchor:%s\n",
                    anchor->name);
            goto done;
        }
        if (!minild_target_checked_add_u64_s64(
                anchor->value, reloc->addend, &anchor_offset)) {
            fprintf(state->diagnostics,
                    "minic-ld: missing-shared-pcrel-hi20:%s\n",
                    anchor->name);
            goto done;
        }
        source = &state->work.sections[reloc->section];
        status = minild_target_apply_pcrel_lo12(&pairs, source, reloc,
                                               (size_t)anchor->section,
                                               anchor_offset,
                                               state->diagnostics);
        if (status == MINILD_TARGET_LO12_MISSING) {
            fprintf(state->diagnostics,
                    "minic-ld: missing-shared-pcrel-hi20:%s\n",
                    anchor->name);
            goto done;
        }
        if (status != MINILD_TARGET_LO12_OK) {
            goto done;
        }
    }

    ok = true;

done:
    minild_target_reloc_pairs_destroy(&pairs);
    return ok;
}

/* Emit optional dynamic PLT entries and always resolve local CALL pairs. */
static bool riscv_shared_finalize_plt_and_calls(MiniLdState *state,
                                          const MiniLdLayout *layout,
                                          const MiniLdOutputPlan *output_plan,
                                          const MiniLdSharedImage *shared) {
    MiniLdSection *plt;
    MiniLdSection *gotplt;
    MiniLdSection *rela_plt;
    uint64_t plt_address;
    uint64_t gotplt_address;
    size_t i;

    plt = &state->work.sections[shared->plt_section];
    gotplt = &state->work.sections[shared->gotplt_section];
    rela_plt = &state->work.sections[shared->rela_plt_section];
    plt_address = layout->work[shared->plt_section].vaddr;
    gotplt_address = layout->work[shared->gotplt_section].vaddr;

    if (shared->plt_count != 0U &&
        (plt->size != shared->plt_count * shared->abi->plt_entry_bytes ||
         gotplt->size != (shared->abi->gotplt_header_slots +
                          shared->plt_count) * shared->abi->gotplt_slot_bytes ||
         rela_plt->size !=
             shared->plt_count * shared->abi->rela_record_bytes)) {
        fprintf(state->diagnostics,
                "minic-ld: invalid-shared-plt-size\n");
        return false;
    }

    /*
     * D019-proven eager RV64 PLT.  There is deliberately no lazy PLT0:
     *
     *   auipc t3, %pcrel_hi(slot)
     *   ld    t3, %pcrel_lo(slot)(t3)
     *   jalr  x0, t3, 0
     *   nop
     *
     * R_RISCV_JUMP_SLOT plus DT_BIND_NOW resolves each slot before first use.
     */
    if (shared->plt_count != 0U) {
        memset(plt->data, 0, plt->size);
        memset(gotplt->data, 0, gotplt->size);
        memset(rela_plt->data, 0, rela_plt->size);
    }

    for (i = 0U; i < state->resolver.count; ++i) {
        size_t plt_index = shared->symbols[i].plt_index;
        size_t plt_offset;
        size_t got_offset;
        size_t rela_offset;
        uint64_t entry_address;
        uint64_t slot_address;
        MiniElfRela relocation;
        size_t dynamic_index;

        if (plt_index == SIZE_MAX) {
            continue;
        }
        if (plt_index >= shared->plt_count) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-shared-plt-index\n");
            return false;
        }
        dynamic_index = shared->symbols[i].dynsym_index;
        if (dynamic_index == SIZE_MAX) {
            fprintf(state->diagnostics,
                    "minic-ld: missing-dynsym-for-jump-slot:%s\n",
                    state->resolver.entries[i].name);
            return false;
        }
        if (dynamic_index > UINT32_MAX ||
            shared->abi->rela_record_bytes == 0U ||
            plt_index > SIZE_MAX / shared->abi->rela_record_bytes) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-shared-jump-slot-record\n");
            return false;
        }

        plt_offset = plt_index * shared->abi->plt_entry_bytes;
        got_offset = (shared->abi->gotplt_header_slots + plt_index) *
            shared->abi->gotplt_slot_bytes;
        rela_offset = plt_index * shared->abi->rela_record_bytes;
        if (!range_ok(rela_offset, shared->abi->rela_record_bytes,
                      rela_plt->size)) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-shared-jump-slot-record\n");
            return false;
        }
        entry_address = plt_address + plt_offset;
        slot_address = gotplt_address + got_offset;
        minild_target_emit_plt_entry(plt->data, plt_offset,
                                     entry_address, slot_address);

        memset(&relocation, 0, sizeof(relocation));
        relocation.offset = slot_address;
        relocation.info = minild_target_dynamic_reloc_info(
            dynamic_index, MINILD_DYNAMIC_RELOC_JUMP_SLOT);
        relocation.addend = 0;
        if (!minild_final_pack_rela(rela_plt->data + rela_offset,
                                    state->target->elf_class,
                                    &relocation)) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-shared-jump-slot-relocation\n");
            return false;
        }
    }

    for (i = 0U; i < state->relocations.count; ++i) {
        MiniLdReloc *reloc = &state->relocations.entries[i];
        MiniLdSection *source;
        MiniLdResolvedSymbol *target;
        uint64_t destination;
        int64_t signed_destination;
        bool live;

        if (minild_target_reloc_class(reloc->type) !=
            MINILD_TARGET_RELOC_CALL) {
            continue;
        }
        if (!shared_relocation_live_source(state, output_plan,
                                           reloc->section, &live)) {
            return false;
        }
        if (!live) {
            continue;
        }
        if ((state->work.sections[reloc->section].flags & SHF_ALLOC) == 0U) {
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-shared-nonalloc-relocation:%u\n",
                    reloc->type);
            return false;
        }
        if (reloc->symbol == SIZE_MAX ||
            reloc->symbol >= state->resolver.count) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-shared-call-symbol\n");
            return false;
        }

        source = &state->work.sections[reloc->section];
        target = &state->resolver.entries[reloc->symbol];
        if (shared_symbol_needs_plt(state, shared, reloc->symbol)) {
            size_t plt_index = shared->symbols[reloc->symbol].plt_index;

            if (plt_index == SIZE_MAX) {
                fprintf(state->diagnostics,
                        "minic-ld: missing-plt-for-call:%s\n",
                        target->name);
                return false;
            }
            destination = plt_address +
                plt_index * shared->abi->plt_entry_bytes;
        } else {
            if (!minild_layout_resolve_symbol(state,
                                       layout,
                                       reloc->symbol,
                                       &destination)) {
                return false;
            }
        }
        /* R_RISCV_CALL(_PLT) is S + A - P in both cases: S names either the
         * local definition or the selected PLT entry.  The JUMP_SLOT's own
         * addend remains zero; this addend belongs to the caller's pair. */
        signed_destination = minild_target_addend_bits(destination,
                                                    reloc->addend);
        if (!minild_target_apply_final_instruction(
                NULL, source, reloc, signed_destination,
                layout->work[reloc->section].vaddr + reloc->offset,
                true, target->name, state->diagnostics)) {
            return false;
        }
    }

    return true;
}
