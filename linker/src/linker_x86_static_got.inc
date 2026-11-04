/* x86-64 ET_EXEC GOT reservation and final materialization.  R_X86_64_GOTPCREL
 * addresses a GOT slot (G+A-P), unlike the direct symbol-address relaxations
 * permitted for R_X86_64_{REX_,}GOTPCRELX.  Keep the slots in an ordinary
 * writable work section so Common owns their placement and ELF serialization. */
typedef struct MiniLdX86StaticGot {
    size_t section;
    size_t *slot_offsets;
    /* A GOTTPOFF entry contains a TP-relative offset, not the absolute
     * address stored in a GOTPCREL entry for that same symbol. */
    size_t *tls_slot_offsets;
    size_t *ifunc_slot_offsets;
    size_t *ifunc_stub_offsets;
    size_t ifunc_stub_section;
    size_t ifunc_got_section;
    size_t ifunc_rela_section;
    size_t ifunc_count;
    size_t symbol_count;
    bool present;
    bool have_tls;
    uint64_t tls_base;
    uint64_t tls_span;
} MiniLdX86StaticGot;

static bool minild_x86_static_tls_symbol_offset(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdX86StaticGot *got, size_t symbol_index,
    uint64_t *offset_bits) {
    const MiniLdResolvedSymbol *symbol;
    uint64_t address;

    if (symbol_index >= state->resolver.count) {
        fprintf(state->diagnostics,
                "minic-ld: invalid-x86-static-tls-symbol-index\n");
        return false;
    }
    symbol = &state->resolver.entries[symbol_index];
    if (symbol->section == MINILD_SECTION_UNDEF &&
        ELF64_ST_BIND(symbol->info) == STB_WEAK) {
        /* GNU BFD leaves optional weak IE-TLS symbols unresolved: their
         * static GOT slot/relocation evaluates to zero.  It must not be
         * turned into the end of an allocated TLS block. */
        *offset_bits = 0U;
        return true;
    }
    if (!got->have_tls) {
        fprintf(state->diagnostics,
                "minic-ld: missing-x86-static-tls-block\n");
        return false;
    }
    if (ELF64_ST_TYPE(symbol->info) != STT_TLS || symbol->section < 0 ||
        (size_t)symbol->section >= state->work.count ||
        (state->work.sections[symbol->section].flags & SHF_TLS) == 0U ||
        !minild_layout_resolve_symbol(state, layout, symbol_index, &address) ||
        address < got->tls_base || address - got->tls_base > got->tls_span) {
        fprintf(state->diagnostics,
                "minic-ld: invalid-x86-static-tls-symbol:%s\n",
                symbol->name);
        return false;
    }
    *offset_bits = (address - got->tls_base) - got->tls_span;
    return true;
}

static void minild_x86_destroy_static_relocations(
    MiniLdStaticTargetPlan *plan) {
    MiniLdX86StaticGot *got = plan->target_data;

    if (got == NULL) {
        return;
    }
    free(got->slot_offsets);
    free(got->tls_slot_offsets);
    free(got->ifunc_slot_offsets);
    free(got->ifunc_stub_offsets);
    free(got);
    plan->target_data = NULL;
}

