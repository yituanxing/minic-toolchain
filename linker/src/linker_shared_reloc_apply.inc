/* ET_DYN final relocation projection and RISC-V work-byte fixups.
 * Dynamic metadata sizing/preemption remains with MiniLdSharedImage;
 * this phase consumes that plan and the final OutputSection membership. */

static bool shared_relocation_live_source(const MiniLdState *state,
                                           const MiniLdOutputPlan *output_plan,
                                           size_t section,
                                           bool *live_out) {
    if (section >= state->work.count) {
        fprintf(state->diagnostics,
                "minic-ld: invalid-shared-relocation-section\n");
        return false;
    }
    *live_out = minild_output_shndx(output_plan, section) != 0U;
    return true;
}

/* Non-ALLOC sections have sh_addr=0, even when a linker script uses a
 * temporary nonzero dot while arranging their input fragments.  Translate
 * work-section offsets through the final output-section owner, exactly as
 * the final symbol view does. */
static bool shared_nonalloc_output_offset(const MiniLdState *state,
                                           const MiniLdLayout *layout,
                                           const MiniLdOutputPlan *output_plan,
                                           size_t section,
                                           uint64_t offset,
                                           uint64_t *value_out) {
    size_t shndx = minild_output_shndx(output_plan, section);
    uint64_t start;
    uint64_t base;

    if (section >= state->work.count || shndx == 0U ||
        shndx > output_plan->count) {
        fprintf(state->diagnostics,
                "minic-ld: invalid-shared-nonalloc-section\n");
        return false;
    }
    start = layout->work[section].vaddr;
    base = output_plan->sections[shndx - 1U].vaddr;
    if (start < base || offset > UINT64_MAX - (start - base)) {
        fprintf(state->diagnostics,
                "minic-ld: invalid-shared-nonalloc-offset\n");
        return false;
    }
    *value_out = start - base + offset;
    return true;
}

static bool shared_resolve_static_relocation_target(
    MiniLdState *state,
    const MiniLdLayout *layout,
    const MiniLdOutputPlan *output_plan,
    const MiniLdReloc *reloc,
    bool nonalloc,
    uint64_t *value_out) {
    if (reloc->symbol == SIZE_MAX) {
        *value_out = 0U;
        return true;
    }
    if (reloc->symbol >= state->resolver.count) {
        fprintf(state->diagnostics,
                "minic-ld: invalid-shared-relocation-symbol\n");
        return false;
    }
    if (nonalloc && state->resolver.entries[reloc->symbol].section ==
                        MINILD_SECTION_UNDEF &&
        ELF64_ST_BIND(state->resolver.entries[reloc->symbol].info) != STB_WEAK) {
        fprintf(state->diagnostics,
                "minic-ld: unresolved-shared-nonalloc-reference:%s\n",
                state->resolver.entries[reloc->symbol].name);
        return false;
    }
    if (nonalloc && state->resolver.entries[reloc->symbol].section >= 0) {
        size_t section =
            (size_t)state->resolver.entries[reloc->symbol].section;
        if (section < state->work.count &&
            (state->work.sections[section].flags & SHF_ALLOC) == 0U) {
            return shared_nonalloc_output_offset(
                state, layout, output_plan, section,
                state->resolver.entries[reloc->symbol].value, value_out);
        }
    }
    return minild_layout_resolve_symbol(state, layout, reloc->symbol, value_out);
}

static bool shared_patch_arithmetic_relocations(
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

        if (!minild_riscv_is_arithmetic_relocation(reloc->type)) {
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
        target = minild_riscv_addend_bits(symbol_value, reloc->addend);

        if (!minild_riscv_apply_arithmetic(
                section, reloc, target, true, state->diagnostics)) {
            return false;
        }
    }
    return true;
}

