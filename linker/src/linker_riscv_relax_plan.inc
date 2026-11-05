/* Whole-image RISC-V CALL/ALIGN fixed-point materializer.
 *
 * A RELAX marker permits shrinking an 8-byte AUIPC/JALR CALL to a 4-byte
 * JAL when the final symbol can be reached.  A R_RISCV_ALIGN fragment then
 * consumes only the NOP bytes needed at its final address.  Both operations
 * change *all later* source offsets; compute from immutable source snapshots
 * on every iteration, never by repeatedly deleting from already-shortened
 * instructions.  The owner of the outer layout iteration decides when the
 * final layout has stabilized.
 */
typedef enum MiniLdRiscvRelaxKind {
    MINILD_RV_RELAX_ALIGN,
    MINILD_RV_RELAX_CALL
} MiniLdRiscvRelaxKind;

enum { MINILD_RV_RELAX_ELF_FLAG_RVC = 0x0001U };

typedef struct MiniLdRiscvRelaxSite {
    size_t original_offset;
    size_t original_size;
    size_t final_offset;
    size_t final_size;
    size_t relocation_index;
    MiniLdRiscvRelaxKind kind;
    unsigned jal_rd;
    bool call_eligible;
    /* 8=AUIPC/JALR, 4=JAL, 2=C.J/C.JAL. */
    unsigned call_width;
} MiniLdRiscvRelaxSite;

typedef struct MiniLdRiscvRelaxSection {
    unsigned char *original_bytes;
    size_t original_size;
    MiniLdRiscvRelaxSite *sites;
    size_t site_count;
    size_t site_capacity;
} MiniLdRiscvRelaxSection;

typedef struct MiniLdRiscvRelaxSymbol {
    int section;
    uint64_t value;
    uint64_t size;
    bool input_defined;
} MiniLdRiscvRelaxSymbol;

typedef struct MiniLdRiscvRelaxPlan {
    MiniLdRiscvRelaxSection *sections;
    size_t section_count;
    MiniLdRiscvRelaxSymbol *symbols;
    size_t symbol_count;
    uint64_t *reloc_offsets;
    uint32_t *reloc_types;
    size_t reloc_count;
} MiniLdRiscvRelaxPlan;

typedef struct MiniLdRiscvRelaxMarker {
    size_t section;
    uint64_t offset;
} MiniLdRiscvRelaxMarker;

static int minild_rv_relax_compare_markers(const void *left,
                                           const void *right) {
    const MiniLdRiscvRelaxMarker *a = left;
    const MiniLdRiscvRelaxMarker *b = right;
    if (a->section != b->section) {
        return a->section < b->section ? -1 : 1;
    }
    if (a->offset != b->offset) {
        return a->offset < b->offset ? -1 : 1;
    }
    return 0;
}

static int minild_rv_relax_compare_sites(const void *left,
                                        const void *right) {
    const MiniLdRiscvRelaxSite *a = left;
    const MiniLdRiscvRelaxSite *b = right;
    if (a->original_offset != b->original_offset) {
        return a->original_offset < b->original_offset ? -1 : 1;
    }
    if (a->kind != b->kind) {
        return a->kind < b->kind ? -1 : 1;
    }
    return 0;
}

static void minild_riscv_relax_plan_destroy(MiniLdRiscvRelaxPlan *plan) {
    size_t i;

    if (plan == NULL) {
        return;
    }
    if (plan->sections != NULL) {
        for (i = 0U; i < plan->section_count; ++i) {
            free(plan->sections[i].original_bytes);
            free(plan->sections[i].sites);
        }
    }
    free(plan->sections);
    free(plan->symbols);
    free(plan->reloc_offsets);
    free(plan->reloc_types);
    free(plan);
}

static bool minild_rv_relax_add_site(MiniLdRiscvRelaxSection *section,
                                     MiniLdRiscvRelaxSite site) {
    MiniLdRiscvRelaxSite *grown;
    size_t capacity;

    if (section->site_count == section->site_capacity) {
        if (section->site_capacity > SIZE_MAX / 2U) {
            return false;
        }
        capacity = section->site_capacity == 0U
                       ? 8U : section->site_capacity * 2U;
        if (capacity > SIZE_MAX / sizeof(*grown)) {
            return false;
        }
        grown = realloc(section->sites, capacity * sizeof(*grown));
        if (grown == NULL) {
            return false;
        }
        section->sites = grown;
        section->site_capacity = capacity;
    }
    section->sites[section->site_count++] = site;
    return true;
}