static bool minild_x86_reserve_static_relocations(
    MiniLdState *state, MiniLdStaticTargetPlan *plan) {
    MiniLdX86StaticGot *got;
    size_t i;

    if (plan->target_data != NULL) {
        fprintf(state->diagnostics,
                "minic-ld: x86-static-plan-already-reserved\n");
        return false;
    }
    got = calloc(1U, sizeof(*got));
    if (got == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:x86-static-got\n");
        return false;
    }
    plan->target_data = got;
    got->section = SIZE_MAX;
    got->ifunc_stub_section = SIZE_MAX;
    got->ifunc_got_section = SIZE_MAX;
    got->ifunc_rela_section = SIZE_MAX;
    got->symbol_count = state->resolver.count;
    if (got->symbol_count > SIZE_MAX / sizeof(*got->slot_offsets)) {
        fprintf(state->diagnostics,
                "minic-ld: x86-static-got-symbol-count-overflow\n");
        return false;
    }
    got->slot_offsets = malloc((got->symbol_count == 0U ? 1U :
                                got->symbol_count) * sizeof(*got->slot_offsets));
    got->tls_slot_offsets = malloc((got->symbol_count == 0U ? 1U :
                                    got->symbol_count) *
                                   sizeof(*got->tls_slot_offsets));
    got->ifunc_slot_offsets = malloc((got->symbol_count == 0U ? 1U :
                                      got->symbol_count) *
                                     sizeof(*got->ifunc_slot_offsets));
    got->ifunc_stub_offsets = malloc((got->symbol_count == 0U ? 1U :
                                      got->symbol_count) *
                                     sizeof(*got->ifunc_stub_offsets));
    if (got->slot_offsets == NULL || got->tls_slot_offsets == NULL ||
        got->ifunc_slot_offsets == NULL ||
        got->ifunc_stub_offsets == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:x86-static-got\n");
        return false;
    }
    for (i = 0U; i < got->symbol_count; ++i) {
        got->slot_offsets[i] = SIZE_MAX;
        got->tls_slot_offsets[i] = SIZE_MAX;
        got->ifunc_slot_offsets[i] = SIZE_MAX;
        got->ifunc_stub_offsets[i] = SIZE_MAX;
    }

    for (i = 0U; i < state->relocations.count; ++i) {
        const MiniLdReloc *reloc = &state->relocations.entries[i];
        MiniLdSection *section;
        size_t aligned;

        if (reloc->symbol != SIZE_MAX &&
            reloc->symbol < got->symbol_count &&
            ELF64_ST_TYPE(state->resolver.entries[reloc->symbol].info) ==
                STT_GNU_IFUNC &&
            (reloc->type == R_X86_64_PLT32 ||
             reloc->type == R_X86_64_PC32 ||
             reloc->type == R_X86_64_64 ||
             reloc->type == R_X86_64_32 ||
             reloc->type == R_X86_64_32S) &&
            got->ifunc_slot_offsets[reloc->symbol] == SIZE_MAX) {
            MiniLdSection *stub_section;
            MiniLdSection *ifunc_got;
            MiniLdSection *rela_section;
            size_t stub_offset;
            size_t got_offset;

            if (state->resolver.entries[reloc->symbol].section < 0 ||
                !find_or_add_section(state, ".iplt", SHT_PROGBITS,
                                     SHF_ALLOC | SHF_EXECINSTR, 8U, 0U,
                                     &got->ifunc_stub_section) ||
                !find_or_add_section(state, ".igot.plt", SHT_PROGBITS,
                                     SHF_ALLOC | SHF_WRITE, 8U, 0U,
                                     &got->ifunc_got_section) ||
                !find_or_add_section(state, ".rela.plt", SHT_RELA,
                                     SHF_ALLOC, 8U, sizeof(Elf64_Rela),
                                     &got->ifunc_rela_section)) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-x86-static-ifunc-reservation\n");
                return false;
            }
            stub_section = &state->work.sections[got->ifunc_stub_section];
            stub_offset = stub_section->size;
            if (!section_append_zero(stub_section, 8U)) {
                return false;
            }
            ifunc_got = &state->work.sections[got->ifunc_got_section];
            got_offset = ifunc_got->size;
            if (!section_append_zero(ifunc_got, 8U)) {
                return false;
            }
            rela_section = &state->work.sections[got->ifunc_rela_section];
            if (!section_append_zero(rela_section, sizeof(Elf64_Rela))) {
                return false;
            }
            got->ifunc_stub_offsets[reloc->symbol] = stub_offset;
            got->ifunc_slot_offsets[reloc->symbol] = got_offset;
            ++got->ifunc_count;
        }

        if (reloc->type != R_X86_64_GOTPCREL &&
            reloc->type != R_X86_64_GOTTPOFF) {
            continue;
        }
        if (reloc->symbol == SIZE_MAX ||
            reloc->symbol >= got->symbol_count) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-x86-static-got-symbol\n");
            return false;
        }
        if ((reloc->type == R_X86_64_GOTPCREL
                 ? got->slot_offsets : got->tls_slot_offsets)
                [reloc->symbol] != SIZE_MAX) {
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
                    "minic-ld: x86-static-got-allocation-overflow\n");
            return false;
        }
        if (reloc->type == R_X86_64_GOTPCREL) {
            got->slot_offsets[reloc->symbol] = aligned;
        } else {
            got->tls_slot_offsets[reloc->symbol] = aligned;
        }
    }
    return true;
}
static bool minild_x86_fill_static_relocations(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdStaticTargetPlan *plan) {
    MiniLdX86StaticGot *got = plan->target_data;
    MiniLdSection *section;
    size_t i;

    if (got == NULL) {
        fprintf(state->diagnostics, "minic-ld: missing-x86-static-plan\n");
        return false;
    }
    {
        MiniLdTlsProjection tls;
        uint64_t memsz;
        size_t aligned_span;

        minild_layout_project_tls(state, layout, &tls);
        if (tls.has_nonempty_section) {
            if (!tls.valid_extents || tls.alignment == 0U ||
                tls.memory_end < tls.block_base ||
                tls.memory_end - tls.block_base > SIZE_MAX) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-x86-static-tls-layout\n");
                return false;
            }
            memsz = tls.memory_end - tls.block_base;
            if (tls.alignment > SIZE_MAX ||
                !align_up_size((size_t)memsz, (size_t)tls.alignment,
                               &aligned_span)) {
                fprintf(state->diagnostics,
                        "minic-ld: x86-static-tls-offset-overflow\n");
                return false;
            }
            /* x86-64 uses ELF TLS variant II: TP follows the entire TLS
             * block, rounded up to its maximum alignment. */
            got->have_tls = true;
            got->tls_base = tls.block_base;
            got->tls_span = (uint64_t)aligned_span;
        }
    }
    if (got->present) {
        if (layout == NULL || layout->work == NULL ||
            got->section >= state->work.count) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-x86-static-got-section\n");
            return false;
        }
        section = &state->work.sections[got->section];

        for (i = 0U; i < got->symbol_count; ++i) {
            uint64_t value;

            if (got->slot_offsets[i] == SIZE_MAX) {
                continue;
            }
            /* A plain GOT symbol slot cannot hold an IFUNC resolver VA. */
            if (ELF64_ST_TYPE(state->resolver.entries[i].info) ==
                    STT_GNU_IFUNC) {
                fprintf(state->diagnostics,
                        "minic-ld: unsupported-x86-static-ifunc-got\n");
                return false;
            }
            if (!minild_layout_resolve_symbol(state, layout, i, &value) ||
                !range_ok(got->slot_offsets[i], 8U, section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-x86-static-got-slot\n");
                return false;
            }
            store_u64le(section->data + got->slot_offsets[i], value);
        }
        for (i = 0U; i < got->symbol_count; ++i) {
            uint64_t offset_bits;

            if (got->tls_slot_offsets[i] == SIZE_MAX) {
                continue;
            }
            if (!minild_x86_static_tls_symbol_offset(state, layout, got, i,
                                                     &offset_bits) ||
                !range_ok(got->tls_slot_offsets[i], 8U, section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-x86-static-tls-got-slot\n");
                return false;
            }
            store_u64le(section->data + got->tls_slot_offsets[i],
                        offset_bits);
        }
    }
    if (got->ifunc_count != 0U) {
        MiniLdSection *stub = &state->work.sections[got->ifunc_stub_section];
        MiniLdSection *ifunc_got =
            &state->work.sections[got->ifunc_got_section];
        MiniLdSection *rela = &state->work.sections[got->ifunc_rela_section];
        uint64_t stub_base = layout->work[got->ifunc_stub_section].vaddr;
        uint64_t got_base = layout->work[got->ifunc_got_section].vaddr;
        size_t rela_offset = 0U;

        for (i = 0U; i < got->symbol_count; ++i) {
            uint64_t resolver;
            uint64_t got_va;
            uint64_t stub_va;
            uint64_t delta;
            size_t stub_offset = got->ifunc_stub_offsets[i];
            size_t got_offset = got->ifunc_slot_offsets[i];

            if (stub_offset == SIZE_MAX) {
                continue;
            }
            if (!range_ok(stub_offset, 8U, stub->size) ||
                !range_ok(got_offset, 8U, ifunc_got->size) ||
                !range_ok(rela_offset, sizeof(Elf64_Rela), rela->size) ||
                (uint64_t)stub_offset > UINT64_MAX - stub_base ||
                (uint64_t)got_offset > UINT64_MAX - got_base ||
                !minild_layout_resolve_symbol(state, layout, i, &resolver)) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-x86-static-ifunc-layout\n");
                return false;
            }
            got_va = got_base + (uint64_t)got_offset;
            stub_va = stub_base + (uint64_t)stub_offset;
            delta = got_va - (stub_va + 6U);
            if (!minild_nonriscv_signed_fits(delta, 32U)) {
                fprintf(state->diagnostics,
                        "minic-ld: x86-static-ifunc-plt-overflow\n");
                return false;
            }
            /* 8-byte static IPLT: JMP QWORD PTR [RIP+disp32]; 66 NOP. */
            stub->data[stub_offset] = 0xffU;
            stub->data[stub_offset + 1U] = 0x25U;
            store_u32le(stub->data + stub_offset + 2U, (uint32_t)delta);
            stub->data[stub_offset + 6U] = 0x66U;
            stub->data[stub_offset + 7U] = 0x90U;
            /* glibc's apply_irel calls this resolver, then writes the
             * selected implementation address into the GOT slot. */
            store_u64le(rela->data + rela_offset, got_va);
            store_u64le(rela->data + rela_offset + 8U,
                        ELF64_R_INFO(0U, R_X86_64_IRELATIVE));
            store_u64le(rela->data + rela_offset + 16U, resolver);
            rela_offset += sizeof(Elf64_Rela);
        }
    }
    return true;
}

static bool minild_x86_synthesize_default_boundaries(
    MiniLdState *state, const MiniLdLayout *layout) {
    size_t i;

    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *section = &state->work.sections[i];
        uint64_t base;

        if (strcmp(section->name, ".rela.plt") != 0 ||
            section->type != SHT_RELA ||
            (section->flags & SHF_ALLOC) == 0U ||
            section->size == 0U) {
            continue;
        }
        base = layout->work[i].vaddr;
        if (section->size > UINT64_MAX - base) {
            fprintf(state->diagnostics,
                    "minic-ld: x86-static-ifunc-rela-span-overflow\n");
            return false;
        }
        return static_define_absolute_if_undefined(state,
                                                   "__rela_iplt_start", base) &&
               static_define_absolute_if_undefined(
                   state, "__rela_iplt_end", base + (uint64_t)section->size);
    }
    return true;
}