static bool shared_fill_relocations(MiniLdState *state,
                                    const MiniLdLayout *layout,
                                    const MiniLdOutputPlan *output_plan,
                                    const MiniLdSharedImage *shared) {
    MiniLdSection *rela = &state->work.sections[shared->rela_section];
    size_t write_index = 0U;
    size_t i;

    /* A vDSO may have no dynamic relocations; that valid empty synthetic
     * section has no byte allocation and cannot be passed to memset. */
    if (rela->size != 0U) {
        memset(rela->data, 0, rela->size);
    }

    for (i = 0U; i < state->resolver.count; ++i) {
        size_t offset = shared->symbols[i].got_slot_offset;
        MiniLdResolvedSymbol *target;
        Elf64_Rela output;

        if (offset == SIZE_MAX) {
            continue;
        }
        target = &state->resolver.entries[i];
        if (shared_symbol_is_static_absolute(state, shared, i)) {
            uint64_t target_value;

            if (target->section != MINILD_SECTION_ABS ||
                !minild_layout_resolve_symbol(state, layout, i, &target_value)) {
                fprintf(state->diagnostics,
                        "minic-ld: unresolved-planned-static-absolute:%s\n",
                        target->name);
                return false;
            }
            store_u64le(state->work.sections[shared->got_section].data + offset,
                        target_value);
            continue;
        }
        if (target->section == MINILD_SECTION_ABS &&
            !shared->symbols[i].preemptible) {
            fprintf(state->diagnostics,
                    "minic-ld: unplanned-shared-absolute-got:%s\n",
                    target->name);
            return false;
        }
        memset(&output, 0, sizeof(output));
        output.r_offset =
            layout->work[shared->got_section].vaddr + (uint64_t)offset;
        if (target->section != MINILD_SECTION_UNDEF &&
            !shared->symbols[i].preemptible) {
            uint64_t target_value;

            if (!minild_layout_resolve_symbol(state, layout, i, &target_value)) {
                return false;
            }
            output.r_info = ELF64_R_INFO(0U, R_RISCV_RELATIVE);
            output.r_addend = (Elf64_Sxword)target_value;
        } else {
            size_t dynamic_index = shared->symbols[i].dynsym_index;

            if (dynamic_index == SIZE_MAX) {
                fprintf(state->diagnostics,
                        "minic-ld: missing-dynsym-for-got:%s\n",
                        target->name);
                return false;
            }
            output.r_info = ELF64_R_INFO(dynamic_index, R_RISCV_64);
            output.r_addend = 0;
        }
        memcpy(rela->data + write_index * sizeof(output),
               &output,
               sizeof(output));
        ++write_index;
    }

    for (i = 0U; i < state->relocations.count; ++i) {
        MiniLdReloc *input = &state->relocations.entries[i];
        MiniLdSection *source;
        MiniLdResolvedSymbol *target;
        Elf64_Rela output;
        size_t dynamic_index = 0U;
        bool live;

        if (!shared_relocation_live_source(state, output_plan,
                                           input->section, &live)) {
            return false;
        }
        if (!live || input->type == R_RISCV_NONE ||
            input->type == R_RISCV_RELAX ||
            input->type == R_RISCV_ALIGN) {
            continue;
        }
        source = &state->work.sections[input->section];
        if ((source->flags & SHF_ALLOC) == 0U) {
            uint64_t resolved = 0U;
            uint64_t value;
            size_t width;

            if (input->type == R_RISCV_CALL ||
                input->type == R_RISCV_CALL_PLT ||
                input->type == R_RISCV_GOT_HI20 ||
                input->type == R_RISCV_TLS_GD_HI20) {
                fprintf(state->diagnostics,
                        "minic-ld: unsupported-shared-nonalloc-relocation:%u\n",
                        input->type);
                return false;
            }
            if (shared_relocation_is_final_layout(input->type)) {
                continue;
            }
            if (input->type != R_RISCV_64 &&
                input->type != R_RISCV_32) {
                fprintf(state->diagnostics,
                        "minic-ld: unsupported-shared-nonalloc-relocation:%u\n",
                        input->type);
                return false;
            }
            width = input->type == R_RISCV_64 ? 8U : 4U;
            if (source->type == SHT_NOBITS || input->offset > SIZE_MAX ||
                !range_ok((size_t)input->offset, width, source->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: shared-nonalloc-relocation-offset-out-of-range\n");
                return false;
            }
            if (!shared_resolve_static_relocation_target(
                    state, layout, output_plan, input, true, &resolved)) {
                return false;
            }
            value = resolved + (uint64_t)input->addend;
            if (input->type == R_RISCV_32) {
                /* R_RISCV_32 writes the low word of S+A, matching the
                 * psABI and the existing static-link relocation path. */
                store_u32le(source->data + (size_t)input->offset,
                            (uint32_t)value);
            } else {
                store_u64le(source->data + (size_t)input->offset, value);
            }
            continue;
        }
        if (input->type == R_RISCV_CALL ||
            input->type == R_RISCV_CALL_PLT ||
            shared_relocation_is_final_layout(input->type)) {
            continue;
        }
        if (input->type != R_RISCV_64 ||
            input->symbol == SIZE_MAX ||
            input->symbol >= state->resolver.count) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-shared-relocation:%u\n",
                    input->type);
            return false;
        }
        if (source->type == SHT_NOBITS ||
            input->offset > SIZE_MAX ||
            !range_ok((size_t)input->offset, 8U, source->size)) {
            fprintf(state->diagnostics,
                    "minic-ld: shared-relocation-offset-out-of-range\n");
            return false;
        }
        target = &state->resolver.entries[input->symbol];

        if (shared_symbol_is_static_absolute(state, shared, input->symbol)) {
            uint64_t target_value;

            if (target->section != MINILD_SECTION_ABS ||
                !minild_layout_resolve_symbol(state, layout, input->symbol,
                                       &target_value)) {
                fprintf(state->diagnostics,
                        "minic-ld: unresolved-planned-static-absolute:%s\n",
                        target->name);
                return false;
            }
            store_u64le(source->data + (size_t)input->offset,
                        target_value + (uint64_t)input->addend);
            continue;
        }
        if (target->section == MINILD_SECTION_ABS &&
            !shared->symbols[input->symbol].preemptible) {
            fprintf(state->diagnostics,
                    "minic-ld: unplanned-shared-absolute-relocation:%s\n",
                    target->name);
            return false;
        }

        memset(&output, 0, sizeof(output));
        output.r_offset =
            layout->work[input->section].vaddr + input->offset;
        if (target->section != MINILD_SECTION_UNDEF &&
            !shared->symbols[input->symbol].preemptible) {
            uint64_t target_value;

            if (!minild_layout_resolve_symbol(state,
                                       layout,
                                       input->symbol,
                                       &target_value)) {
                return false;
            }
            output.r_info = ELF64_R_INFO(0U, R_RISCV_RELATIVE);
            output.r_addend =
                minild_riscv_addend_bits(target_value, input->addend);
        } else {
            dynamic_index = shared->symbols[input->symbol].dynsym_index;
            if (dynamic_index == SIZE_MAX) {
                fprintf(state->diagnostics,
                        "minic-ld: missing-dynsym-for-relocation:%s\n",
                        target->name);
                return false;
            }
            output.r_info = ELF64_R_INFO(dynamic_index, R_RISCV_64);
            output.r_addend = input->addend;
        }
        store_u64le(source->data + (size_t)input->offset, 0U);
        memcpy(rela->data + write_index * sizeof(output),
               &output,
               sizeof(output));
        ++write_index;
    }

    for (i = 0U; i < state->resolver.count; ++i) {
        size_t offset = shared->symbols[i].tls_gd_offset;
        size_t dynamic_index;
        uint64_t slot_address;
        Elf64_Rela output;

        if (offset == SIZE_MAX) {
            continue;
        }
        dynamic_index = shared->symbols[i].dynsym_index;
        if (dynamic_index == SIZE_MAX ||
            shared->got_section >= state->work.count) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-shared-tls-gd-dynsym\n");
            return false;
        }
        slot_address =
            layout->work[shared->got_section].vaddr + (uint64_t)offset;

        memset(&output, 0, sizeof(output));
        output.r_offset = slot_address;
        output.r_info =
            ELF64_R_INFO(dynamic_index, R_RISCV_TLS_DTPMOD64);
        memcpy(rela->data + write_index * sizeof(output),
               &output,
               sizeof(output));
        ++write_index;

        memset(&output, 0, sizeof(output));
        output.r_offset = slot_address + 8U;
        output.r_info =
            ELF64_R_INFO(dynamic_index, R_RISCV_TLS_DTPREL64);
        memcpy(rela->data + write_index * sizeof(output),
               &output,
               sizeof(output));
        ++write_index;
    }

    if (write_index != shared->rela_count) {
        fprintf(state->diagnostics,
                "minic-ld: shared-relocation-count-mismatch:%zu:%zu\n",
                write_index,
                shared->rela_count);
        return false;
    }
    return true;
}