/* The input relocation workspace does not guarantee sorting by offset.
 * A CALL primary relocation is relaxable only if the *same byte offset*
 * carries an explicit R_RISCV_RELAX marker from the assembler. */
static bool minild_rv_relax_has_marker(
    const MiniLdRiscvRelaxMarker *markers, size_t count,
    size_t section_index, uint64_t offset) {
    MiniLdRiscvRelaxMarker query = {section_index, offset};
    return bsearch(&query, markers, count, sizeof(*markers),
                   minild_rv_relax_compare_markers) != NULL;
}

static bool minild_rv_relax_call_encoding(const MiniLdSection *section,
                                           uint64_t offset,
                                           unsigned *rd_out) {
    uint32_t auipc;
    uint32_t jalr;
    unsigned auipc_rd;

    if (offset > SIZE_MAX ||
        !range_ok((size_t)offset, 8U, section->size)) {
        return false;
    }
    auipc = load_u32le(section->data + (size_t)offset);
    jalr = load_u32le(section->data + (size_t)offset + 4U);
    auipc_rd = (auipc >> 7U) & 31U;
    if ((auipc & 0x7fU) != 0x17U ||
        (jalr & 0x707fU) != 0x67U ||
        ((jalr >> 15U) & 31U) != auipc_rd) {
        return false;
    }
    *rd_out = (jalr >> 7U) & 31U;
    return true;
}

