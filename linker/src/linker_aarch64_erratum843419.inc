/* Cortex-A53 erratum 843419 (r0p0/r0p1/r0p2/r0p4): an ADRP at page
 * offset 0xff8/0xffc followed by a particular load/store sequence can
 * misexecute on the affected cores.  The decoder and mapping-symbol rules
 * follow LLVM lld's AArch64ErrataFix.cpp (Apache-2.0 WITH LLVM-exception).
 *
 * This target-only phase runs AFTER addresses have been assigned, but BEFORE
 * relocations are applied.  It extends the affected executable work fragment
 * with an 8-byte island: the original unsigned-immediate load/store followed
 * by an unconditional branch back.  The original instruction is replaced by
 * a branch to the island only after the ordinary relocation pass has written
 * the relocated instruction into its new home.  Layout must be recomputed
 * whenever this phase appends an island, until it reports no change.
 *
 * A work-fragment-local island is deliberately chosen over an unselected
 * synthetic .text.patch: the former remains in the very same output-script
 * command as its owner and preserves the linker script's section policy.
 * A >128MiB branch span fails closed rather than emitting a broken image. */

static bool minild_aarch64_843419_is_adrp(uint32_t insn) {
    return (insn & UINT32_C(0x9f000000)) == UINT32_C(0x90000000);
}

static bool minild_aarch64_843419_is_loadstore(uint32_t insn) {
    return (insn & UINT32_C(0x0a000000)) == UINT32_C(0x08000000);
}

static bool minild_aarch64_843419_is_exclusive(uint32_t insn) {
    return (insn & UINT32_C(0x3f000000)) == UINT32_C(0x08000000);
}

static bool minild_aarch64_843419_is_literal(uint32_t insn) {
    return (insn & UINT32_C(0x3b000000)) == UINT32_C(0x18000000);
}

static bool minild_aarch64_843419_is_unsigned(uint32_t insn) {
    return (insn & UINT32_C(0x3b000000)) == UINT32_C(0x39000000);
}

static bool minild_aarch64_843419_is_single(uint32_t insn) {
    return (insn & UINT32_C(0x3b000c00)) == UINT32_C(0x38000000) ||
           (insn & UINT32_C(0x3b200c00)) == UINT32_C(0x38000400) ||
           (insn & UINT32_C(0x3b200c00)) == UINT32_C(0x38000800) ||
           (insn & UINT32_C(0x3b200c00)) == UINT32_C(0x38000c00) ||
           (insn & UINT32_C(0x3b200c00)) == UINT32_C(0x38200800) ||
           minild_aarch64_843419_is_unsigned(insn);
}

static bool minild_aarch64_843419_is_st1_opcode(uint32_t insn) {
    uint32_t opcode = insn & UINT32_C(0x0000f000);
    return opcode == UINT32_C(0x00002000) ||
           opcode == UINT32_C(0x00006000) ||
           opcode == UINT32_C(0x00007000) ||
           opcode == UINT32_C(0x0000a000);
}

static bool minild_aarch64_843419_is_st1_single_opcode(uint32_t insn) {
    uint32_t opcode = insn & UINT32_C(0x0040e000);
    return opcode == 0U || opcode == UINT32_C(0x00004000) ||
           opcode == UINT32_C(0x00008000);
}

static bool minild_aarch64_843419_is_st1(uint32_t insn) {
    return (((insn & UINT32_C(0xbfff0000)) == UINT32_C(0x0c000000) ||
             (insn & UINT32_C(0xbfe00000)) == UINT32_C(0x0c800000)) &&
            minild_aarch64_843419_is_st1_opcode(insn)) ||
           (((insn & UINT32_C(0xbfff0000)) == UINT32_C(0x0d000000) ||
             (insn & UINT32_C(0xbfe00000)) == UINT32_C(0x0d800000)) &&
            minild_aarch64_843419_is_st1_single_opcode(insn));
}

static bool minild_aarch64_843419_is_stp(uint32_t insn) {
    uint32_t opcode = insn & UINT32_C(0x3bc00000);
    return opcode == UINT32_C(0x28000000) ||
           opcode == UINT32_C(0x28800000) ||
           opcode == UINT32_C(0x29000000) ||
           opcode == UINT32_C(0x29800000);
}