static bool shared_patch_final_layout_relocations(
    MiniLdState *state,
    const MiniLdLayout *layout,
    const MiniLdOutputPlan *output_plan,
    const MiniLdSharedImage *shared) {
    MiniLdRiscvPcrelPairs pairs = {0};
    size_t i;
    bool ok = false;

    if (!shared_patch_arithmetic_relocations(state, layout, output_plan)) {
        return false;
    }
    if (!minild_riscv_pairs_init(&pairs, state->relocations.count, true,
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

        if (!shared_relocation_live_source(state, output_plan,
                                           reloc->section, &live)) {
            goto done;
        }
        if (!live || reloc->type == R_RISCV_NONE ||
            reloc->type == R_RISCV_RELAX ||
            reloc->type == R_RISCV_ALIGN ||
            reloc->type == R_RISCV_64 ||
            reloc->type == R_RISCV_CALL ||
            reloc->type == R_RISCV_CALL_PLT ||
            reloc->type == R_RISCV_PCREL_LO12_I ||
            reloc->type == R_RISCV_PCREL_LO12_S ||
            minild_riscv_is_arithmetic_relocation(reloc->type)) {
            continue;
        }
        if (!shared_relocation_is_final_layout(reloc->type)) {
            continue;
        }
        source = &state->work.sections[reloc->section];
        nonalloc = (source->flags & SHF_ALLOC) == 0U;
        if (nonalloc && (reloc->type == R_RISCV_GOT_HI20 ||
                         reloc->type == R_RISCV_TLS_GD_HI20)) {
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-shared-nonalloc-relocation:%u\n",
                    reloc->type);
            goto done;
        }
        if (reloc->symbol != SIZE_MAX &&
            reloc->type != R_RISCV_GOT_HI20 &&
            reloc->type != R_RISCV_TLS_GD_HI20 &&
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
        target = minild_riscv_addend_bits(symbol_value, reloc->addend);
        /* The shared linker chooses GOT/TLSGD slot addresses and output
         * liveness; the RISC-V target owner encodes all instruction types. */
        if (reloc->type == R_RISCV_GOT_HI20) {
            if (reloc->symbol == SIZE_MAX ||
                reloc->symbol >= state->resolver.count ||
                shared->symbols[reloc->symbol].got_slot_offset == SIZE_MAX) {
                fprintf(state->diagnostics,
                        "minic-ld: missing-shared-got-slot\n");
                goto done;
            }
            target = minild_riscv_signed_bits(
                layout->work[shared->got_section].vaddr +
                shared->symbols[reloc->symbol].got_slot_offset);
        } else if (reloc->type == R_RISCV_TLS_GD_HI20) {
            if (reloc->symbol == SIZE_MAX ||
                reloc->symbol >= state->resolver.count ||
                shared->symbols[reloc->symbol].tls_gd_offset == SIZE_MAX) {
                fprintf(state->diagnostics,
                        "minic-ld: missing-shared-tls-gd-slot\n");
                goto done;
            }
            target = minild_riscv_addend_bits(
                layout->work[shared->got_section].vaddr +
                shared->symbols[reloc->symbol].tls_gd_offset, reloc->addend);
        }
        if (!minild_riscv_apply_final_instruction(
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
        MiniLdRiscvLo12Status status;
        bool live;

        if (reloc->type != R_RISCV_PCREL_LO12_I &&
            reloc->type != R_RISCV_PCREL_LO12_S) {
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
        if (!minild_riscv_checked_add_u64_s64(
                anchor->value, reloc->addend, &anchor_offset)) {
            fprintf(state->diagnostics,
                    "minic-ld: missing-shared-pcrel-hi20:%s\n",
                    anchor->name);
            goto done;
        }
        source = &state->work.sections[reloc->section];
        status = minild_riscv_apply_pcrel_lo12(&pairs, source, reloc,
                                               (size_t)anchor->section,
                                               anchor_offset,
                                               state->diagnostics);
        if (status == MINILD_RV_LO12_MISSING) {
            fprintf(state->diagnostics,
                    "minic-ld: missing-shared-pcrel-hi20:%s\n",
                    anchor->name);
            goto done;
        }
        if (status != MINILD_RV_LO12_OK) {
            goto done;
        }
    }

    ok = true;

done:
    minild_riscv_pairs_destroy(&pairs);
    return ok;
}

/* Emit optional dynamic PLT entries and always resolve local CALL pairs. */
static bool shared_finalize_plt_and_calls(MiniLdState *state,
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
        (plt->size != shared->plt_count * 16U ||
         gotplt->size != (3U + shared->plt_count) * 8U ||
         rela_plt->size != shared->plt_count * sizeof(Elf64_Rela))) {
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
        uint64_t entry_address;
        uint64_t slot_address;
        Elf64_Rela relocation;
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

        plt_offset = plt_index * 16U;
        got_offset = (3U + plt_index) * 8U;
        entry_address = plt_address + plt_offset;
        slot_address = gotplt_address + got_offset;
        minild_riscv_emit_plt_entry(plt->data, plt_offset,
                                     entry_address, slot_address);

        memset(&relocation, 0, sizeof(relocation));
        relocation.r_offset = slot_address;
        relocation.r_info =
            ELF64_R_INFO(dynamic_index, R_RISCV_JUMP_SLOT);
        relocation.r_addend = 0;
        memcpy(rela_plt->data + plt_index * sizeof(relocation),
               &relocation,
               sizeof(relocation));
    }

    for (i = 0U; i < state->relocations.count; ++i) {
        MiniLdReloc *reloc = &state->relocations.entries[i];
        MiniLdSection *source;
        MiniLdResolvedSymbol *target;
        uint64_t destination;
        int64_t signed_destination;
        bool live;

        if (reloc->type != R_RISCV_CALL &&
            reloc->type != R_RISCV_CALL_PLT) {
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
            destination = plt_address + plt_index * 16U;
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
        signed_destination = minild_riscv_addend_bits(destination,
                                                    reloc->addend);
        if (!minild_riscv_apply_final_instruction(
                NULL, source, reloc, signed_destination,
                layout->work[reloc->section].vaddr + reloc->offset,
                true, target->name, state->diagnostics)) {
            return false;
        }
    }

    return true;
}