static MiniLdRiscvRelaxPlan *minild_rv_relax_capture(MiniLdState *state) {
    MiniLdRiscvRelaxPlan *plan = calloc(1U, sizeof(*plan));
    MiniLdRiscvRelaxMarker *markers = NULL;
    size_t marker_count = 0U;
    size_t i;

    if (plan == NULL) {
        return NULL;
    }
    plan->section_count = state->work.count;
    plan->symbol_count = state->resolver.count;
    plan->reloc_count = state->relocations.count;
    if (plan->reloc_count > SIZE_MAX / sizeof(*plan->reloc_offsets) ||
        plan->reloc_count > SIZE_MAX / sizeof(*plan->reloc_types) ||
        plan->reloc_count > SIZE_MAX / sizeof(*markers)) {
        goto error;
    }
    plan->sections = calloc(plan->section_count ? plan->section_count : 1U,
                            sizeof(*plan->sections));
    plan->symbols = calloc(plan->symbol_count ? plan->symbol_count : 1U,
                           sizeof(*plan->symbols));
    plan->reloc_offsets = malloc((plan->reloc_count ? plan->reloc_count : 1U) *
                                 sizeof(*plan->reloc_offsets));
    plan->reloc_types = malloc((plan->reloc_count ? plan->reloc_count : 1U) *
                               sizeof(*plan->reloc_types));
    if (plan->sections == NULL || plan->symbols == NULL ||
        plan->reloc_offsets == NULL || plan->reloc_types == NULL) {
        goto error;
    }
    markers = malloc((plan->reloc_count ? plan->reloc_count : 1U) *
                     sizeof(*markers));
    if (markers == NULL) {
        goto error;
    }
    for (i = 0U; i < plan->symbol_count; ++i) {
        const MiniLdResolvedSymbol *symbol = &state->resolver.entries[i];
        plan->symbols[i].section = symbol->section;
        plan->symbols[i].value = symbol->value;
        plan->symbols[i].size = symbol->size;
        plan->symbols[i].input_defined = symbol->source_symbol != SIZE_MAX;
    }
    for (i = 0U; i < plan->reloc_count; ++i) {
        const MiniLdReloc *reloc = &state->relocations.entries[i];
        plan->reloc_offsets[i] = reloc->offset;
        plan->reloc_types[i] = reloc->type;
        if (reloc->type == R_RISCV_RELAX) {
            markers[marker_count].section = reloc->section;
            markers[marker_count].offset = reloc->offset;
            ++marker_count;
        }
    }
    qsort(markers, marker_count, sizeof(*markers),
          minild_rv_relax_compare_markers);
    for (i = 0U; i < plan->reloc_count; ++i) {
        const MiniLdReloc *reloc = &state->relocations.entries[i];
        MiniLdRiscvRelaxSection *section;
        MiniLdRiscvRelaxSite site = {0};

        if (reloc->section >= plan->section_count ||
            (reloc->type != R_RISCV_ALIGN &&
             reloc->type != R_RISCV_CALL &&
             reloc->type != R_RISCV_CALL_PLT)) {
            continue;
        }
        section = &plan->sections[reloc->section];
        site.relocation_index = i;
        if (reloc->type == R_RISCV_ALIGN) {
            if (reloc->addend < 0 ||
                (uint64_t)reloc->addend > SIZE_MAX) {
                fprintf(state->diagnostics,
                        "minic-ld: riscv-relax-invalid-align-addend\n");
                goto error;
            }
            site.kind = MINILD_RV_RELAX_ALIGN;
            site.original_size = (size_t)reloc->addend;
        } else {
            site.kind = MINILD_RV_RELAX_CALL;
            site.original_size = 8U;
            site.call_eligible = minild_rv_relax_has_marker(
                markers, marker_count, reloc->section, reloc->offset);
            if (site.call_eligible &&
                !minild_rv_relax_call_encoding(
                    &state->work.sections[reloc->section],
                    reloc->offset, &site.jal_rd)) {
                fprintf(state->diagnostics,
                        "minic-ld: riscv-relax-invalid-call-pair:%s:%llu\n",
                        state->work.sections[reloc->section].name,
                        (unsigned long long)reloc->offset);
                goto error;
            }
        }
        if (reloc->offset > SIZE_MAX) {
            fprintf(state->diagnostics,
                    "minic-ld: riscv-relax-offset-overflow\n");
            goto error;
        }
        site.original_offset = (size_t)reloc->offset;
        site.final_size = site.original_size;
        if (!minild_rv_relax_add_site(section, site)) {
            goto error;
        }
    }
    for (i = 0U; i < plan->section_count; ++i) {
        MiniLdRiscvRelaxSection *entry = &plan->sections[i];
        const MiniLdSection *section = &state->work.sections[i];
        size_t j;
        size_t cursor = 0U;

        if (entry->site_count == 0U) {
            continue;
        }
        if (section->type == SHT_NOBITS) {
            fprintf(state->diagnostics,
                    "minic-ld: riscv-relax-in-nobits:%s\n", section->name);
            goto error;
        }
        qsort(entry->sites, entry->site_count,
              sizeof(*entry->sites), minild_rv_relax_compare_sites);
        entry->original_size = section->size;
        entry->original_bytes = malloc(section->size ? section->size : 1U);
        if (entry->original_bytes == NULL) {
            goto error;
        }
        if (section->size != 0U) {
            memcpy(entry->original_bytes, section->data, section->size);
        }
        for (j = 0U; j < entry->site_count; ++j) {
            MiniLdRiscvRelaxSite *site = &entry->sites[j];
            if (site->original_offset < cursor ||
                !range_ok(site->original_offset, site->original_size,
                          section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: riscv-relax-overlapping-sites:%s:%zu\n",
                        section->name, site->original_offset);
                goto error;
            }
            cursor = site->original_offset + site->original_size;
        }
    }
    free(markers);
    return plan;
error:
    free(markers);
    minild_riscv_relax_plan_destroy(plan);
    return NULL;
}

/* Source-coordinate projection is monotone even for labels inside removed
 * padding: such positions collapse to the last surviving NOP byte boundary.
 * Lookups are logarithmic in the count of relaxation sites. */
