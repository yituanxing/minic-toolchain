/* ET_EXEC final relocation application.  Input selection, GOT reservation,
 * layout and RISC-V instruction encoding are owned by their earlier phases.
 * This consumes final addresses and mutates work-section bytes only. */

static bool static_apply_relocations(MiniLdState *state,
                                     const MiniLdLayout *layout,
                                     const MiniLdStaticGot *got) {
    size_t capacity = pcrel_capacity_for(state->relocations.count);
    MiniLdPcrelSlot *pcrel;
    size_t i;

    if (capacity == 0U) {
        fprintf(state->diagnostics, "minic-ld: relocation-index-overflow\n");
        return false;
    }
    pcrel = calloc(capacity, sizeof(*pcrel));
    if (pcrel == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:pcrel-index\n");
        return false;
    }

    for (i = 0U; i < state->relocations.count; ++i) {
        MiniLdReloc *reloc = &state->relocations.entries[i];
        MiniLdSection *section = &state->work.sections[reloc->section];
        uint64_t symbol_value = 0U;

        if (layout->scripted &&
            ((section->flags & SHF_ALLOC) == 0U ||
             layout->section_discarded[reloc->section])) {
            continue;
        }
        uint64_t place = layout->section_vaddr[reloc->section] + reloc->offset;
        int64_t target;
        int64_t delta;

        if (reloc->type == R_RISCV_NONE ||
            reloc->type == R_RISCV_RELAX ||
            reloc->type == R_RISCV_ALIGN ||
            reloc->type == R_RISCV_PCREL_LO12_I ||
            reloc->type == R_RISCV_PCREL_LO12_S) {
            continue;
        }
        if (reloc->symbol != SIZE_MAX &&
            !static_resolve_symbol(state, layout, reloc->symbol, &symbol_value)) {
            free(pcrel);
            return false;
        }
        target = (int64_t)symbol_value + reloc->addend;

        if (minild_riscv_is_arithmetic_relocation(reloc->type)) {
            if (!minild_riscv_apply_arithmetic(
                    section, reloc, target, false, state->diagnostics)) {
                free(pcrel);
                return false;
            }
            continue;
        }

        switch (reloc->type) {
        case R_RISCV_64:
            if (section->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
                !range_ok((size_t)reloc->offset, 8U, section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: R_RISCV_64-offset-out-of-range\n");
                free(pcrel);
                return false;
            }
            store_u64le(section->data + (size_t)reloc->offset,
                        (uint64_t)target);
            break;
        case R_RISCV_32:
            if (section->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
                !range_ok((size_t)reloc->offset, 4U, section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: R_RISCV_32-offset-out-of-range\n");
                free(pcrel);
                return false;
            }
            store_u32le(section->data + (size_t)reloc->offset,
                        (uint32_t)target);
            break;
        case R_RISCV_CALL:
        case R_RISCV_CALL_PLT:
            delta = target - (int64_t)place;
            if (!static_patch_utype(section,
                                    reloc->offset,
                                    riscv_hi20(delta),
                                    state->diagnostics) ||
                !static_patch_itype(section,
                                    reloc->offset + 4U,
                                    riscv_lo12(delta),
                                    state->diagnostics)) {
                free(pcrel);
                return false;
            }
            break;

        case R_RISCV_GOT_HI20: {
            uint64_t got_address;

            if (!got->present || reloc->symbol == SIZE_MAX ||
                reloc->symbol >= got->symbol_count ||
                got->slot_offsets[reloc->symbol] == SIZE_MAX ||
                reloc->addend != 0) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-R_RISCV_GOT_HI20-slot\n");
                free(pcrel);
                return false;
            }
            got_address = layout->section_vaddr[got->section] +
                          (uint64_t)got->slot_offsets[reloc->symbol];
            delta = (int64_t)got_address - (int64_t)place;
            if (!static_patch_utype(section,
                                    reloc->offset,
                                    riscv_hi20(delta),
                                    state->diagnostics)) {
                free(pcrel);
                return false;
            }
            pcrel_insert(pcrel,
                         capacity,
                         reloc->section,
                         reloc->offset,
                         delta);
            break;
        }
        case R_RISCV_PCREL_HI20:
            delta = target - (int64_t)place;
            if (!static_patch_utype(section,
                                    reloc->offset,
                                    riscv_hi20(delta),
                                    state->diagnostics)) {
                free(pcrel);
                return false;
            }
            pcrel_insert(pcrel,
                         capacity,
                         reloc->section,
                         reloc->offset,
                         delta);
            break;
        case R_RISCV_HI20:
            if (!static_patch_utype(section,
                                    reloc->offset,
                                    riscv_hi20(target),
                                    state->diagnostics)) {
                free(pcrel);
                return false;
            }
            break;
        case R_RISCV_LO12_I:
            if (!static_patch_itype(section,
                                    reloc->offset,
                                    riscv_lo12(target),
                                    state->diagnostics)) {
                free(pcrel);
                return false;
            }
            break;
        case R_RISCV_LO12_S:
            if (!static_patch_stype(section,
                                    reloc->offset,
                                    riscv_lo12(target),
                                    state->diagnostics)) {
                free(pcrel);
                return false;
            }
            break;
        case R_RISCV_BRANCH:
            delta = target - (int64_t)place;
            if (!static_patch_branch(section,
                                     reloc->offset,
                                     delta,
                                     state->diagnostics)) {
                free(pcrel);
                return false;
            }
            break;
        case R_RISCV_JAL:
            delta = target - (int64_t)place;
            if ((delta & 1) != 0 ||
                delta < -1048576 ||
                delta > 1048574) {
                const char *symbol_name =
                    (reloc->symbol != SIZE_MAX &&
                     reloc->symbol < state->resolver.count)
                        ? state->resolver.entries[reloc->symbol].name
                        : "<none>";
                fprintf(state->diagnostics,
                        "minic-ld: R_RISCV_JAL-overflow:"
                        "section=%s:offset=%llu:symbol=%s:"
                        "place=0x%llx:target=0x%llx:delta=%lld\n",
                        section->name,
                        (unsigned long long)reloc->offset,
                        symbol_name,
                        (unsigned long long)place,
                        (unsigned long long)(uint64_t)target,
                        (long long)delta);
                free(pcrel);
                return false;
            }
            if (!static_patch_jal(section,
                                  reloc->offset,
                                  delta,
                                  state->diagnostics)) {
                free(pcrel);
                return false;
            }
            break;

        case R_RISCV_RVC_BRANCH:
            delta = target - (int64_t)place;
            if (!static_patch_rvc_branch(section,
                                         reloc->offset,
                                         delta,
                                         state->diagnostics)) {
                free(pcrel);
                return false;
            }
            break;
        case R_RISCV_RVC_JUMP:
            delta = target - (int64_t)place;
            if (!static_patch_rvc_jump(section,
                                       reloc->offset,
                                       delta,
                                       state->diagnostics)) {
                free(pcrel);
                return false;
            }
            break;
        case R_RISCV_32_PCREL:
            delta = target - (int64_t)place;
            if (delta < INT32_MIN || delta > INT32_MAX ||
                section->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
                !range_ok((size_t)reloc->offset, 4U, section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: R_RISCV_32_PCREL-overflow-or-range:delta=%lld\n",
                        (long long)delta);
                free(pcrel);
                return false;
            }
            store_u32le(section->data + (size_t)reloc->offset,
                        (uint32_t)(int32_t)delta);
            break;
        default:
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-static-relocation:%u\n",
                    reloc->type);
            free(pcrel);
            return false;
        }
    }

    for (i = 0U; i < state->relocations.count; ++i) {
        MiniLdReloc *reloc = &state->relocations.entries[i];
        MiniLdSection *section;
        MiniLdResolvedSymbol *label;
        int64_t delta;

        if (reloc->type != R_RISCV_PCREL_LO12_I &&
            reloc->type != R_RISCV_PCREL_LO12_S) {
            continue;
        }
        if (reloc->symbol == SIZE_MAX ||
            reloc->symbol >= state->resolver.count) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-pcrel-lo-symbol\n");
            free(pcrel);
            return false;
        }
        label = &state->resolver.entries[reloc->symbol];
        if (label->section < 0 ||
            (size_t)label->section >= state->work.count ||
            !pcrel_find(pcrel,
                        capacity,
                        (size_t)label->section,
                        label->value + (uint64_t)reloc->addend,
                        &delta)) {
            fprintf(state->diagnostics,
                    "minic-ld: missing-matching-pcrel-hi20\n");
            free(pcrel);
            return false;
        }
        section = &state->work.sections[reloc->section];
        if (reloc->type == R_RISCV_PCREL_LO12_I) {
            if (!static_patch_itype(section,
                                    reloc->offset,
                                    riscv_lo12(delta),
                                    state->diagnostics)) {
                free(pcrel);
                return false;
            }
        } else if (!static_patch_stype(section,
                                       reloc->offset,
                                       riscv_lo12(delta),
                                       state->diagnostics)) {
            free(pcrel);
            return false;
        }
    }

    free(pcrel);
    return true;
}
