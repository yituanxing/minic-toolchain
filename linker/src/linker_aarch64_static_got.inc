/* AArch64 ET_EXEC GOT ownership.  GNU-produced ADR_GOT_PAGE/LD64_GOT_LO12
 * pairs reference the address of a pointer-sized GOT entry, not S+A.  The
 * target reserves one entry per referenced canonical symbol before layout;
 * Common assigns .got an address and emits the ordinary section. */
/* Older glibc elf.h versions omit this AAELF64 psABI relocation (315). */
#ifndef R_AARCH64_GOTPCREL32
#define R_AARCH64_GOTPCREL32 315
#endif
/* A53-843419 is an address-dependent *post-layout* target transform.  The
 * work section owns relocated code and its appended 8-byte branch islands;
 * immutable InputGraph sections never change.  Keep the patch plan alongside
 * GOT/IFUNC state so that repeated layout passes remain idempotent. */
typedef struct MiniLdAarch64ErratumPatch {
    size_t section;
    size_t instruction_offset;
    size_t stub_offset;
    uint32_t instruction;
} MiniLdAarch64ErratumPatch;

typedef struct MiniLdAarch64CodeMap {
    size_t section;
    uint64_t offset;
    bool code;
} MiniLdAarch64CodeMap;

typedef struct MiniLdAarch64ErratumPlan {
    MiniLdAarch64ErratumPatch *patches;
    size_t count;
    size_t capacity;
    size_t *original_sizes;
    unsigned char *input_owned;
    size_t original_section_count;
    MiniLdAarch64CodeMap *code_map;
    size_t code_map_count;
    bool initialized;
} MiniLdAarch64ErratumPlan;

typedef struct MiniLdAarch64StaticGot {
    size_t section;
    size_t symbol_count;
    size_t *slot_offsets;
    /* TLS IE entries hold TP-relative offsets, never absolute symbol VMAs.
     * A symbol may appear in both ordinary GOT and IE relocations. */
    size_t *tls_slot_offsets;
    /* A static IFUNC must be called through an IPLT thunk.  glibc walks
     * __rela_iplt_{start,end} at startup, invokes each resolver and installs
     * its selected implementation in the associated writable slot. */
    size_t *ifunc_stub_offsets;
    size_t *ifunc_slot_offsets;
    size_t ifunc_stub_section;
    size_t ifunc_got_section;
    size_t ifunc_rela_section;
    size_t ifunc_count;
    bool present;
    MiniLdAarch64ErratumPlan erratum843419;
} MiniLdAarch64StaticGot;

static bool minild_aarch64_static_uses_got(uint32_t type);

static bool minild_aarch64_static_ifunc_reference(uint32_t type) {
    switch (type) {
    case R_AARCH64_CALL26:
    case R_AARCH64_JUMP26:
    case R_AARCH64_ABS64:
    case R_AARCH64_ADR_PREL_PG_HI21:
    case R_AARCH64_ADD_ABS_LO12_NC:
        return true;
    default:
        return minild_aarch64_static_uses_got(type);
    }
}

static bool minild_aarch64_static_uses_got(uint32_t type) {
    switch (type) {
    case R_AARCH64_GOTREL64:
    case R_AARCH64_GOTREL32:
    case R_AARCH64_GOT_LD_PREL19:
    case R_AARCH64_LD64_GOTOFF_LO15:
    case R_AARCH64_ADR_GOT_PAGE:
    case R_AARCH64_LD64_GOT_LO12_NC:
    case R_AARCH64_LD64_GOTPAGE_LO15:
    case R_AARCH64_GOTPCREL32:
        return true;
    default:
        return false;
    }
}

static bool minild_aarch64_static_uses_tls_ie(uint32_t type) {
    switch (type) {
    case R_AARCH64_TLSIE_MOVW_GOTTPREL_G1:
    case R_AARCH64_TLSIE_MOVW_GOTTPREL_G0_NC:
    case R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21:
    case R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC:
    case R_AARCH64_TLSIE_LD_GOTTPREL_PREL19:
        return true;
    default:
        return false;
    }
}

