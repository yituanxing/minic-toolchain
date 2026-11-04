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
#ifndef R_LARCH_IRELATIVE
#define R_LARCH_IRELATIVE 12
#endif

typedef struct MiniLdLoongarchStaticGot {
    size_t section;
    size_t symbol_count;
    size_t *symbol_slots;
    size_t *tls_ie_slots;
    /* GNU static IFUNC: the PLT stub loads a writable .igot.plt slot;
     * __rela_iplt_{start,end} delimit a table of R_LARCH_IRELATIVE entries
     * whose addends are resolver VMAs, not implementations. */
    size_t *ifunc_stub_offsets;
    size_t *ifunc_slot_offsets;
    size_t ifunc_stub_section;
    size_t ifunc_got_section;
    size_t ifunc_rela_section;
    size_t ifunc_count;
    bool present;
} MiniLdLoongarchStaticGot;

static bool minild_loongarch_static_uses_got(uint32_t type);

static bool minild_loongarch_static_ifunc_reference(uint32_t type) {
    switch (type) {
    case R_LARCH_64:
    case R_LARCH_32:
    case R_LARCH_B26:
    case R_LARCH_CALL36:
    case R_LARCH_ABS_HI20:
    case R_LARCH_ABS_LO12:
    case R_LARCH_ABS64_LO20:
    case R_LARCH_ABS64_HI12:
    case R_LARCH_PCALA_HI20:
    case R_LARCH_PCALA_LO12:
    case R_LARCH_PCALA64_LO20:
    case R_LARCH_PCALA64_HI12:
        return true;
    default:
        return minild_loongarch_static_uses_got(type);
    }
}

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
        free(got->ifunc_stub_offsets);
        free(got->ifunc_slot_offsets);
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
    got->ifunc_stub_section = SIZE_MAX;
    got->ifunc_got_section = SIZE_MAX;
    got->ifunc_rela_section = SIZE_MAX;
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
    got->ifunc_stub_offsets = malloc((got->symbol_count == 0U ? 1U :
                                      got->symbol_count) * sizeof(size_t));
    got->ifunc_slot_offsets = malloc((got->symbol_count == 0U ? 1U :
                                      got->symbol_count) * sizeof(size_t));
    if (got->symbol_slots == NULL || got->tls_ie_slots == NULL ||
        got->ifunc_stub_offsets == NULL ||
        got->ifunc_slot_offsets == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:loongarch-static-got\n");
        return false;
    }
    for (i = 0U; i < got->symbol_count; ++i) {
        got->symbol_slots[i] = SIZE_MAX;
        got->tls_ie_slots[i] = SIZE_MAX;
        got->ifunc_stub_offsets[i] = SIZE_MAX;
        got->ifunc_slot_offsets[i] = SIZE_MAX;
    }
    for (i = 0U; i < state->relocations.count; ++i) {
        const MiniLdReloc *reloc = &state->relocations.entries[i];
        bool tls_ie = minild_loongarch_static_uses_tls_ie(reloc->type);
        size_t *slots;
        MiniLdSection *section;
        size_t aligned;

        if (reloc->symbol != SIZE_MAX &&
            reloc->symbol < got->symbol_count &&
            ELF64_ST_TYPE(state->resolver.entries[reloc->symbol].info) ==
                STT_GNU_IFUNC) {
            const MiniLdResolvedSymbol *symbol =
                &state->resolver.entries[reloc->symbol];
            size_t alias;

            if (!minild_loongarch_static_ifunc_reference(reloc->type) ||
                symbol->section < 0) {
                fprintf(state->diagnostics,
                        "minic-ld: unsupported-loongarch-static-ifunc-reference:%u\n",
                        (unsigned)reloc->type);
                return false;
            }
            if (got->ifunc_stub_offsets[reloc->symbol] == SIZE_MAX) {
                /* Weak and hidden aliases sharing the same resolver body
                 * must share one slot, as in GNU BFD's static IPLT. */
                for (alias = 0U; alias < got->symbol_count; ++alias) {
                    const MiniLdResolvedSymbol *old =
                        &state->resolver.entries[alias];
                    if (got->ifunc_stub_offsets[alias] != SIZE_MAX &&
                        old->section == symbol->section &&
                        old->value == symbol->value) {
                        got->ifunc_stub_offsets[reloc->symbol] =
                            got->ifunc_stub_offsets[alias];
                        got->ifunc_slot_offsets[reloc->symbol] =
                            got->ifunc_slot_offsets[alias];
                        break;
                    }
                }
                if (alias == got->symbol_count) {
                    MiniLdSection *stub_section;
                    MiniLdSection *slot_section;
                    MiniLdSection *rela_section;

                    if (!find_or_add_section(state, ".iplt", SHT_PROGBITS,
                                             SHF_ALLOC | SHF_EXECINSTR,
                                             16U, 0U,
                                             &got->ifunc_stub_section) ||
                        !find_or_add_section(state, ".igot.plt", SHT_PROGBITS,
                                             SHF_ALLOC | SHF_WRITE,
                                             8U, 0U,
                                             &got->ifunc_got_section) ||
                        !find_or_add_section(state, ".rela.dyn", SHT_RELA,
                                             SHF_ALLOC, 8U,
                                             sizeof(Elf64_Rela),
                                             &got->ifunc_rela_section)) {
                        return false;
                    }
                    stub_section =
                        &state->work.sections[got->ifunc_stub_section];
                    slot_section =
                        &state->work.sections[got->ifunc_got_section];
                    rela_section =
                        &state->work.sections[got->ifunc_rela_section];
                    got->ifunc_stub_offsets[reloc->symbol] =
                        stub_section->size;
                    got->ifunc_slot_offsets[reloc->symbol] =
                        slot_section->size;
                    if (!section_append_zero(stub_section, 16U) ||
                        !section_append_zero(slot_section, 8U) ||
                        !section_append_zero(rela_section,
                                             sizeof(Elf64_Rela))) {
                        return false;
                    }
                    ++got->ifunc_count;
                }
            }
        }

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
    if (got->present && got->section >= state->work.count) {
        return false;
    }
    if (got->present) {
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
                if (tls_ie) {
                    if (!minild_loongarch_static_tls_offset(state, layout,
                                                           i, &value)) {
                        return false;
                    }
                } else if (got->ifunc_stub_offsets[i] != SIZE_MAX) {
                    value = layout->work[got->ifunc_stub_section].vaddr +
                            (uint64_t)got->ifunc_stub_offsets[i];
                } else if (!minild_layout_resolve_symbol(state, layout, i,
                                                         &value)) {
                    return false;
                }
                if (!range_ok(off, 8U, section->size)) {
                    fprintf(state->diagnostics,
                            "minic-ld: invalid-loongarch-static-got-slot\n");
                    return false;
                }
                store_u64le(section->data + off, value);
            }
        }
    }
    if (got->ifunc_count != 0U) {
        MiniLdSection *stub = &state->work.sections[got->ifunc_stub_section];
        MiniLdSection *slot = &state->work.sections[got->ifunc_got_section];
        MiniLdSection *rela = &state->work.sections[got->ifunc_rela_section];
        uint64_t stub_base = layout->work[got->ifunc_stub_section].vaddr;
        uint64_t slot_base = layout->work[got->ifunc_got_section].vaddr;
        size_t rela_offset = 0U;

        for (i = 0U; i < got->symbol_count; ++i) {
            size_t stub_offset = got->ifunc_stub_offsets[i];
            size_t slot_offset = got->ifunc_slot_offsets[i];
            uint64_t resolver, stub_va, slot_va, delta, rounded, high, low;

            if (stub_offset == SIZE_MAX) {
                continue;
            }
            /* Aliases share the exact stub and a single resolver call. */
            if (i > 0U) {
                size_t prev;
                for (prev = 0U; prev < i; ++prev) {
                    if (got->ifunc_stub_offsets[prev] == stub_offset) {
                        break;
                    }
                }
                if (prev < i) {
                    continue;
                }
            }
            if (slot_offset == SIZE_MAX ||
                !range_ok(stub_offset, 16U, stub->size) ||
                !range_ok(slot_offset, 8U, slot->size) ||
                !range_ok(rela_offset, sizeof(Elf64_Rela), rela->size) ||
                (uint64_t)stub_offset > UINT64_MAX - stub_base ||
                (uint64_t)slot_offset > UINT64_MAX - slot_base ||
                !minild_layout_resolve_symbol(state, layout, i, &resolver)) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-loongarch-static-ifunc-layout\n");
                return false;
            }
            stub_va = stub_base + (uint64_t)stub_offset;
            slot_va = slot_base + (uint64_t)slot_offset;
            delta = slot_va - stub_va;
            rounded = delta + UINT64_C(0x800);
            high = rounded >> 12U;
            if ((rounded & UINT64_C(0x8000000000000000)) != 0U) {
                high |= UINT64_C(0xfff0000000000000);
            }
            low = delta - (high << 12U);
            if (!minild_nonriscv_signed_fits(high, 20U) ||
                !minild_nonriscv_signed_fits(low, 12U)) {
                fprintf(state->diagnostics,
                        "minic-ld: loongarch-static-ifunc-plt-overflow\n");
                return false;
            }
            /* Byte-for-byte GNU BFD IPLT shape:
             * pcalau12i $t3,slot; ld.d $t3,$t3,lo12;
             * jirl $t1,$t3,0; nop.
             * Unlike a page-relative ADRP, PCALA uses the exact stub PC. */
            store_u32le(stub->data + stub_offset,
                        UINT32_C(0x1c00000f) |
                        (((uint32_t)high & UINT32_C(0xfffff)) << 5U));
            store_u32le(stub->data + stub_offset + 4U,
                        UINT32_C(0x28c001ef) |
                        (((uint32_t)low & UINT32_C(0xfff)) << 10U));
            store_u32le(stub->data + stub_offset + 8U,
                        UINT32_C(0x4c0001ed));
            store_u32le(stub->data + stub_offset + 12U,
                        UINT32_C(0x03400000));
            store_u64le(rela->data + rela_offset, slot_va);
            store_u64le(rela->data + rela_offset + 8U,
                        ELF64_R_INFO(0U, R_LARCH_IRELATIVE));
            store_u64le(rela->data + rela_offset + 16U, resolver);
            rela_offset += sizeof(Elf64_Rela);
        }
    }
    return true;
}

