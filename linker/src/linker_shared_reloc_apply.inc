/* ET_DYN dynamic RELA projection from canonical reservations.
 * Dynamic metadata sizing/preemption remains with MiniLdSharedImage;
 * this phase consumes that plan and final OutputSection membership.
 * Target instruction fixups and PLT byte emission live in the RV leaf. */

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
    if (!minild_output_plan_nonalloc_value(
            state, layout, output_plan, section, offset, value_out)) {
        fprintf(state->diagnostics,
                "minic-ld: invalid-shared-nonalloc-offset\n");
        return false;
    }
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

/* The shared planner is the sole owner of the dynamic RELA budget.  Every
 * producer (GOT, source ABS64 and TLS-GD) goes through one bounds-checked
 * writer so an unplanned relocation cannot overwrite another section before
 * the final count check notices the mismatch. */
static bool shared_rela_append(MiniLdState *state,
                               const MiniLdSharedImage *shared,
                               size_t *written,
                               const MiniElfRela *record) {
    MiniLdSection *section = &state->work.sections[shared->rela_section];
    size_t stride = shared->abi->rela_record_bytes;

    if (stride == 0U || *written >= shared->rela_count ||
        *written > SIZE_MAX / stride ||
        !range_ok(*written * stride, stride, section->size)) {
        fprintf(state->diagnostics,
                "minic-ld: shared-relocation-count-mismatch:%zu:%zu\n",
                *written + 1U, shared->rela_count);
        return false;
    }
    if (!minild_final_pack_rela(section->data + *written * stride,
                                state->target->elf_class, record)) {
        fprintf(state->diagnostics,
                "minic-ld: invalid-shared-rela-record\n");
        return false;
    }
    ++*written;
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
        MiniElfRela output;

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
            shared->abi->store_absolute(
                state->work.sections[shared->got_section].data + offset,
                MINILD_TARGET_RELOC_ABS64, target_value);
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
        output.offset =
            layout->work[shared->got_section].vaddr + (uint64_t)offset;
        if (target->section != MINILD_SECTION_UNDEF &&
            !shared->symbols[i].preemptible) {
            uint64_t target_value;

            if (!minild_layout_resolve_symbol(state, layout, i, &target_value)) {
                return false;
            }
            output.info = shared->abi->dynamic_relocation_info(
                0U, MINILD_DYNAMIC_RELOC_RELATIVE);
            output.addend = shared->abi->addend_bits(target_value, 0);
        } else {
            size_t dynamic_index = shared->symbols[i].dynsym_index;

            if (dynamic_index == SIZE_MAX) {
                fprintf(state->diagnostics,
                        "minic-ld: missing-dynsym-for-got:%s\n",
                        target->name);
                return false;
            }
            output.info = shared->abi->dynamic_relocation_info(
                dynamic_index, MINILD_DYNAMIC_RELOC_ABSOLUTE);
            output.addend = 0;
        }
        if (!shared_rela_append(state, shared, &write_index, &output)) {
            return false;
        }
    }

    for (i = 0U; i < state->relocations.count; ++i) {
        MiniLdReloc *input = &state->relocations.entries[i];
        MiniLdSection *source;
        MiniLdResolvedSymbol *target;
        MiniElfRela output;
        size_t dynamic_index = 0U;
        bool live;
        MiniLdTargetRelocClass kind =
            shared->abi->classify_relocation(input->type);

        if (!shared_relocation_live_source(state, output_plan,
                                           input->section, &live)) {
            return false;
        }
        if (!live || kind == MINILD_TARGET_RELOC_NOOP ||
            kind == MINILD_TARGET_RELOC_ALIGN) {
            continue;
        }
        source = &state->work.sections[input->section];
        if ((source->flags & SHF_ALLOC) == 0U) {
            uint64_t resolved = 0U;
            uint64_t value;
            size_t width;

            if (kind == MINILD_TARGET_RELOC_CALL ||
                kind == MINILD_TARGET_RELOC_GOT ||
                kind == MINILD_TARGET_RELOC_TLS_GD) {
                fprintf(state->diagnostics,
                        "minic-ld: unsupported-shared-nonalloc-relocation:%u\n",
                        input->type);
                return false;
            }
            if (shared->abi->relocation_at_final_layout(input->type)) {
                continue;
            }
            if (kind != MINILD_TARGET_RELOC_ABS64 &&
                kind != MINILD_TARGET_RELOC_ABS32) {
                fprintf(state->diagnostics,
                        "minic-ld: unsupported-shared-nonalloc-relocation:%u\n",
                        input->type);
                return false;
            }
            width = shared->abi->absolute_relocation_width(kind);
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
            shared->abi->store_absolute(
                source->data + (size_t)input->offset, kind, value);
            continue;
        }
        if (kind == MINILD_TARGET_RELOC_CALL ||
            shared->abi->relocation_at_final_layout(input->type)) {
            continue;
        }
        if (kind != MINILD_TARGET_RELOC_ABS64 ||
            input->symbol == SIZE_MAX ||
            input->symbol >= state->resolver.count) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-shared-relocation:%u\n",
                    input->type);
            return false;
        }
        if (source->type == SHT_NOBITS ||
            input->offset > SIZE_MAX ||
            !range_ok((size_t)input->offset,
                      shared->abi->absolute_relocation_width(kind),
                      source->size)) {
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
            shared->abi->store_absolute(
                source->data + (size_t)input->offset, kind,
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
        output.offset =
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
            output.info = shared->abi->dynamic_relocation_info(
                0U, MINILD_DYNAMIC_RELOC_RELATIVE);
            output.addend =
                shared->abi->addend_bits(target_value, input->addend);
        } else {
            dynamic_index = shared->symbols[input->symbol].dynsym_index;
            if (dynamic_index == SIZE_MAX) {
                fprintf(state->diagnostics,
                        "minic-ld: missing-dynsym-for-relocation:%s\n",
                        target->name);
                return false;
            }
            output.info = shared->abi->dynamic_relocation_info(
                dynamic_index, MINILD_DYNAMIC_RELOC_ABSOLUTE);
            output.addend = input->addend;
        }
        shared->abi->store_absolute(
            source->data + (size_t)input->offset, kind, 0U);
        if (!shared_rela_append(state, shared, &write_index, &output)) {
            return false;
        }
    }

    for (i = 0U; i < state->resolver.count; ++i) {
        size_t offset = shared->symbols[i].tls_gd_offset;
        size_t dynamic_index;
        uint64_t slot_address;
        MiniElfRela output;

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
        output.offset = slot_address;
        output.info = shared->abi->dynamic_relocation_info(
            dynamic_index, MINILD_DYNAMIC_RELOC_TLS_MODULE);
        if (!shared_rela_append(state, shared, &write_index, &output)) {
            return false;
        }

        memset(&output, 0, sizeof(output));
        output.offset = slot_address + shared->abi->got_slot_bytes;
        output.info = shared->abi->dynamic_relocation_info(
            dynamic_index, MINILD_DYNAMIC_RELOC_TLS_OFFSET);
        if (!shared_rela_append(state, shared, &write_index, &output)) {
            return false;
        }
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

/* The only current full ET_DYN backend is RV64.  Keep the machine-code
 * relocation backend next to, but separate from, mode-owned RELA projection. */
#include "linker_shared_riscv_target.inc"