static bool minild_aarch64_static_uses_tls_le(uint32_t type) {
    return (type >= R_AARCH64_TLSLE_MOVW_TPREL_G2 &&
            type <= R_AARCH64_TLSLE_LDST64_TPREL_LO12_NC) ||
           type == R_AARCH64_TLSLE_LDST128_TPREL_LO12 ||
           type == R_AARCH64_TLSLE_LDST128_TPREL_LO12_NC;
}

/* SysV AArch64 uses TLS variant I.  TP points to a 16-byte TCB and the
 * executable's PT_TLS block follows that TCB, possibly after padding.
 * AAELF's PADsize is (PT_TLS.p_vaddr - 16) mod PT_TLS.p_align. */
static bool minild_aarch64_static_tls_offset(
    MiniLdState *state, const MiniLdLayout *layout,
    size_t symbol_index, uint64_t *offset_out) {
    const MiniLdResolvedSymbol *symbol;
    MiniLdTlsProjection tls;
    uint64_t address;
    uint64_t pad, base_mod, tcb_mod, relative;

    if (symbol_index >= state->resolver.count) {
        fprintf(state->diagnostics,
                "minic-ld: aarch64-static-tls-symbol-out-of-range\n");
        return false;
    }
    symbol = &state->resolver.entries[symbol_index];
    if (ELF64_ST_TYPE(symbol->info) != STT_TLS) {
        fprintf(state->diagnostics,
                "minic-ld: aarch64-static-tls-non-tls-symbol:%s\n",
                symbol->name);
        return false;
    }
    if (symbol->section == MINILD_SECTION_UNDEF &&
        ELF64_ST_BIND(symbol->info) == STB_WEAK) {
        /* An undefined weak TLS reference carries no module allocation. */
        *offset_out = 0U;
        return true;
    }
    minild_layout_project_tls(state, layout, &tls);
    if (!tls.valid_extents || !tls.has_nonempty_section ||
        tls.alignment == 0U || symbol->section < 0 ||
        (size_t)symbol->section >= state->work.count ||
        (state->work.sections[symbol->section].flags & SHF_TLS) == 0U ||
        !minild_layout_resolve_symbol(state, layout, symbol_index, &address) ||
        address < tls.block_base || address > tls.memory_end) {
        fprintf(state->diagnostics,
                "minic-ld: invalid-aarch64-static-tls-symbol:%s\n",
                symbol->name);
        return false;
    }
    base_mod = tls.block_base % tls.alignment;
    tcb_mod = UINT64_C(16) % tls.alignment;
    pad = base_mod >= tcb_mod ? base_mod - tcb_mod :
          tls.alignment - (tcb_mod - base_mod);
    relative = address - tls.block_base;
    if (pad > UINT64_MAX - UINT64_C(16) ||
        relative > UINT64_MAX - (UINT64_C(16) + pad)) {
        fprintf(state->diagnostics,
                "minic-ld: aarch64-static-tls-offset-overflow\n");
        return false;
    }
    *offset_out = UINT64_C(16) + pad + relative;
    return true;
}

static void minild_aarch64_destroy_static_relocations(
    MiniLdStaticTargetPlan *plan) {
    MiniLdAarch64StaticGot *got = plan->target_data;

    if (got != NULL) {
        free(got->slot_offsets);
        free(got->tls_slot_offsets);
        free(got->ifunc_stub_offsets);
        free(got->ifunc_slot_offsets);
        free(got->erratum843419.patches);
        free(got->erratum843419.original_sizes);
        free(got->erratum843419.input_owned);
        free(got->erratum843419.code_map);
        free(got);
        plan->target_data = NULL;
    }
}