static size_t minild_rv_relax_map_offset(
    const MiniLdRiscvRelaxSection *section, size_t original) {
    size_t low = 0U;
    size_t high = section->site_count;
    const MiniLdRiscvRelaxSite *site;
    size_t original_end;
    size_t final_end;

    while (low < high) {
        size_t middle = low + (high - low) / 2U;
        if (section->sites[middle].original_offset <= original) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    if (low == 0U) {
        return original;
    }
    site = &section->sites[low - 1U];
    original_end = site->original_offset + site->original_size;
    final_end = site->final_offset + site->final_size;
    if (original < original_end) {
        size_t inside = original - site->original_offset;
        return site->final_offset +
               (inside <= site->final_size ? inside : site->final_size);
    }
    return original - (original_end - final_end);
}

static bool minild_rv_relax_append_nops(unsigned char *data,
                                       size_t capacity, size_t *cursor,
                                       size_t length) {
    while (length >= 4U) {
        if (!range_ok(*cursor, 4U, capacity)) {
            return false;
        }
        store_u32le(data + *cursor, UINT32_C(0x00000013));
        *cursor += 4U;
        length -= 4U;
    }
    if (length == 2U) {
        if (!range_ok(*cursor, 2U, capacity)) {
            return false;
        }
        data[(*cursor)++] = 1U;
        data[(*cursor)++] = 0U;
        length = 0U;
    }
    return length == 0U;
}

static bool minild_rv_relax_resolve_call_target(
    const MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdReloc *reloc, uint64_t *target_out) {
    const MiniLdResolvedSymbol *symbol;
    uint64_t value;

    if (reloc->symbol >= state->resolver.count) {
        return false;
    }
    symbol = &state->resolver.entries[reloc->symbol];
    if (symbol->section == MINILD_SECTION_ABS) {
        value = symbol->value;
    } else if (symbol->section == MINILD_SECTION_UNDEF) {
        if (ELF64_ST_BIND(symbol->info) != STB_WEAK) {
            return false;
        }
        value = 0U;
    } else if (symbol->section >= 0 &&
               (size_t)symbol->section < state->work.count &&
               (state->work.sections[symbol->section].flags & SHF_ALLOC) != 0 &&
               !state->work.sections[symbol->section].gc_discarded &&
               !layout->work[symbol->section].discarded &&
               symbol->value <=
                   UINT64_MAX - layout->work[symbol->section].vaddr) {
        value = layout->work[symbol->section].vaddr + symbol->value;
    } else {
        return false;
    }
    *target_out = value + (uint64_t)reloc->addend;
    return true;
}

static bool minild_rv_relax_rebuild_section(
    MiniLdState *state, const MiniLdLayout *layout,
    MiniLdRiscvRelaxSection *source, size_t index, bool *changed) {
    MiniLdSection *section = &state->work.sections[index];
    unsigned char *output = malloc(source->original_size ?
                                    source->original_size : 1U);
    size_t cursor = 0U;
    size_t output_size = 0U;
    size_t i;
    bool valid = false;

    if (output == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:riscv-relax\n");
        return false;
    }
    for (i = 0U; i < source->site_count; ++i) {
        MiniLdRiscvRelaxSite *site = &source->sites[i];
        size_t copy_size = site->original_offset - cursor;
        size_t final_size = site->original_size;
        if (!range_ok(output_size, copy_size, source->original_size)) {
            goto done;
        }
        memcpy(output + output_size, source->original_bytes + cursor,
               copy_size);
        output_size += copy_size;
        site->final_offset = output_size;
        if (site->kind == MINILD_RV_RELAX_ALIGN) {
            size_t alignment = 1U;
            size_t bits = site->original_size;
            uint64_t place;
            while (bits != 0U) {
                if (alignment > SIZE_MAX / 2U) {
                    goto done;
                }
                alignment *= 2U;
                bits >>= 1U;
            }
            if (layout->work[index].vaddr >
                UINT64_MAX - (uint64_t)output_size) {
                fprintf(state->diagnostics,
                        "minic-ld: riscv-relax-align-place-overflow:%s\n",
                        section->name);
                goto done;
            }
            place = layout->work[index].vaddr + (uint64_t)output_size;
            final_size = (size_t)((-(uint64_t)place) &
                                  (uint64_t)(alignment - 1U));
            if (final_size > site->original_size ||
                (final_size & 1U) != 0U ||
                !minild_rv_relax_append_nops(
                    output, source->original_size, &output_size,
                    final_size)) {
                fprintf(state->diagnostics,
                        "minic-ld: riscv-relax-align-invalid:%s:%zu\n",
                        section->name, site->original_offset);
                goto done;
            }
        } else if (site->call_width != 8U) {
            /* Final target relocation will encode the selected immediate. */
            final_size = site->call_width;
            if (!range_ok(output_size, final_size,
                          source->original_size)) {
                goto done;
            }
            if (final_size == 2U) {
                uint16_t insn = site->jal_rd == 0U
                                    ? UINT16_C(0xa001) /* C.J */
                                    : UINT16_C(0x2001); /* RV32 C.JAL */
                store_u16le(output + output_size, insn);
            } else {
                store_u32le(output + output_size,
                            UINT32_C(0x6f) |
                                (uint32_t)(site->jal_rd << 7U));
            }
            output_size += final_size;
        } else {
            if (!range_ok(output_size, 8U, source->original_size)) {
                goto done;
            }
            memcpy(output + output_size,
                   source->original_bytes + site->original_offset, 8U);
            output_size += 8U;
        }
        site->final_size = final_size;
        cursor = site->original_offset + site->original_size;
    }
    if (!range_ok(output_size, source->original_size - cursor,
                  source->original_size)) {
        goto done;
    }
    memcpy(output + output_size, source->original_bytes + cursor,
           source->original_size - cursor);
    output_size += source->original_size - cursor;
    if (section->size != output_size ||
        (output_size != 0U &&
         memcmp(section->data, output, output_size) != 0)) {
        *changed = true;
    }
    if (output_size != 0U) {
        memcpy(section->data, output, output_size);
    }
    section->size = output_size;
    valid = true;
done:
    if (!valid) {
        fprintf(state->diagnostics,
                "minic-ld: riscv-relax-output-overflow:%s\n",
                section->name);
    }
    free(output);
    return valid;
}

typedef bool (*MiniLdRiscvRelaxCallPolicy)(const MiniLdReloc *, void *);

/* ET_EXEC and ET_DYN use *identical* source snapshots, deleted-byte events
 * and projection.  Shared calls needing a PLT remain full AUIPC/JALR pairs:
 * that PLT destination is owned by finalization, not the resolved symbol. */
static bool minild_riscv_replan_image_layout(
    MiniLdState *state, const MiniLdLayout *layout,
    MiniLdRiscvRelaxPlan **plan_slot, bool *changed,
    MiniLdRiscvRelaxCallPolicy call_policy, void *call_policy_data) {
    MiniLdRiscvRelaxPlan *plan;
    size_t i;

    if (plan_slot == NULL || changed == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: riscv-relax-missing-target-plan\n");
        return false;
    }
    *changed = false;
    if (*plan_slot == NULL) {
        *plan_slot = minild_rv_relax_capture(state);
        if (*plan_slot == NULL) {
            fprintf(state->diagnostics,
                    "minic-ld: riscv-relax-capture-failed\n");
            return false;
        }
    }
    plan = *plan_slot;

    /* Scan on the *current complete layout*; the outer coordinator keeps
     * refreshing VMA and script-defined symbols until no deletion changes
     * any section size or any CALL reachability decision. */
    for (i = 0U; i < plan->section_count; ++i) {
        MiniLdRiscvRelaxSection *section = &plan->sections[i];
        size_t j;
        if (section->site_count == 0U) {
            continue;
        }
        for (j = 0U; j < section->site_count; ++j) {
            MiniLdRiscvRelaxSite *site = &section->sites[j];
            const MiniLdReloc *reloc;
            uint64_t target;
            uint64_t place;
            int64_t displacement;
            int64_t jal_displacement;
            int64_t rvc_displacement;
            bool forward_same_section;
            if (site->kind != MINILD_RV_RELAX_CALL) {
                continue;
            }
            reloc = &state->relocations.entries[site->relocation_index];
            site->call_width = 8U;
            if (!site->call_eligible ||
                (call_policy != NULL &&
                 !call_policy(reloc, call_policy_data)) ||
                (layout->scripted && layout->work[i].discarded) ||
                reloc->offset > UINT64_MAX - layout->work[i].vaddr ||
                !minild_rv_relax_resolve_call_target(
                    state, layout, reloc, &target)) {
                continue;
            }
            place = layout->work[i].vaddr + reloc->offset;
            displacement = (int64_t)(target - place);
            forward_same_section =
                reloc->symbol < state->resolver.count &&
                state->resolver.entries[reloc->symbol].section == (int)i &&
                displacement > 0;
            jal_displacement = displacement;
            rvc_displacement = displacement;
            /* A forward target in the *same* input section moves as this
             * very CALL shortens.  Without accounting for its own 4B
             * deletion, an initial distance of +1,048,576 can never enter
             * the fixed point even though the final JAL reaches.  For a
             * cross-section target, alignment may mask this change, so
             * defer that decision until some other re-layout moves it. */
            if (forward_same_section) {
                if (site->final_size == 8U) {
                    jal_displacement -= 4;
                    rvc_displacement -= 6;
                } else if (site->final_size == 4U) {
                    rvc_displacement -= 2;
                }
            }
            if ((jal_displacement & 1) == 0 &&
                jal_displacement >= -1048576 &&
                jal_displacement <= 1048574) {
                site->call_width = 4U;
                /* ELF e_flags carries the link unit's RVC capability;
                 * RV32 supports C.JAL, while RV64 reserves its opcode
                 * for C.ADDIW.  C.J (rd=x0) works on both XLENs. */
                if ((state->elf_flags & MINILD_RV_RELAX_ELF_FLAG_RVC) != 0U &&
                    (site->jal_rd == 0U ||
                     (site->jal_rd == 1U &&
                      state->target->elf_class == ELFCLASS32)) &&
                    rvc_displacement >= -2048 &&
                    rvc_displacement <= 2046) {
                    site->call_width = 2U;
                }
            }
        }
    }
    for (i = 0U; i < plan->section_count; ++i) {
        MiniLdRiscvRelaxSection *section = &plan->sections[i];
        if (section->site_count != 0U &&
            !minild_rv_relax_rebuild_section(
                state, layout, section, i, changed)) {
            return false;
        }
    }

    /* Resolved symbols own local offsets and st_size, never source input
     * symbols.  Mapping the end boundary as well as the start preserves
     * function extents spanning one or more shortened CALL/ALIGN fragments. */
    for (i = 0U; i < plan->symbol_count; ++i) {
        const MiniLdRiscvRelaxSymbol *original = &plan->symbols[i];
        MiniLdResolvedSymbol *symbol = &state->resolver.entries[i];
        const MiniLdRiscvRelaxSection *section;
        size_t begin;
        size_t end;
        if (!original->input_defined || original->section < 0 ||
            (size_t)original->section >= plan->section_count ||
            original->value > SIZE_MAX) {
            continue;
        }
        section = &plan->sections[original->section];
        if (section->site_count == 0U) {
            continue;
        }
        begin = minild_rv_relax_map_offset(section,
                                             (size_t)original->value);
        symbol->value = (uint64_t)begin;
        if (original->size <= SIZE_MAX - (size_t)original->value) {
            end = minild_rv_relax_map_offset(
                section, (size_t)original->value + (size_t)original->size);
            symbol->size = end >= begin ? (uint64_t)(end - begin) : 0U;
        }
    }
    for (i = 0U; i < plan->reloc_count; ++i) {
        MiniLdReloc *reloc = &state->relocations.entries[i];
        const MiniLdRiscvRelaxSection *section;
        reloc->type = plan->reloc_types[i];
        reloc->offset = plan->reloc_offsets[i];
        if (reloc->section >= plan->section_count ||
            plan->reloc_offsets[i] > SIZE_MAX) {
            continue;
        }
        section = &plan->sections[reloc->section];
        if (section->site_count == 0U) {
            continue;
        }
        reloc->offset = (uint64_t)minild_rv_relax_map_offset(
            section, (size_t)plan->reloc_offsets[i]);
    }
    for (i = 0U; i < plan->section_count; ++i) {
        const MiniLdRiscvRelaxSection *section = &plan->sections[i];
        size_t j;
        for (j = 0U; j < section->site_count; ++j) {
            const MiniLdRiscvRelaxSite *site = &section->sites[j];
            if (site->kind == MINILD_RV_RELAX_CALL &&
                site->call_width != 8U) {
                state->relocations.entries[site->relocation_index].type =
                    site->call_width == 2U
                        ? R_RISCV_RVC_JUMP : R_RISCV_JAL;
            }
        }
    }
    return true;
}

static bool minild_riscv_replan_static_layout(
    MiniLdState *state, const MiniLdLayout *layout,
    MiniLdStaticTargetPlan *target_plan, bool *changed) {
    MiniLdRiscvStaticGot *got = target_plan->target_data;

    if (got == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: riscv-relax-missing-static-GOT-plan\n");
        return false;
    }
    return minild_riscv_replan_image_layout(
        state, layout, &got->relax, changed, NULL, NULL);
}