static bool minild_aarch64_843419_has_writeback(uint32_t insn) {
    uint32_t pair = insn & UINT32_C(0x3bc00000);
    uint32_t single = insn & UINT32_C(0x3b200c00);
    return pair == UINT32_C(0x28800000) ||
           pair == UINT32_C(0x29800000) ||
           single == UINT32_C(0x38000400) ||
           single == UINT32_C(0x38000c00) ||
           ((insn & UINT32_C(0xbfe00000)) == UINT32_C(0x0c800000) &&
            minild_aarch64_843419_is_st1_opcode(insn)) ||
           ((insn & UINT32_C(0xbfe00000)) == UINT32_C(0x0d800000) &&
            minild_aarch64_843419_is_st1_single_opcode(insn));
}

static bool minild_aarch64_843419_is_load(uint32_t insn) {
    if ((insn & UINT32_C(0x3f400000)) == UINT32_C(0x08400000) ||
        minild_aarch64_843419_is_literal(insn)) {
        return true;
    }
    if (minild_aarch64_843419_is_single(insn)) {
        uint32_t size = (insn >> 30U) & 3U;
        uint32_t vector = (insn >> 26U) & 1U;
        uint32_t opcode = (insn >> 22U) & 3U;
        return opcode != 0U &&
               !(size == 0U && vector != 0U && opcode == 2U) &&
               !(size == 3U && vector == 0U && opcode == 2U);
    }
    return false;
}

static bool minild_aarch64_843419_is_branch(uint32_t insn) {
    return (insn & UINT32_C(0xfe000000)) == UINT32_C(0xd6000000) ||
           (insn & UINT32_C(0xfe000000)) == UINT32_C(0x54000000) ||
           (insn & UINT32_C(0x7c000000)) == UINT32_C(0x14000000) ||
           (insn & UINT32_C(0x7c000000)) == UINT32_C(0x34000000);
}

static bool minild_aarch64_843419_sequence(uint32_t adrp, uint32_t second,
                                           uint32_t last) {
    uint32_t reg;

    if (!minild_aarch64_843419_is_adrp(adrp) ||
        !minild_aarch64_843419_is_loadstore(second)) {
        return false;
    }
    reg = adrp & 31U;
    if (!minild_aarch64_843419_is_exclusive(second) &&
        !minild_aarch64_843419_is_literal(second) &&
        !minild_aarch64_843419_is_single(second) &&
        !minild_aarch64_843419_is_stp(second) &&
        !minild_aarch64_843419_is_st1(second)) {
        return false;
    }
    if ((minild_aarch64_843419_is_load(second) &&
         (second & 31U) == reg) ||
        (minild_aarch64_843419_has_writeback(second) &&
         ((second >> 5U) & 31U) == reg)) {
        return false;
    }
    return minild_aarch64_843419_is_unsigned(last) &&
           ((last >> 5U) & 31U) == reg;
}

static bool minild_aarch64_843419_mapping(const char *name, bool *code) {
    if (name[0] != '$' || (name[1] != 'x' && name[1] != 'd') ||
        (name[2] != '\0' && name[2] != '.')) {
        return false;
    }
    *code = name[1] == 'x';
    return true;
}

static int minild_aarch64_843419_compare_maps(const void *a, const void *b) {
    const MiniLdAarch64CodeMap *lhs = a, *rhs = b;

    if (lhs->section != rhs->section) {
        return lhs->section < rhs->section ? -1 : 1;
    }
    if (lhs->offset != rhs->offset) {
        return lhs->offset < rhs->offset ? -1 : 1;
    }
    if (lhs->code != rhs->code) {
        /* At a tied address the data mapping conservatively wins. */
        return lhs->code ? -1 : 1;
    }
    return 0;
}