static bool minild_aarch64_reserve_static_relocations(
    MiniLdState *state, MiniLdStaticTargetPlan *plan) {
    MiniLdAarch64StaticGot *got;
    size_t i;

    if (plan->target_data != NULL) {
        fprintf(state->diagnostics,
                "minic-ld: aarch64-static-plan-already-reserved\n");
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
    if (got->symbol_count > SIZE_MAX / sizeof(*got->slot_offsets)) {
        fprintf(state->diagnostics,
                "minic-ld: aarch64-static-got-symbol-count-overflow\n");
        return false;
    }
    got->slot_offsets = malloc((got->symbol_count == 0U ? 1U :
                                got->symbol_count) * sizeof(*got->slot_offsets));
    got->tls_slot_offsets = malloc((got->symbol_count == 0U ? 1U :
                                    got->symbol_count) *
                                   sizeof(*got->tls_slot_offsets));
    got->ifunc_stub_offsets = malloc((got->symbol_count == 0U ? 1U :
                                      got->symbol_count) *
                                     sizeof(*got->ifunc_stub_offsets));
    got->ifunc_slot_offsets = malloc((got->symbol_count == 0U ? 1U :
                                      got->symbol_count) *
                                     sizeof(*got->ifunc_slot_offsets));
    if (got->slot_offsets == NULL || got->tls_slot_offsets == NULL ||
        got->ifunc_stub_offsets == NULL ||
        got->ifunc_slot_offsets == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:aarch64-static-got\n");
        return false;
    }
    for (i = 0U; i < got->symbol_count; ++i) {
        got->slot_offsets[i] = SIZE_MAX;
        got->tls_slot_offsets[i] = SIZE_MAX;
        got->ifunc_stub_offsets[i] = SIZE_MAX;
        got->ifunc_slot_offsets[i] = SIZE_MAX;
    }

    for (i = 0U; i < state->relocations.count; ++i) {
        const MiniLdReloc *reloc = &state->relocations.entries[i];
        MiniLdSection *section;
        bool tls_ie = minild_aarch64_static_uses_tls_ie(reloc->type);
        size_t *offsets = tls_ie ? got->tls_slot_offsets : got->slot_offsets;
        size_t aligned;

        if (reloc->symbol != SIZE_MAX &&
            reloc->symbol < got->symbol_count &&
            ELF64_ST_TYPE(state->resolver.entries[reloc->symbol].info) ==
                STT_GNU_IFUNC) {
            MiniLdSection *stub_section;
            MiniLdSection *slot_section;
            MiniLdSection *rela_section;

            if (!minild_aarch64_static_ifunc_reference(reloc->type) ||
                state->resolver.entries[reloc->symbol].section < 0) {
                fprintf(state->diagnostics,
                        "minic-ld: unsupported-aarch64-static-ifunc-reference:%u\n",
                        (unsigned)reloc->type);
                return false;
            }
            if (got->ifunc_stub_offsets[reloc->symbol] == SIZE_MAX) {
                if (!find_or_add_section(state, ".iplt", SHT_PROGBITS,
                                         SHF_ALLOC | SHF_EXECINSTR, 16U, 0U,
                                         &got->ifunc_stub_section) ||
                    !find_or_add_section(state, ".igot.plt", SHT_PROGBITS,
                                         SHF_ALLOC | SHF_WRITE, 8U, 0U,
                                         &got->ifunc_got_section) ||
                    !find_or_add_section(state, ".rela.plt", SHT_RELA,
                                         SHF_ALLOC, 8U, sizeof(Elf64_Rela),
                                         &got->ifunc_rela_section)) {
                    return false;
                }
                stub_section = &state->work.sections[got->ifunc_stub_section];
                slot_section = &state->work.sections[got->ifunc_got_section];
                rela_section = &state->work.sections[got->ifunc_rela_section];
                got->ifunc_stub_offsets[reloc->symbol] = stub_section->size;
                got->ifunc_slot_offsets[reloc->symbol] = slot_section->size;
                if (!section_append_zero(stub_section, 16U) ||
                    !section_append_zero(slot_section, 8U) ||
                    !section_append_zero(rela_section, sizeof(Elf64_Rela))) {
                    return false;
                }
                ++got->ifunc_count;
            }
        }

        if (!minild_aarch64_static_uses_got(reloc->type) && !tls_ie) {
            continue;
        }
        if (reloc->symbol == SIZE_MAX ||
            reloc->symbol >= got->symbol_count ||
            ((tls_ie || (reloc->type != R_AARCH64_GOTREL64 &&
                         reloc->type != R_AARCH64_GOTREL32)) &&
             reloc->addend != 0)) {
            /* AAELF64 GDAT(S) relocations require a zero addend. */
            fprintf(state->diagnostics,
                    "minic-ld: invalid-aarch64-static-got-relocation:%u\n",
                    (unsigned)reloc->type);
            return false;
        }
        if (tls_ie &&
            ELF64_ST_TYPE(state->resolver.entries[reloc->symbol].info) !=
                STT_TLS) {
            fprintf(state->diagnostics,
                    "minic-ld: aarch64-static-tls-ie-non-tls-symbol\n");
            return false;
        }
        if (offsets[reloc->symbol] != SIZE_MAX) {
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
                    "minic-ld: aarch64-static-got-allocation-overflow\n");
            return false;
        }
        offsets[reloc->symbol] = aligned;
    }
    return true;
}