static bool minild_loongarch_synthesize_default_boundaries(
    MiniLdState *state, const MiniLdLayout *layout) {
    size_t i;

    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *section = &state->work.sections[i];
        uint64_t base;

        if (strcmp(section->name, ".rela.dyn") != 0 ||
            section->type != SHT_RELA ||
            (section->flags & SHF_ALLOC) == 0U ||
            section->size == 0U) {
            continue;
        }
        base = layout->work[i].vaddr;
        if ((uint64_t)section->size > UINT64_MAX - base) {
            fprintf(state->diagnostics,
                    "minic-ld: loongarch-static-ifunc-rela-span-overflow\n");
            return false;
        }
        return static_define_absolute_if_undefined(
                   state, "__rela_iplt_start", base) &&
               static_define_absolute_if_undefined(
                   state, "__rela_iplt_end",
                   base + (uint64_t)section->size);
    }
    return true;
}

static bool minild_loongarch_static_resolve_target(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdStaticTargetPlan *plan, const MiniLdReloc *reloc,
    uint64_t *target) {
    const MiniLdLoongarchStaticGot *got = plan->target_data;
    bool tls_ie = minild_loongarch_static_uses_tls_ie(reloc->type);

    if (got != NULL && reloc->symbol != SIZE_MAX &&
        reloc->symbol < got->symbol_count &&
        ELF64_ST_TYPE(state->resolver.entries[reloc->symbol].info) ==
            STT_GNU_IFUNC &&
        !minild_loongarch_static_uses_got(reloc->type)) {
        size_t offset = got->ifunc_stub_offsets[reloc->symbol];
        if (offset == SIZE_MAX ||
            got->ifunc_stub_section >= state->work.count ||
            !minild_loongarch_static_ifunc_reference(reloc->type) ||
            layout->work[got->ifunc_stub_section].vaddr >
                UINT64_MAX - offset) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-loongarch-static-ifunc-reference:%u\n",
                    (unsigned)reloc->type);
            return false;
        }
        *target = layout->work[got->ifunc_stub_section].vaddr +
                  (uint64_t)offset + (uint64_t)reloc->addend;
        return true;
    }
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