static bool minild_aarch64_843419_prepare(MiniLdState *state,
                                           MiniLdAarch64ErratumPlan *errata) {
    size_t i, count = 0U;
    const size_t canonical = state->static_canonical_work_count;

    if (canonical > state->work.count) {
        fprintf(state->diagnostics,
                "minic-ld: aarch64-843419-lost-source-work-sections\n");
        return false;
    }

    if (errata->initialized) {
        if (canonical != errata->original_section_count) {
            fprintf(state->diagnostics,
                    "minic-ld: aarch64-843419-work-section-set-changed\n");
            return false;
        }
        return true;
    }
    if (canonical > SIZE_MAX / sizeof(*errata->original_sizes) ||
        state->resolver.count > SIZE_MAX / sizeof(*errata->code_map)) {
        return false;
    }
    errata->original_sizes = malloc((canonical == 0U ? 1U : canonical) *
                                    sizeof(*errata->original_sizes));
    errata->input_owned = calloc(canonical == 0U ? 1U : canonical, 1U);
    errata->code_map = malloc((state->resolver.count == 0U ? 1U :
                               state->resolver.count) *
                              sizeof(*errata->code_map));
    if (errata->original_sizes == NULL || errata->input_owned == NULL ||
        errata->code_map == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:aarch64-843419-mapping\n");
        return false;
    }
    errata->original_section_count = canonical;
    for (i = 0U; i < canonical; ++i) {
        errata->original_sizes[i] = state->work.sections[i].size;
    }
    for (i = 0U; i < state->work.placement_count; ++i) {
        size_t owner = state->work.placements[i].work_section;
        if (owner != SIZE_MAX && owner < canonical) {
            errata->input_owned[owner] = 1U;
        }
    }
    for (i = 0U; i < state->resolver.count; ++i) {
        const MiniLdResolvedSymbol *sym = &state->resolver.entries[i];
        size_t section;
        bool code;

        if (sym->section < 0 ||
            ELF64_ST_BIND(sym->info) != STB_LOCAL ||
            !minild_aarch64_843419_mapping(sym->name, &code)) {
            continue;
        }
        section = (size_t)sym->section;
        if (section >= canonical) {
            /* A script-generated tail is not stable across fixed-point
             * rounds and does not contain original machine-code mappings. */
            continue;
        }
        /* $d is emitted in ordinary .data too, but only executable input
         * fragments participate in the erratum code/data interval map. */
        if ((state->work.sections[section].flags & SHF_EXECINSTR) == 0U) {
            continue;
        }
        if (sym->value > errata->original_sizes[section]) {
            fprintf(state->diagnostics,
                    "minic-ld: aarch64-843419-invalid-mapping-symbol:%s\n",
                    sym->name);
            return false;
        }
        errata->code_map[count++] = (MiniLdAarch64CodeMap){
            section, sym->value, code
        };
    }
    qsort(errata->code_map, count, sizeof(*errata->code_map),
          minild_aarch64_843419_compare_maps);
    errata->code_map_count = count;
    errata->initialized = true;
    return true;
}

static bool minild_aarch64_843419_patch_known(
    const MiniLdAarch64ErratumPlan *errata, size_t section, size_t offset) {
    size_t i;
    for (i = 0U; i < errata->count; ++i) {
        if (errata->patches[i].section == section &&
            errata->patches[i].instruction_offset == offset) {
            return true;
        }
    }
    return false;
}

static bool minild_aarch64_843419_relocation_movable(uint32_t type) {
    switch (type) {
    case R_AARCH64_LDST8_ABS_LO12_NC:
    case R_AARCH64_LDST16_ABS_LO12_NC:
    case R_AARCH64_LDST32_ABS_LO12_NC:
    case R_AARCH64_LDST64_ABS_LO12_NC:
    case R_AARCH64_LDST128_ABS_LO12_NC:
    case R_AARCH64_LD64_GOT_LO12_NC:
    case R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC:
        return true;
    default:
        return false;
    }
}

static bool minild_aarch64_843419_add_patch(
    MiniLdState *state, MiniLdAarch64ErratumPlan *errata,
    size_t section_index, size_t instruction_offset) {
    MiniLdSection *section = &state->work.sections[section_index];
    MiniLdAarch64ErratumPatch *next;
    size_t reloc_index = SIZE_MAX, stub_offset, i;
    uint32_t instruction;

    if (minild_aarch64_843419_patch_known(errata, section_index,
                                          instruction_offset)) {
        return true;
    }
    if (!range_ok(instruction_offset, 4U,
                  errata->original_sizes[section_index]) ||
        section->type != SHT_PROGBITS || section->data == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: aarch64-843419-invalid-patch-site\n");
        return false;
    }
    for (i = 0U; i < state->relocations.count; ++i) {
        MiniLdReloc *reloc = &state->relocations.entries[i];
        if (reloc->section != section_index ||
            reloc->offset != (uint64_t)instruction_offset) {
            continue;
        }
        if (reloc_index != SIZE_MAX ||
            !minild_aarch64_843419_relocation_movable(reloc->type)) {
            fprintf(state->diagnostics,
                    "minic-ld: aarch64-843419-unmovable-relocation:%u\n",
                    (unsigned)reloc->type);
            return false;
        }
        reloc_index = i;
    }
    if (errata->count == errata->capacity) {
        size_t capacity = errata->capacity == 0U ? 8U :
                          errata->capacity * 2U;
        if (capacity < errata->capacity ||
            capacity > SIZE_MAX / sizeof(*errata->patches)) {
            return false;
        }
        next = realloc(errata->patches, capacity * sizeof(*next));
        if (next == NULL) {
            fprintf(state->diagnostics,
                    "minic-ld: out-of-memory:aarch64-843419-patches\n");
            return false;
        }
        errata->patches = next;
        errata->capacity = capacity;
    }
    if (!align_up_size(section->size, 4U, &stub_offset)) {
        fprintf(state->diagnostics,
                "minic-ld: aarch64-843419-stub-alignment-overflow\n");
        return false;
    }
    instruction = load_u32le(section->data + instruction_offset);
    if (!section_append_zero(section, stub_offset - section->size) ||
        !section_append_zero(section, 8U)) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:aarch64-843419-stub\n");
        return false;
    }
    store_u32le(section->data + stub_offset, instruction);
    store_u32le(section->data + stub_offset + 4U, UINT32_C(0x14000000));
    if (reloc_index != SIZE_MAX) {
        /* The work coordinate is mutable; input_offset remains the immutable
         * source coordinate, and GOT/IFUNC reservation is already complete. */
        state->relocations.entries[reloc_index].offset = stub_offset;
    }
    errata->patches[errata->count++] = (MiniLdAarch64ErratumPatch){
        section_index, instruction_offset, stub_offset, instruction
    };
    return true;
}