static bool minild_aarch64_fill_static_relocations(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdStaticTargetPlan *plan) {
    const MiniLdAarch64StaticGot *got = plan->target_data;
    MiniLdSection *section;
    size_t i;

    if (got == NULL) {
        fprintf(state->diagnostics, "minic-ld: missing-aarch64-static-plan\n");
        return false;
    }
    if (layout == NULL || layout->work == NULL ||
        (got->present && got->section >= state->work.count)) {
        return false;
    }
    if (got->present) {
        section = &state->work.sections[got->section];
        for (i = 0U; i < got->symbol_count; ++i) {
            size_t mode;

            for (mode = 0U; mode < 2U; ++mode) {
                bool tls_ie = mode != 0U;
                size_t offset = tls_ie ? got->tls_slot_offsets[i] :
                                         got->slot_offsets[i];
                uint64_t value;

                if (offset == SIZE_MAX) {
                    continue;
                }
                if (tls_ie) {
                    if (!minild_aarch64_static_tls_offset(state, layout,
                                                           i, &value)) {
                        return false;
                    }
                } else if (got->ifunc_stub_offsets[i] != SIZE_MAX) {
                    value = layout->work[got->ifunc_stub_section].vaddr +
                            (uint64_t)got->ifunc_stub_offsets[i];
                } else if (!minild_layout_resolve_symbol(state, layout,
                                                          i, &value)) {
                    return false;
                }
                if (!range_ok(offset, 8U, section->size)) {
                    fprintf(state->diagnostics,
                            "minic-ld: invalid-aarch64-static-got-slot\n");
                    return false;
                }
                store_u64le(section->data + offset, value);
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
            uint64_t resolver, stub_va, slot_va, page_delta;
            uint32_t imm;

            if (stub_offset == SIZE_MAX) {
                continue;
            }
            if (slot_offset == SIZE_MAX ||
                !range_ok(stub_offset, 16U, stub->size) ||
                !range_ok(slot_offset, 8U, slot->size) ||
                !range_ok(rela_offset, sizeof(Elf64_Rela), rela->size) ||
                (uint64_t)stub_offset > UINT64_MAX - stub_base ||
                (uint64_t)slot_offset > UINT64_MAX - slot_base ||
                !minild_layout_resolve_symbol(state, layout, i, &resolver)) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-aarch64-static-ifunc-layout\n");
                return false;
            }
            stub_va = stub_base + (uint64_t)stub_offset;
            slot_va = slot_base + (uint64_t)slot_offset;
            page_delta = (slot_va >> 12U) - (stub_va >> 12U);
            if (!minild_nonriscv_signed_fits(page_delta, 21U) ||
                (slot_va & 7U) != 0U) {
                fprintf(state->diagnostics,
                        "minic-ld: aarch64-static-ifunc-plt-overflow\n");
                return false;
            }
            /* ADRP X16, SLOT; LDR X17, [X16,#lo12]; BR X17; NOP. */
            imm = (uint32_t)page_delta & UINT32_C(0x1fffff);
            store_u32le(stub->data + stub_offset,
                        UINT32_C(0x90000010) |
                        ((imm & 3U) << 29U) |
                        (((imm >> 2U) & UINT32_C(0x7ffff)) << 5U));
            store_u32le(stub->data + stub_offset + 4U,
                        UINT32_C(0xf9400211) |
                        (((uint32_t)(slot_va & UINT64_C(0xfff)) >> 3U)
                         << 10U));
            store_u32le(stub->data + stub_offset + 8U, UINT32_C(0xd61f0220));
            store_u32le(stub->data + stub_offset + 12U, UINT32_C(0xd503201f));
            store_u64le(rela->data + rela_offset, slot_va);
            store_u64le(rela->data + rela_offset + 8U,
                        ELF64_R_INFO(0U, R_AARCH64_IRELATIVE));
            store_u64le(rela->data + rela_offset + 16U, resolver);
            rela_offset += sizeof(Elf64_Rela);
        }
    }
    return true;
}