static bool minild_aarch64_843419_scan_code_range(
    MiniLdState *state, const MiniLdLayout *layout,
    MiniLdAarch64ErratumPlan *errata, size_t section_index,
    uint64_t start, uint64_t end, bool *changed) {
    const MiniLdSection *section = &state->work.sections[section_index];
    uint64_t base = layout->work[section_index].vaddr;
    uint64_t off = start;

    if (end > errata->original_sizes[section_index] || start > end ||
        ((base + start) & UINT64_C(3)) != 0U ||
        (base > UINT64_MAX - end)) {
        fprintf(state->diagnostics,
                "minic-ld: aarch64-843419-invalid-code-range\n");
        return false;
    }
    while (off < end) {
        uint64_t page_offset = (base + off) & UINT64_C(0xfff);
        uint32_t first, second, third;
        uint64_t patch_at = UINT64_MAX;

        if (page_offset < UINT64_C(0xff8)) {
            off += UINT64_C(0xff8) - page_offset;
        }
        if (off >= end || end - off < 12U) {
            break;
        }
        first = load_u32le(section->data + (size_t)off);
        second = load_u32le(section->data + (size_t)off + 4U);
        third = load_u32le(section->data + (size_t)off + 8U);
        if (minild_aarch64_843419_sequence(first, second, third)) {
            patch_at = off + 8U;
        } else if (end - off >= 16U &&
                   !minild_aarch64_843419_is_branch(third) &&
                   minild_aarch64_843419_sequence(
                       first, second,
                       load_u32le(section->data + (size_t)off + 12U))) {
            patch_at = off + 12U;
        }
        if (patch_at != UINT64_MAX &&
            !minild_aarch64_843419_patch_known(
                errata, section_index, (size_t)patch_at)) {
            if (!minild_aarch64_843419_add_patch(state, errata,
                                                 section_index,
                                                 (size_t)patch_at)) {
                return false;
            }
            *changed = true;
        }
        off += ((base + off) & UINT64_C(0xfff)) == UINT64_C(0xff8)
                   ? 4U : UINT64_C(0xffc);
    }
    return true;
}

/* Invoked by Core's shared target post-layout fixed-point coordinator. */
static bool minild_aarch64_replan_static_layout(
    MiniLdState *state, const MiniLdLayout *layout,
    MiniLdStaticTargetPlan *plan, bool *changed) {
    MiniLdAarch64StaticGot *got = plan == NULL ? NULL : plan->target_data;
    MiniLdAarch64ErratumPlan *errata;
    size_t map_index = 0U, section_index;

    if (changed == NULL || layout == NULL || layout->work == NULL ||
        got == NULL) {
        return false;
    }
    *changed = false;
    if (!state->fix_cortex_a53_843419) {
        return true;
    }
    errata = &got->erratum843419;
    if (!minild_aarch64_843419_prepare(state, errata)) {
        return false;
    }
    for (section_index = 0U;
         section_index < errata->original_section_count;
         ++section_index) {
        const MiniLdSection *section = &state->work.sections[section_index];
        size_t first, last, m;
        uint64_t code_start = 0U;
        bool in_code = false;

        while (map_index < errata->code_map_count &&
               errata->code_map[map_index].section < section_index) {
            ++map_index;
        }
        first = map_index;
        while (map_index < errata->code_map_count &&
               errata->code_map[map_index].section == section_index) {
            ++map_index;
        }
        last = map_index;
        if ((section->flags & (SHF_ALLOC | SHF_EXECINSTR)) !=
                (SHF_ALLOC | SHF_EXECINSTR) ||
            section->gc_discarded ||
            (layout->scripted && layout->work[section_index].discarded) ||
            errata->original_sizes[section_index] == 0U) {
            continue;
        }
        if (first == last) {
            /* Target-created executable fragments need no mapping symbols.
             * An input executable with unknown code-vs-data boundaries
             * cannot honestly be certified free of the A53 hazard. */
            if (errata->input_owned[section_index] != 0U) {
                fprintf(state->diagnostics,
                        "minic-ld: aarch64-843419-missing-code-mapping:%s\n",
                        section->input_name != NULL ?
                            section->input_name : section->name);
                return false;
            }
            continue;
        }
        for (m = first; m < last; ++m) {
            const MiniLdAarch64CodeMap *mapping = &errata->code_map[m];
            if (mapping->code && !in_code) {
                code_start = mapping->offset;
                in_code = true;
            } else if (!mapping->code && in_code) {
                if (!minild_aarch64_843419_scan_code_range(
                        state, layout, errata, section_index, code_start,
                        mapping->offset, changed)) {
                    return false;
                }
                in_code = false;
            }
        }
        if (in_code && !minild_aarch64_843419_scan_code_range(
                state, layout, errata, section_index, code_start,
                errata->original_sizes[section_index], changed)) {
            return false;
        }
    }
    return true;
}

static bool minild_aarch64_843419_emit_branches(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdStaticTargetPlan *plan) {
    const MiniLdAarch64StaticGot *got = plan->target_data;
    const MiniLdAarch64ErratumPlan *errata;
    size_t i;

    if (!state->fix_cortex_a53_843419 || got == NULL) {
        return true;
    }
    errata = &got->erratum843419;
    for (i = 0U; i < errata->count; ++i) {
        const MiniLdAarch64ErratumPatch *patch = &errata->patches[i];
        MiniLdSection *section;
        uint64_t source_va, stub_va, delta_to_stub, delta_to_return;

        if (patch->section >= state->work.count) {
            return false;
        }
        section = &state->work.sections[patch->section];
        if (!range_ok(patch->instruction_offset, 4U, section->size) ||
            !range_ok(patch->stub_offset, 8U, section->size) ||
            layout->work[patch->section].vaddr >
                UINT64_MAX - patch->stub_offset - 8U) {
            fprintf(state->diagnostics,
                    "minic-ld: aarch64-843419-invalid-stub-layout\n");
            return false;
        }
        source_va = layout->work[patch->section].vaddr +
                    patch->instruction_offset;
        stub_va = layout->work[patch->section].vaddr + patch->stub_offset;
        delta_to_stub = stub_va - source_va;
        delta_to_return = (source_va + 4U) - (stub_va + 4U);
        if ((delta_to_stub & 3U) != 0U ||
            (delta_to_return & 3U) != 0U ||
            !minild_nonriscv_signed_fits(delta_to_stub, 28U) ||
            !minild_nonriscv_signed_fits(delta_to_return, 28U)) {
            fprintf(state->diagnostics,
                    "minic-ld: aarch64-843419-stub-branch-out-of-range\n");
            return false;
        }
        /* The instruction in the island has already received its original
         * relocation, if any, in minild_nonriscv_apply_with_ops(). */
        store_u32le(section->data + patch->instruction_offset,
                    UINT32_C(0x14000000) |
                    (((uint32_t)(delta_to_stub >> 2U)) & UINT32_C(0x03ffffff)));
        store_u32le(section->data + patch->stub_offset + 4U,
                    UINT32_C(0x14000000) |
                    (((uint32_t)(delta_to_return >> 2U)) & UINT32_C(0x03ffffff)));
        if (i == 0U ||
            errata->patches[i - 1U].section != patch->section) {
            size_t mapping_symbol;

            /* This island is executable even if the original section ended
             * with a literal pool ($d).  Preserve AAELF64 mapping accuracy
             * in the final .symtab, as lld's Patch843419Section does. */
            if (patch->section > INT_MAX ||
                !symbol_resolver_add_local(
                    &state->resolver, state->diagnostics, "$x",
                    (int)patch->section, patch->stub_offset, 0U,
                    ELF64_ST_INFO(STB_LOCAL, STT_NOTYPE), 0U, SIZE_MAX,
                    &mapping_symbol)) {
                fprintf(state->diagnostics,
                        "minic-ld: aarch64-843419-map-stub-failed\n");
                return false;
            }
        }
    }
    return true;
}