static bool minild_aarch64_synthesize_default_boundaries(
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
                    "minic-ld: aarch64-static-ifunc-rela-span-overflow\n");
            return false;
        }
        return static_define_absolute_if_undefined(
                   state, "__rela_iplt_start", base) &&
               static_define_absolute_if_undefined(
                   state, "__rela_iplt_end", base + (uint64_t)section->size);
    }
    return true;
}

static bool minild_aarch64_static_resolve_target(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdStaticTargetPlan *plan, const MiniLdReloc *reloc,
    uint64_t *target) {
    const MiniLdAarch64StaticGot *got = plan->target_data;
    uint64_t got_base;
    uint64_t slot;
    bool tls_ie = minild_aarch64_static_uses_tls_ie(reloc->type);
    size_t *offsets;

    if (got != NULL && reloc->symbol != SIZE_MAX &&
        reloc->symbol < got->symbol_count &&
        ELF64_ST_TYPE(state->resolver.entries[reloc->symbol].info) ==
            STT_GNU_IFUNC &&
        !minild_aarch64_static_uses_got(reloc->type)) {
        size_t stub_offset = got->ifunc_stub_offsets[reloc->symbol];

        if (stub_offset == SIZE_MAX ||
            got->ifunc_stub_section >= state->work.count ||
            !minild_aarch64_static_ifunc_reference(reloc->type) ||
            layout->work[got->ifunc_stub_section].vaddr >
                UINT64_MAX - stub_offset) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-aarch64-static-ifunc-reference:%u\n",
                    (unsigned)reloc->type);
            return false;
        }
        *target = layout->work[got->ifunc_stub_section].vaddr +
                  (uint64_t)stub_offset + (uint64_t)reloc->addend;
        return true;
    }
    if (!minild_aarch64_static_uses_got(reloc->type) && !tls_ie) {
        if (minild_aarch64_static_uses_tls_le(reloc->type)) {
            uint64_t tls_value;

            if (reloc->symbol == SIZE_MAX ||
                !minild_aarch64_static_tls_offset(
                    state, layout, reloc->symbol, &tls_value)) {
                return false;
            }
            *target = tls_value + (uint64_t)reloc->addend;
        }
        return true;
    }
    if (got == NULL || !got->present || got->section >= state->work.count ||
        reloc->symbol >= got->symbol_count) {
        fprintf(state->diagnostics,
                "minic-ld: missing-aarch64-static-got-entry:%u\n",
                (unsigned)reloc->type);
        return false;
    }
    offsets = tls_ie ? got->tls_slot_offsets : got->slot_offsets;
    if (offsets[reloc->symbol] == SIZE_MAX ||
        layout->work[got->section].vaddr >
            UINT64_MAX - offsets[reloc->symbol]) {
        fprintf(state->diagnostics,
                "minic-ld: missing-aarch64-static-got-entry:%u\n",
                (unsigned)reloc->type);
        return false;
    }
    got_base = layout->work[got->section].vaddr;
    slot = got_base + offsets[reloc->symbol];
    switch (reloc->type) {
    case R_AARCH64_TLSIE_MOVW_GOTTPREL_G1:
    case R_AARCH64_TLSIE_MOVW_GOTTPREL_G0_NC:
        *target = slot - got_base;
        break;
    case R_AARCH64_GOTREL64:
    case R_AARCH64_GOTREL32:
        *target -= got_base; /* S+A-GOT */
        break;
    case R_AARCH64_LD64_GOTOFF_LO15:
        *target = slot - got_base;
        break;
    case R_AARCH64_LD64_GOTPAGE_LO15:
        *target = slot - (got_base & ~UINT64_C(0xfff));
        break;
    case R_AARCH64_GOTPCREL32:
        *target = slot;
        break;
    default:
        *target = slot; /* GOT_LD_PREL19 / ADR_GOT_PAGE / LD64_LO12 */
        break;
    }
    return true;
}
