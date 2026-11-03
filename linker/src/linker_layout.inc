typedef struct MiniLdPcrelSlot {
    size_t section_plus_one;
    uint64_t offset;
    int64_t delta;
} MiniLdPcrelSlot;

typedef struct MiniLdScriptBytePatch {
    size_t command;
    uint64_t address;
    unsigned char value;
} MiniLdScriptBytePatch;

typedef struct MiniLdLayout {
    uint64_t *section_vaddr;
    uint64_t *section_laddr;
    size_t *section_file_offset;
    bool *section_discarded;
    /* Script-command identity survives layout: final ELF output sections
     * own working fragments, not the other way around. SIZE_MAX = none. */
    size_t *section_owner;
    unsigned char *script_fill;
    bool *script_have_fill;
    size_t script_command_count;
    uint64_t *script_output_start;
    uint64_t *script_output_end;
    size_t *script_output_file_base;
    MiniLdScriptBytePatch *script_bytes;
    size_t script_byte_count;
    size_t script_byte_capacity;
    size_t rx_file_offset;
    uint64_t rx_vaddr;
    size_t rx_file_size;
    size_t rw_file_offset;
    uint64_t rw_vaddr;
    size_t rw_file_size;
    size_t rw_mem_size;
    bool have_rx;
    bool have_rw;
    bool scripted;
} MiniLdLayout;

typedef struct MiniLdAlignSite {
    size_t offset;
    size_t max_padding;
} MiniLdAlignSite;

typedef struct MiniLdRelaxEvent {
    size_t raw_end;
    size_t cumulative_deleted;
} MiniLdRelaxEvent;

typedef struct MiniLdStaticGot {
    size_t section;
    size_t *slot_offsets;
    size_t symbol_count;
    bool present;
} MiniLdStaticGot;

static uint8_t load_u8(const unsigned char *data) {
    return data[0];
}

static void store_u8(unsigned char *data, uint8_t value) {
    data[0] = value;
}

static uint16_t load_u16le(const unsigned char *data) {
    return (uint16_t)((uint16_t)data[0] |
                      ((uint16_t)data[1] << 8U));
}

static void store_u16le(unsigned char *data, uint16_t value) {
    data[0] = (unsigned char)(value & UINT16_C(0xff));
    data[1] = (unsigned char)((value >> 8U) & UINT16_C(0xff));
}

static uint32_t load_u32le(const unsigned char *data) {
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

static void store_u32le(unsigned char *data, uint32_t value) {
    data[0] = (unsigned char)(value & 0xffU);
    data[1] = (unsigned char)((value >> 8U) & 0xffU);
    data[2] = (unsigned char)((value >> 16U) & 0xffU);
    data[3] = (unsigned char)((value >> 24U) & 0xffU);
}

static uint64_t load_u64le(const unsigned char *data) {
    uint64_t value = 0U;
    size_t i;

    for (i = 0U; i < 8U; ++i) {
        value |= (uint64_t)data[i] << (i * 8U);
    }
    return value;
}

static void store_u64le(unsigned char *data, uint64_t value) {
    size_t i;

    for (i = 0U; i < 8U; ++i) {
        data[i] = (unsigned char)((value >> (i * 8U)) & 0xffU);
    }
}

static int64_t riscv_hi20(int64_t value) {
    return (value + INT64_C(0x800)) >> 12;
}

static int64_t riscv_lo12(int64_t value) {
    int64_t high = riscv_hi20(value);
    return value - (high << 12);
}

static bool static_patch_utype(MiniLdSection *section,
                               uint64_t offset,
                               int64_t imm20,
                               FILE *diagnostics) {
    uint32_t instruction;

    if (section->type == SHT_NOBITS || offset > SIZE_MAX ||
        !range_ok((size_t)offset, 4U, section->size)) {
        fprintf(diagnostics, "minic-ld: relocation-offset-out-of-range\n");
        return false;
    }
    instruction = load_u32le(section->data + (size_t)offset);
    instruction &= UINT32_C(0x00000fff);
    instruction |= ((uint32_t)imm20 & UINT32_C(0x000fffff)) << 12U;
    store_u32le(section->data + (size_t)offset, instruction);
    return true;
}

static bool static_patch_itype(MiniLdSection *section,
                               uint64_t offset,
                               int64_t imm12,
                               FILE *diagnostics) {
    uint32_t instruction;

    if (section->type == SHT_NOBITS || offset > SIZE_MAX ||
        !range_ok((size_t)offset, 4U, section->size)) {
        fprintf(diagnostics, "minic-ld: relocation-offset-out-of-range\n");
        return false;
    }
    instruction = load_u32le(section->data + (size_t)offset);
    instruction &= UINT32_C(0x000fffff);
    instruction |= ((uint32_t)imm12 & UINT32_C(0x00000fff)) << 20U;
    store_u32le(section->data + (size_t)offset, instruction);
    return true;
}

static bool static_patch_stype(MiniLdSection *section,
                               uint64_t offset,
                               int64_t imm12,
                               FILE *diagnostics) {
    uint32_t instruction;
    uint32_t encoded = (uint32_t)imm12 & UINT32_C(0x00000fff);

    if (section->type == SHT_NOBITS || offset > SIZE_MAX ||
        !range_ok((size_t)offset, 4U, section->size)) {
        fprintf(diagnostics, "minic-ld: relocation-offset-out-of-range\n");
        return false;
    }
    instruction = load_u32le(section->data + (size_t)offset);
    instruction &= ~UINT32_C(0xfe000f80);
    instruction |= ((encoded >> 5U) & UINT32_C(0x7f)) << 25U;
    instruction |= (encoded & UINT32_C(0x1f)) << 7U;
    store_u32le(section->data + (size_t)offset, instruction);
    return true;
}

static bool static_patch_branch(MiniLdSection *section,
                                uint64_t offset,
                                int64_t delta,
                                FILE *diagnostics) {
    uint32_t instruction;
    uint32_t encoded;

    if ((delta & 1) != 0 || delta < -4096 || delta > 4094) {
        fprintf(diagnostics,
                "minic-ld: R_RISCV_BRANCH-overflow:delta=%lld\n",
                (long long)delta);
        return false;
    }
    if (section->type == SHT_NOBITS || offset > SIZE_MAX ||
        !range_ok((size_t)offset, 4U, section->size)) {
        fprintf(diagnostics, "minic-ld: relocation-offset-out-of-range\n");
        return false;
    }
    encoded = (uint32_t)delta & UINT32_C(0x1fff);
    instruction = load_u32le(section->data + (size_t)offset);
    instruction &= ~UINT32_C(0xfe000f80);
    instruction |= ((encoded >> 12U) & 1U) << 31U;
    instruction |= ((encoded >> 5U) & UINT32_C(0x3f)) << 25U;
    instruction |= ((encoded >> 1U) & UINT32_C(0x0f)) << 8U;
    instruction |= ((encoded >> 11U) & 1U) << 7U;
    store_u32le(section->data + (size_t)offset, instruction);
    return true;
}


static bool static_patch_rvc_branch(MiniLdSection *section,
                                    uint64_t offset,
                                    int64_t delta,
                                    FILE *diagnostics) {
    uint16_t instruction;
    uint16_t encoded;

    if ((delta & 1) != 0 || delta < -256 || delta > 254) {
        fprintf(diagnostics,
                "minic-ld: R_RISCV_RVC_BRANCH-overflow:delta=%lld\n",
                (long long)delta);
        return false;
    }
    if (section->type == SHT_NOBITS || offset > SIZE_MAX ||
        !range_ok((size_t)offset, 2U, section->size)) {
        fprintf(diagnostics, "minic-ld: relocation-offset-out-of-range\n");
        return false;
    }
    encoded = (uint16_t)((uint64_t)delta & UINT64_C(0x1ff));
    instruction = load_u16le(section->data + (size_t)offset);
    instruction &= (uint16_t)~UINT16_C(0x1c7c);
    instruction |= (uint16_t)(((encoded >> 8U) & 1U) << 12U);
    instruction |= (uint16_t)(((encoded >> 3U) & 3U) << 10U);
    instruction |= (uint16_t)(((encoded >> 6U) & 3U) << 5U);
    instruction |= (uint16_t)(((encoded >> 1U) & 3U) << 3U);
    instruction |= (uint16_t)(((encoded >> 5U) & 1U) << 2U);
    store_u16le(section->data + (size_t)offset, instruction);
    return true;
}

static bool static_patch_rvc_jump(MiniLdSection *section,
                                  uint64_t offset,
                                  int64_t delta,
                                  FILE *diagnostics) {
    uint16_t instruction;
    uint16_t encoded;

    if ((delta & 1) != 0 || delta < -2048 || delta > 2046) {
        fprintf(diagnostics,
                "minic-ld: R_RISCV_RVC_JUMP-overflow:delta=%lld\n",
                (long long)delta);
        return false;
    }
    if (section->type == SHT_NOBITS || offset > SIZE_MAX ||
        !range_ok((size_t)offset, 2U, section->size)) {
        fprintf(diagnostics, "minic-ld: relocation-offset-out-of-range\n");
        return false;
    }
    encoded = (uint16_t)((uint64_t)delta & UINT64_C(0xfff));
    instruction = load_u16le(section->data + (size_t)offset);
    instruction &= (uint16_t)~UINT16_C(0x1ffc);
    instruction |= (uint16_t)(((encoded >> 11U) & 1U) << 12U);
    instruction |= (uint16_t)(((encoded >> 4U) & 1U) << 11U);
    instruction |= (uint16_t)(((encoded >> 8U) & 3U) << 9U);
    instruction |= (uint16_t)(((encoded >> 10U) & 1U) << 8U);
    instruction |= (uint16_t)(((encoded >> 6U) & 1U) << 7U);
    instruction |= (uint16_t)(((encoded >> 7U) & 1U) << 6U);
    instruction |= (uint16_t)(((encoded >> 1U) & 7U) << 3U);
    instruction |= (uint16_t)(((encoded >> 5U) & 1U) << 2U);
    store_u16le(section->data + (size_t)offset, instruction);
    return true;
}

static bool static_patch_jal(MiniLdSection *section,
                             uint64_t offset,
                             int64_t delta,
                             FILE *diagnostics) {
    uint32_t instruction;
    uint32_t encoded;

    if ((delta & 1) != 0 || delta < -1048576 || delta > 1048574) {
        fprintf(diagnostics,
                "minic-ld: R_RISCV_JAL-overflow:delta=%lld\n",
                (long long)delta);
        return false;
    }
    if (section->type == SHT_NOBITS || offset > SIZE_MAX ||
        !range_ok((size_t)offset, 4U, section->size)) {
        fprintf(diagnostics, "minic-ld: relocation-offset-out-of-range\n");
        return false;
    }
    encoded = (uint32_t)delta & UINT32_C(0x1fffff);
    instruction = load_u32le(section->data + (size_t)offset);
    instruction &= UINT32_C(0x00000fff);
    instruction |= ((encoded >> 20U) & 1U) << 31U;
    instruction |= ((encoded >> 1U) & UINT32_C(0x03ff)) << 21U;
    instruction |= ((encoded >> 11U) & 1U) << 20U;
    instruction |= ((encoded >> 12U) & UINT32_C(0x00ff)) << 12U;
    store_u32le(section->data + (size_t)offset, instruction);
    return true;
}

static bool static_allocate_common(MiniLdState *state) {
    size_t bss_index = SIZE_MAX;
    size_t i;

    for (i = 0U; i < state->resolver.count; ++i) {
        MiniLdResolvedSymbol *symbol = &state->resolver.entries[i];
        size_t aligned;
        uint64_t alignment;

        if (symbol->section != MINILD_SECTION_COMMON) {
            continue;
        }
        if (bss_index == SIZE_MAX &&
            !find_or_add_section(state,
                                 ".bss",
                                 SHT_NOBITS,
                                 SHF_ALLOC | SHF_WRITE,
                                 16U,
                                 0U,
                                 &bss_index)) {
            return false;
        }
        /* An input-backed .bss may leave an empty GC merger placeholder.
         * COMMON allocation turns that holder into a real synthetic output
         * section again, independently of the original input fragments. */
        state->work.sections[bss_index].gc_discarded = false;
        alignment = symbol->value == 0U ? 1U : symbol->value;
        if (!align_up_size(state->work.sections[bss_index].size,
                           alignment,
                           &aligned) ||
            !section_append_zero(&state->work.sections[bss_index],
                                 aligned - state->work.sections[bss_index].size) ||
            !section_append_zero(&state->work.sections[bss_index],
                                 (size_t)symbol->size)) {
            fprintf(state->diagnostics, "minic-ld: common-allocation-overflow\n");
            return false;
        }
        symbol->section = (int)bss_index;
        symbol->value = aligned;
    }
    return true;
}


static void static_got_destroy(MiniLdStaticGot *got) {
    free(got->slot_offsets);
    got->slot_offsets = NULL;
    got->symbol_count = 0U;
    got->section = SIZE_MAX;
    got->present = false;
}

static bool static_build_got(MiniLdState *state, MiniLdStaticGot *got) {
    size_t i;

    memset(got, 0, sizeof(*got));
    got->section = SIZE_MAX;
    got->symbol_count = state->resolver.count;
    got->slot_offsets =
        malloc((got->symbol_count == 0U ? 1U : got->symbol_count) *
               sizeof(*got->slot_offsets));
    if (got->slot_offsets == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:static-got\n");
        return false;
    }
    for (i = 0U; i < got->symbol_count; ++i) {
        got->slot_offsets[i] = SIZE_MAX;
    }

    for (i = 0U; i < state->relocations.count; ++i) {
        MiniLdReloc *reloc = &state->relocations.entries[i];
        MiniLdSection *section;
        size_t aligned;

        if (reloc->type != R_RISCV_GOT_HI20) {
            continue;
        }
        if (reloc->symbol == SIZE_MAX ||
            reloc->symbol >= got->symbol_count ||
            reloc->addend != 0) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-R_RISCV_GOT_HI20\n");
            static_got_destroy(got);
            return false;
        }
        if (got->slot_offsets[reloc->symbol] != SIZE_MAX) {
            continue;
        }
        if (!got->present) {
            if (!find_or_add_section(state,
                                     ".got",
                                     SHT_PROGBITS,
                                     SHF_ALLOC | SHF_WRITE,
                                     8U,
                                     0U,
                                     &got->section)) {
                static_got_destroy(got);
                return false;
            }
            /* As with COMMON, a generated GOT can reuse an emptied source
             * merger. It is live synthetic output, never a GC tombstone. */
            state->work.sections[got->section].gc_discarded = false;
            got->present = true;
        }

        section = &state->work.sections[got->section];
        if (!align_up_size(section->size, 8U, &aligned) ||
            !section_append_zero(section, aligned - section->size) ||
            !section_append_zero(section, 8U)) {
            fprintf(state->diagnostics,
                    "minic-ld: static-got-allocation-overflow\n");
            static_got_destroy(got);
            return false;
        }
        got->slot_offsets[reloc->symbol] = aligned;
    }
    return true;
}


static int static_align_site_compare(const void *left, const void *right) {
    const MiniLdAlignSite *a = left;
    const MiniLdAlignSite *b = right;

    if (a->offset < b->offset) {
        return -1;
    }
    if (a->offset > b->offset) {
        return 1;
    }
    return 0;
}

static bool static_append_riscv_nops(unsigned char *output,
                                     size_t output_capacity,
                                     size_t *output_size,
                                     size_t length) {
    while (length >= 4U) {
        if (!range_ok(*output_size, 4U, output_capacity)) {
            return false;
        }
        output[*output_size + 0U] = 0x13U;
        output[*output_size + 1U] = 0x00U;
        output[*output_size + 2U] = 0x00U;
        output[*output_size + 3U] = 0x00U;
        *output_size += 4U;
        length -= 4U;
    }
    if (length == 2U) {
        if (!range_ok(*output_size, 2U, output_capacity)) {
            return false;
        }
        output[*output_size + 0U] = 0x01U;
        output[*output_size + 1U] = 0x00U;
        *output_size += 2U;
        length = 0U;
    }
    return length == 0U;
}

static size_t static_map_relaxed_offset(
    size_t raw_offset,
    const MiniLdRelaxEvent *events,
    size_t event_count) {
    size_t deleted = 0U;
    size_t i;

    for (i = 0U; i < event_count; ++i) {
        if (raw_offset < events[i].raw_end) {
            break;
        }
        deleted = events[i].cumulative_deleted;
    }
    return raw_offset - deleted;
}

static bool static_relax_align_section(MiniLdState *state,
                                       size_t section_index,
                                       uint64_t base_addr) {
    MiniLdSection *section = &state->work.sections[section_index];
    MiniLdAlignSite *sites = NULL;
    MiniLdRelaxEvent *events = NULL;
    unsigned char *output = NULL;
    size_t site_count = 0U;
    size_t event_count = 0U;
    size_t output_size = 0U;
    size_t cursor = 0U;
    size_t i;
    bool ok = false;

    size_t relocation_begin;
    size_t relocation_end;

    if (section->type == SHT_NOBITS || section->size == 0U) {
        return true;
    }
    if (!relocations_build_section_index(&state->relocations,
                                         state->work.count,
                                         state->diagnostics)) {
        return false;
    }
    /* Synthetic script-only sections appended later have no relocations. */
    if (section_index >= state->relocations.index_sections) {
        return true;
    }
    relocation_begin = state->relocations.section_starts[section_index];
    relocation_end = state->relocations.section_starts[section_index + 1U];
    for (i = relocation_begin; i < relocation_end; ++i) {
        if (state->relocations.entries[state->relocations.section_refs[i]].type == R_RISCV_ALIGN) {
            ++site_count;
        }
    }
    if (site_count == 0U) {
        return true;
    }

    sites = malloc(site_count * sizeof(*sites));
    events = malloc(site_count * sizeof(*events));
    output = malloc(section->size == 0U ? 1U : section->size);
    if (sites == NULL || events == NULL || output == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:align-relax:%s\n",
                section->name);
        goto done;
    }

    site_count = 0U;
    for (i = relocation_begin; i < relocation_end; ++i) {
        MiniLdReloc *reloc = &state->relocations.entries[state->relocations.section_refs[i]];
        if (reloc->type != R_RISCV_ALIGN) {
            continue;
        }
        if (reloc->offset > SIZE_MAX || reloc->addend < 0 ||
            (uint64_t)reloc->addend > SIZE_MAX) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-R_RISCV_ALIGN:%s\n",
                    section->name);
            goto done;
        }
        sites[site_count].offset = (size_t)reloc->offset;
        sites[site_count].max_padding = (size_t)reloc->addend;
        ++site_count;
    }
    qsort(sites,
          site_count,
          sizeof(*sites),
          static_align_site_compare);

    for (i = 0U; i < site_count; ++i) {
        size_t raw_offset = sites[i].offset;
        size_t max_padding = sites[i].max_padding;
        size_t alignment = 1U;
        size_t required;
        uint64_t place;
        size_t bits_value;

        if ((max_padding & 1U) != 0U ||
            raw_offset < cursor ||
            !range_ok(raw_offset, max_padding, section->size)) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-R_RISCV_ALIGN-range:%s:%zu+%zu\n",
                    section->name,
                    raw_offset,
                    max_padding);
            goto done;
        }

        if (!range_ok(cursor, raw_offset - cursor, section->size) ||
            !range_ok(output_size, raw_offset - cursor, section->size)) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-R_RISCV_ALIGN-copy:%s\n",
                    section->name);
            goto done;
        }
        memcpy(output + output_size,
               section->data + cursor,
               raw_offset - cursor);
        output_size += raw_offset - cursor;

        bits_value = max_padding;
        while (bits_value != 0U) {
            if (alignment > SIZE_MAX / 2U) {
                fprintf(state->diagnostics,
                        "minic-ld: R_RISCV_ALIGN-overflow:%s\n",
                        section->name);
                goto done;
            }
            alignment *= 2U;
            bits_value >>= 1U;
        }

        if (base_addr > UINT64_MAX - (uint64_t)output_size) {
            fprintf(state->diagnostics,
                    "minic-ld: R_RISCV_ALIGN-address-overflow:%s\n",
                    section->name);
            goto done;
        }
        place = base_addr + (uint64_t)output_size;
        required = (size_t)((-(uint64_t)place) &
                            (uint64_t)(alignment - 1U));
        if (required > max_padding || (required & 1U) != 0U) {
            fprintf(state->diagnostics,
                    "minic-ld: R_RISCV_ALIGN-cannot-satisfy:%s:"
                    "align=%zu:required=%zu:max=%zu\n",
                    section->name,
                    alignment,
                    required,
                    max_padding);
            goto done;
        }
        if (!static_append_riscv_nops(output,
                                      section->size,
                                      &output_size,
                                      required)) {
            fprintf(state->diagnostics,
                    "minic-ld: R_RISCV_ALIGN-nop-overflow:%s\n",
                    section->name);
            goto done;
        }

        events[event_count].raw_end = raw_offset + max_padding;
        events[event_count].cumulative_deleted =
            (event_count == 0U
                 ? 0U
                 : events[event_count - 1U].cumulative_deleted) +
            (max_padding - required);
        ++event_count;
        cursor = raw_offset + max_padding;
    }

    if (!range_ok(cursor, section->size - cursor, section->size) ||
        !range_ok(output_size, section->size - cursor, section->size)) {
        fprintf(state->diagnostics,
                "minic-ld: R_RISCV_ALIGN-tail-overflow:%s\n",
                section->name);
        goto done;
    }
    memcpy(output + output_size,
           section->data + cursor,
           section->size - cursor);
    output_size += section->size - cursor;

    for (i = 0U; i < state->resolver.count; ++i) {
        MiniLdResolvedSymbol *symbol = &state->resolver.entries[i];

        if (symbol->section == (int)section_index &&
            symbol->value <= SIZE_MAX) {
            symbol->value = (uint64_t)static_map_relaxed_offset(
                (size_t)symbol->value,
                events,
                event_count);
        }
    }

    for (i = relocation_begin; i < relocation_end; ++i) {
        MiniLdReloc *reloc = &state->relocations.entries[state->relocations.section_refs[i]];

        if (reloc->offset <= SIZE_MAX) {
            reloc->offset = (uint64_t)static_map_relaxed_offset(
                (size_t)reloc->offset,
                events,
                event_count);
        }
    }

    memcpy(section->data, output, output_size);
    section->size = output_size;
    ok = true;

done:
    free(output);
    free(events);
    free(sites);
    return ok;
}

static bool static_build_layout_at(MiniLdState *state,
                                   MiniLdLayout *layout,
                                   uint64_t base_vaddr) {
    const size_t page = 4096U;
    const size_t header_page = 4096U;
    size_t rx_cursor = 0U;
    size_t rw_file_cursor = 0U;
    size_t rw_mem_cursor = 0U;
    size_t i;

    memset(layout, 0, sizeof(*layout));
    layout->section_vaddr =
        calloc(state->work.count == 0U ? 1U : state->work.count,
               sizeof(*layout->section_vaddr));
    layout->section_laddr =
        calloc(state->work.count == 0U ? 1U : state->work.count,
               sizeof(*layout->section_laddr));
    layout->section_file_offset =
        malloc((state->work.count == 0U ? 1U : state->work.count) *
               sizeof(*layout->section_file_offset));
    layout->section_discarded =
        calloc(state->work.count == 0U ? 1U : state->work.count,
               sizeof(*layout->section_discarded));
    if (layout->section_vaddr == NULL || layout->section_laddr == NULL ||
        layout->section_file_offset == NULL ||
        layout->section_discarded == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:static-layout\n");
        return false;
    }
    for (i = 0U; i < state->work.count; ++i) {
        layout->section_file_offset[i] = SIZE_MAX;
    }

    layout->rx_file_offset = header_page;
    layout->rx_vaddr = base_vaddr;

    for (i = 0U; i < state->work.count; ++i) {
        MiniLdSection *section = &state->work.sections[i];
        size_t aligned;

        if ((section->flags & SHF_ALLOC) == 0U ||
            (section->flags & SHF_WRITE) != 0U) {
            continue;
        }
        if (section->type == SHT_NOBITS) {
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-rx-nobits:%s\n",
                    section->name);
            return false;
        }
        if (!align_up_size(rx_cursor, section->align, &aligned)) {
            fprintf(state->diagnostics, "minic-ld: static-layout-overflow\n");
            return false;
        }
        rx_cursor = aligned;
        layout->section_vaddr[i] = layout->rx_vaddr + rx_cursor;
        layout->section_laddr[i] = layout->section_vaddr[i];
        layout->section_file_offset[i] = layout->rx_file_offset + rx_cursor;
        if (!static_relax_align_section(state,
                                        i,
                                        layout->section_vaddr[i])) {
            return false;
        }
        if (!add_size(rx_cursor, section->size, &rx_cursor)) {
            fprintf(state->diagnostics, "minic-ld: static-layout-overflow\n");
            return false;
        }
        layout->have_rx = true;
    }
    layout->rx_file_size = rx_cursor;

    if (!align_up_size(layout->rx_file_offset + layout->rx_file_size,
                       page,
                       &layout->rw_file_offset)) {
        fprintf(state->diagnostics, "minic-ld: static-layout-overflow\n");
        return false;
    }
    {
        size_t next_vaddr;
        if (!align_up_size((size_t)(layout->rx_vaddr + layout->rx_file_size),
                           page,
                           &next_vaddr)) {
            fprintf(state->diagnostics, "minic-ld: static-layout-overflow\n");
            return false;
        }
        layout->rw_vaddr = (uint64_t)next_vaddr;
    }

    for (i = 0U; i < state->work.count; ++i) {
        MiniLdSection *section = &state->work.sections[i];
        size_t aligned;

        if ((section->flags & (SHF_ALLOC | SHF_WRITE)) !=
                (SHF_ALLOC | SHF_WRITE) ||
            section->type == SHT_NOBITS) {
            continue;
        }
        if (!align_up_size(rw_file_cursor, section->align, &aligned)) {
            fprintf(state->diagnostics, "minic-ld: static-layout-overflow\n");
            return false;
        }
        rw_file_cursor = aligned;
        rw_mem_cursor = aligned;
        layout->section_vaddr[i] = layout->rw_vaddr + rw_mem_cursor;
        layout->section_laddr[i] = layout->section_vaddr[i];
        layout->section_file_offset[i] =
            layout->rw_file_offset + rw_file_cursor;
        if (!static_relax_align_section(state,
                                        i,
                                        layout->section_vaddr[i])) {
            return false;
        }
        if (!add_size(rw_file_cursor, section->size, &rw_file_cursor) ||
            !add_size(rw_mem_cursor, section->size, &rw_mem_cursor)) {
            fprintf(state->diagnostics, "minic-ld: static-layout-overflow\n");
            return false;
        }
        layout->have_rw = true;
    }

    for (i = 0U; i < state->work.count; ++i) {
        MiniLdSection *section = &state->work.sections[i];
        size_t aligned;

        if ((section->flags & (SHF_ALLOC | SHF_WRITE)) !=
                (SHF_ALLOC | SHF_WRITE) ||
            section->type != SHT_NOBITS) {
            continue;
        }
        if (!align_up_size(rw_mem_cursor, section->align, &aligned)) {
            fprintf(state->diagnostics, "minic-ld: static-layout-overflow\n");
            return false;
        }
        rw_mem_cursor = aligned;
        layout->section_vaddr[i] = layout->rw_vaddr + rw_mem_cursor;
        layout->section_laddr[i] = layout->section_vaddr[i];
        if (!add_size(rw_mem_cursor, section->size, &rw_mem_cursor)) {
            fprintf(state->diagnostics, "minic-ld: static-layout-overflow\n");
            return false;
        }
        layout->have_rw = true;
    }

    layout->rw_file_size = rw_file_cursor;
    layout->rw_mem_size = rw_mem_cursor;
    return layout->have_rx;
}

static bool static_build_layout(MiniLdState *state,
                                MiniLdLayout *layout) {
    return static_build_layout_at(state, layout, UINT64_C(0x10000));
}

static bool shared_build_layout(MiniLdState *state,
                                MiniLdLayout *layout) {
    /*
     * ET_DYN load bias is defined relative to virtual address zero.
     * Keep the ELF/program headers at vaddr 0 and place the first
     * allocatable section one page later so e_phoff is addressable as
     * base + e_phoff during the dynamic linker's stage-1 bootstrap.
     */
    return static_build_layout_at(state, layout, UINT64_C(0x1000));
}


typedef struct MiniLdScriptLayoutState {
    MiniLdState *state;
    MiniLdLayout *layout;
    const MiniLdScript *script;
    bool *section_placed;
    bool *output_defined;
    uint64_t dot;
    uint64_t load_bias;
    bool have_load_bias;
    uint64_t headers_size;
    bool have_headers_size;
} MiniLdScriptLayoutState;

static bool static_align_up_u64(uint64_t value,
                                uint64_t alignment,
                                uint64_t *out) {
    uint64_t mask;

    if (alignment <= 1U) {
        *out = value;
        return true;
    }
    if ((alignment & (alignment - 1U)) != 0U) {
        return false;
    }
    mask = alignment - 1U;
    if (value > UINT64_MAX - mask) {
        return false;
    }
    *out = (value + mask) & ~mask;
    return true;
}

static bool script_layout_symbol_value(void *context,
                                       const char *name,
                                       uint64_t *value_out) {
    MiniLdScriptLayoutState *script_state = context;
    size_t index = symbol_resolver_lookup(&script_state->state->resolver, name);
    MiniLdResolvedSymbol *symbol;

    if (index == SIZE_MAX) {
        return false;
    }
    symbol = &script_state->state->resolver.entries[index];
    if (symbol->section == MINILD_SECTION_ABS) {
        *value_out = symbol->value;
        return true;
    }
    if (symbol->section < 0 ||
        (size_t)symbol->section >= script_state->state->work.count ||
        !script_state->section_placed[symbol->section] ||
        script_state->layout->section_discarded[symbol->section]) {
        return false;
    }
    *value_out = script_state->layout->section_vaddr[symbol->section] +
                 symbol->value;
    return true;
}

static bool script_layout_section_value(void *context,
                                        const char *name,
                                        uint64_t *value_out) {
    MiniLdScriptLayoutState *script_state = context;
    size_t i;

    for (i = 0U; i < script_state->script->command_count; ++i) {
        const MiniLdScriptCommand *command = &script_state->script->commands[i];

        if (command->kind == MINILD_SCRIPT_OUTPUT_SECTION &&
            script_state->output_defined[i] &&
            strcmp(command->value.section.name, name) == 0) {
            *value_out = script_state->layout->script_output_start[i];
            return true;
        }
    }
    return false;
}

static bool script_layout_evaluate(MiniLdScriptLayoutState *script_state,
                                   MiniLdScriptExprId expression,
                                   uint64_t *value_out) {
    MiniLdScriptEvalContext context;

    memset(&context, 0, sizeof(context));
    context.dot = script_state->dot;
    context.headers_size = script_state->headers_size;
    context.have_headers_size = script_state->have_headers_size;
    context.resolve_symbol = script_layout_symbol_value;
    context.resolve_section = script_layout_section_value;
    context.user = script_state;
    return minild_script_evaluate(script_state->script,
                                  expression,
                                  &context,
                                  value_out,
                                  script_state->state->diagnostics);
}

static bool script_layout_define_symbol(MiniLdScriptLayoutState *script_state,
                                        const char *name,
                                        uint64_t value) {
    size_t index = symbol_resolver_lookup(&script_state->state->resolver, name);

    if (index != SIZE_MAX) {
        uint64_t current;

        if (script_layout_symbol_value(script_state, name, &current) &&
            current == value) {
            return true;
        }
    }
    return symbol_resolver_assign_absolute(&script_state->state->resolver,
                                            script_state->state->diagnostics,
                                            name, value);
}

static bool script_layout_place_section(MiniLdScriptLayoutState *script_state,
                                        size_t section_index,
                                        size_t command_index) {
    MiniLdSection *section = &script_state->state->work.sections[section_index];
    uint64_t aligned;

    if (!static_align_up_u64(script_state->dot,
                             section->align == 0U ? 1U : section->align,
                             &aligned)) {
        fprintf(script_state->state->diagnostics,
                "minic-ld: linker-script-section-alignment-overflow:%s\n",
                section->name);
        return false;
    }
    script_state->dot = aligned;
    script_state->layout->section_vaddr[section_index] = script_state->dot;
    script_state->section_placed[section_index] = true;
    script_state->layout->section_owner[section_index] = command_index;
    if (script_state->script->commands[command_index].kind ==
            MINILD_SCRIPT_OUTPUT_SECTION) {
        section->output_name =
            script_state->script->commands[command_index].value.section.name;
    }

    if ((section->flags & SHF_ALLOC) != 0U &&
        !static_relax_align_section(script_state->state,
                                    section_index,
                                    script_state->dot)) {
        return false;
    }
    if ((uint64_t)section->size > UINT64_MAX - script_state->dot) {
        fprintf(script_state->state->diagnostics,
                "minic-ld: linker-script-section-size-overflow:%s\n",
                section->name);
        return false;
    }
    script_state->dot += (uint64_t)section->size;
    return true;
}

/*
 * GNU SORT_BY_NAME must be stable for equal input section names.
 * The previous insertion sort took O(n*n) strcmp calls for large Linux
 * .initcall and .text linker-script patterns. Sort by (name, input index)
 * once instead, preserving the exact old tie order.
 */
typedef struct MiniLdNamedMatch {
    size_t index;
    const char *name;
} MiniLdNamedMatch;

static int minild_compare_named_match(const void *lhs, const void *rhs) {
    const MiniLdNamedMatch *a = lhs;
    const MiniLdNamedMatch *b = rhs;
    int c = strcmp(a->name, b->name);
    if (c != 0) {
        return c;
    }
    if (a->index != b->index) {
        return a->index < b->index ? -1 : 1;
    }
    return 0;
}

static const char *script_section_source_name(const MiniLdSection *section) {
    return section->input_name != NULL ? section->input_name : section->name;
}

static bool script_layout_place_pattern(MiniLdScriptLayoutState *script_state,
                                        const MiniLdScriptPattern *pattern,
                                        size_t command_index,
                                        bool discard) {
    size_t *matches;
    size_t count = 0U;
    size_t i;

    if (pattern->common) {
        return true;
    }

    matches = malloc((script_state->state->work.count == 0U
                          ? 1U
                          : script_state->state->work.count) *
                     sizeof(*matches));
    if (matches == NULL) {
        fprintf(script_state->state->diagnostics,
                "minic-ld: out-of-memory:linker-script-pattern\n");
        return false;
    }

    for (i = 0U; i < script_state->state->work.count; ++i) {
        if (script_state->section_placed[i] ||
            script_state->state->work.sections[i].gc_discarded ||
            script_state->layout->section_discarded[i]) {
            continue;
        }
        if (minild_script_pattern_matches_input(
                pattern,
                script_section_source_name(&script_state->state->work.sections[i]),
                script_state->state->work.sections[i].input_path)) {
            matches[count++] = i;
        }
    }

    if (pattern->sort && count > 1U) {
        MiniLdNamedMatch *sorted;
        if (count > SIZE_MAX / sizeof(*sorted)) {
            free(matches);
            fprintf(script_state->state->diagnostics,
                    "minic-ld: linker-script-pattern-sort-overflow\n");
            return false;
        }
        sorted = malloc(count * sizeof(*sorted));
        if (sorted == NULL) {
            free(matches);
            fprintf(script_state->state->diagnostics,
                    "minic-ld: out-of-memory:linker-script-pattern-sort\n");
            return false;
        }
        for (i = 0U; i < count; ++i) {
            sorted[i].index = matches[i];
            sorted[i].name =
                script_section_source_name(
                    &script_state->state->work.sections[matches[i]]);
        }
        qsort(sorted, count, sizeof(*sorted), minild_compare_named_match);
        for (i = 0U; i < count; ++i) {
            matches[i] = sorted[i].index;
        }
        free(sorted);
    }

    for (i = 0U; i < count; ++i) {
        size_t section_index = matches[i];

        if (discard) {
            script_state->layout->section_discarded[section_index] = true;
            script_state->section_placed[section_index] = true;
            continue;
        }
        if (!script_layout_place_section(script_state,
                                         section_index,
                                         command_index)) {
            free(matches);
            return false;
        }
    }

    free(matches);
    return true;
}

static bool script_layout_bind_output_symbols(
    MiniLdScriptLayoutState *script_state,
    const MiniLdScriptOutputSection *output,
    size_t command_index) {
    size_t anchor = SIZE_MAX;
    uint64_t anchor_vaddr;
    size_t i;

    for (i = 0U; i < script_state->state->work.count; ++i) {
        MiniLdSection *section = &script_state->state->work.sections[i];

        if (script_state->layout->section_owner[i] == command_index &&
            !script_state->layout->section_discarded[i] &&
            (section->flags & SHF_ALLOC) != 0U &&
            (anchor == SIZE_MAX ||
             script_state->layout->section_vaddr[i] <
                 script_state->layout->section_vaddr[anchor])) {
            anchor = i;
        }
    }
    if (anchor == SIZE_MAX) {
        return true;
    }
    anchor_vaddr = script_state->layout->section_vaddr[anchor];

    for (i = 0U; i < output->item_count; ++i) {
        const MiniLdScriptSectionItem *item = &output->items[i];
        MiniLdResolvedSymbol *symbol;
        size_t symbol_index;

        if (item->kind != MINILD_SCRIPT_SECTION_DEFINE_SYMBOL) {
            continue;
        }
        if (item->value.symbol.expression >=
                script_state->script->expression_count ||
            script_state->script->expressions[
                item->value.symbol.expression].kind ==
                MINILD_SCRIPT_EXPR_ABSOLUTE) {
            continue;
        }
        symbol_index = symbol_resolver_lookup(&script_state->state->resolver,
                                          item->value.symbol.name);
        if (symbol_index == SIZE_MAX) {
            fprintf(script_state->state->diagnostics,
                    "minic-ld: linker-script-missing-defined-symbol:%s\n",
                    item->value.symbol.name);
            return false;
        }
        symbol = &script_state->state->resolver.entries[symbol_index];
        if (symbol->section != MINILD_SECTION_ABS ||
            symbol->value < anchor_vaddr) {
            continue;
        }
        symbol->section = (int)anchor;
        symbol->value -= anchor_vaddr;
    }
    return true;
}

static bool script_layout_process_output(MiniLdScriptLayoutState *script_state,
                                         const MiniLdScriptOutputSection *output,
                                         size_t command_index) {
    uint64_t output_start;
    uint64_t lma_start;
    size_t byte_start = script_state->layout->script_byte_count;
    size_t i;

    if (output->fill != MINILD_SCRIPT_EXPR_NONE) {
        uint64_t fill;

        if (!script_layout_evaluate(script_state, output->fill, &fill)) {
            return false;
        }
        if (fill > UINT8_MAX) {
            fprintf(script_state->state->diagnostics,
                    "minic-ld: unsupported-script-fill-pattern:%s\n",
                    output->name);
            return false;
        }
        script_state->layout->script_fill[command_index] =
            (unsigned char)fill;
        script_state->layout->script_have_fill[command_index] = true;
    }

    if (output->discard) {
        for (i = 0U; i < output->item_count; ++i) {
            const MiniLdScriptSectionItem *item = &output->items[i];

            if (item->kind == MINILD_SCRIPT_SECTION_PATTERN &&
                !script_layout_place_pattern(script_state,
                                             &item->value.pattern,
                                             command_index,
                                             true)) {
                return false;
            }
        }
        return true;
    }

    if (output->address != MINILD_SCRIPT_EXPR_NONE) {
        if (!script_layout_evaluate(script_state,
                                    output->address,
                                    &script_state->dot)) {
            return false;
        }
    }
    if (output->align != MINILD_SCRIPT_EXPR_NONE) {
        uint64_t alignment;

        if (!script_layout_evaluate(script_state,
                                    output->align,
                                    &alignment) ||
            !static_align_up_u64(script_state->dot,
                                 alignment == 0U ? 1U : alignment,
                                 &script_state->dot)) {
            fprintf(script_state->state->diagnostics,
                    "minic-ld: linker-script-output-alignment:%s\n",
                    output->name);
            return false;
        }
    }

    /*
     * GNU ld aligns an output section to the strictest participating
     * input-section alignment BEFORE evaluating leading symbol
     * assignments. Without this, __start___param may precede the first
     * aligned __param input by two bytes, causing parse_args() to read
     * a shifted pointer and page-fault during Linux boot.
     *
     * Respect an explicit output VMA; no implicit VMA alignment may
     * override the address selected by the linker script.
     */
    if (output->address == MINILD_SCRIPT_EXPR_NONE) {
        uint64_t input_alignment = 1U;
        size_t item_index;

        for (item_index = 0U; item_index < output->item_count;
             ++item_index) {
            const MiniLdScriptSectionItem *item = &output->items[item_index];
            size_t section_index;

            if (item->kind != MINILD_SCRIPT_SECTION_PATTERN ||
                item->value.pattern.common) {
                continue;
            }
            for (section_index = 0U;
                 section_index < script_state->state->work.count;
                 ++section_index) {
                const MiniLdSection *section =
                    &script_state->state->work.sections[section_index];
                uint64_t align = section->align == 0U ? 1U : section->align;

                if (script_state->section_placed[section_index] ||
                    section->gc_discarded ||
                    script_state->layout->section_discarded[section_index] ||
                    (section->flags & SHF_ALLOC) == 0U ||
                    !minild_script_pattern_matches_input(
                        &item->value.pattern,
                        script_section_source_name(section),
                        section->input_path)) {
                    continue;
                }
                if (align > input_alignment) {
                    input_alignment = align;
                }
            }
        }
        if (!static_align_up_u64(script_state->dot, input_alignment,
                                 &script_state->dot)) {
            fprintf(script_state->state->diagnostics,
                    "minic-ld: linker-script-input-alignment:%s\n",
                    output->name);
            return false;
        }
    }

    output_start = script_state->dot;
    script_state->output_defined[command_index] = true;
    script_state->layout->script_output_start[command_index] = output_start;

    for (i = 0U; i < output->item_count; ++i) {
        const MiniLdScriptSectionItem *item = &output->items[i];
        uint64_t value;

        switch (item->kind) {
        case MINILD_SCRIPT_SECTION_PATTERN:
            if (!script_layout_place_pattern(script_state,
                                             &item->value.pattern,
                                             command_index,
                                             false)) {
                return false;
            }
            break;
        case MINILD_SCRIPT_SECTION_DEFINE_SYMBOL:
            if (!script_layout_evaluate(script_state,
                                        item->value.symbol.expression,
                                        &value) ||
                !script_layout_define_symbol(script_state,
                                             item->value.symbol.name,
                                             value)) {
                return false;
            }
            break;
        case MINILD_SCRIPT_SECTION_SET_DOT:
            if (!script_layout_evaluate(script_state,
                                        item->value.expression,
                                        &value)) {
                return false;
            }
            if (value < script_state->dot) {
                fprintf(script_state->state->diagnostics,
                        "minic-ld: linker-script-dot-moved-backward:%s\n",
                        output->name);
                return false;
            }
            script_state->dot = value;
            break;
        case MINILD_SCRIPT_SECTION_BYTE:
            if (!script_layout_evaluate(script_state,
                                        item->value.expression,
                                        &value)) {
                return false;
            }
            if (script_state->dot == UINT64_MAX) {
                fprintf(script_state->state->diagnostics,
                        "minic-ld: linker-script-BYTE-overflow:%s\n",
                        output->name);
                return false;
            }
            {
                MiniLdLayout *layout = script_state->layout;
                if (layout->script_byte_count == layout->script_byte_capacity) {
                    size_t capacity = layout->script_byte_capacity == 0U
                                          ? 16U : layout->script_byte_capacity * 2U;
                    MiniLdScriptBytePatch *patches;
                    if (capacity < layout->script_byte_capacity ||
                        capacity > SIZE_MAX / sizeof(*patches)) {
                        fprintf(script_state->state->diagnostics,
                                "minic-ld: script-BYTE-count-overflow\n");
                        return false;
                    }
                    patches = realloc(layout->script_bytes,
                                      capacity * sizeof(*patches));
                    if (patches == NULL) {
                        fprintf(script_state->state->diagnostics,
                                "minic-ld: out-of-memory:script-BYTE\n");
                        return false;
                    }
                    layout->script_bytes = patches;
                    layout->script_byte_capacity = capacity;
                }
                layout->script_bytes[layout->script_byte_count].command =
                    command_index;
                layout->script_bytes[layout->script_byte_count].address =
                    script_state->dot;
                layout->script_bytes[layout->script_byte_count].value =
                    (unsigned char)value;
                ++layout->script_byte_count;
            }
            ++script_state->dot;
            break;
        case MINILD_SCRIPT_SECTION_CONSTRUCTORS:
            break;
        }
    }

    script_state->layout->script_output_end[command_index] =
        script_state->dot;

    /* A pure BYTE output needs a working section to anchor its layout.
     * Mixed input/BYTE outputs instead keep their real work fragments and
     * materialize byte members at their output-relative file addresses. */
    if (script_state->layout->script_byte_count != byte_start) {
        bool has_input = false;

        for (i = 0U; i < script_state->state->work.count; ++i) {
            if (script_state->layout->section_owner[i] == command_index &&
                !script_state->layout->section_discarded[i]) {
                has_input = true;
                break;
            }
        }
        if (!has_input) {
            MiniLdState *state = script_state->state;
            MiniLdSection *section;
            size_t synthetic_index;
            size_t length;

            if (script_state->dot < output_start ||
                script_state->dot - output_start > SIZE_MAX ||
                find_section_index(state, output->name,
                                   hash_text(output->name)) != SIZE_MAX) {
                fprintf(state->diagnostics,
                        "minic-ld: unsupported-script-generated-section:%s\n",
                        output->name);
                return false;
            }
            length = (size_t)(script_state->dot - output_start);
            if (!find_or_add_section(state, output->name,
                                     SHT_PROGBITS, SHF_ALLOC, 1U, 0U,
                                     &synthetic_index)) {
                return false;
            }
            section = &state->work.sections[synthetic_index];
            if (!section_append_zero(section, length)) {
                fprintf(state->diagnostics,
                        "minic-ld: script-generated-section-overflow:%s\n",
                        output->name);
                return false;
            }
            for (i = byte_start;
                 i < script_state->layout->script_byte_count; ++i) {
                const MiniLdScriptBytePatch *patch =
                    &script_state->layout->script_bytes[i];
                uint64_t off = patch->address - output_start;

                if (off >= length) {
                    fprintf(state->diagnostics,
                            "minic-ld: invalid-script-BYTE-address:%s\n",
                            output->name);
                    return false;
                }
                section->data[(size_t)off] = patch->value;
            }
            script_state->layout->section_vaddr[synthetic_index] =
                output_start;
            script_state->layout->section_owner[synthetic_index] = command_index;
            script_state->section_placed[synthetic_index] = true;
        }
    }

    if (output->at != MINILD_SCRIPT_EXPR_NONE) {
        if (!script_layout_evaluate(script_state,
                                    output->at,
                                    &lma_start)) {
            return false;
        }
        script_state->load_bias = output_start - lma_start;
        script_state->have_load_bias = true;
    } else if (script_state->have_load_bias) {
        lma_start = output_start - script_state->load_bias;
    } else {
        lma_start = output_start;
    }

    for (i = 0U; i < script_state->state->work.count; ++i) {
        if (script_state->layout->section_owner[i] == command_index) {
            script_state->layout->section_laddr[i] =
                lma_start +
                (script_state->layout->section_vaddr[i] - output_start);
        }
    }
    return script_layout_bind_output_symbols(script_state,
                                             output,
                                             command_index);
}

/*
 * The old layout scanned all sections to find the next lowest LMA on EVERY
 * iteration (quadratic in alloc section count). Linux emits many section
 * fragments. Sorting stable keys once preserves the original first-minimum
 * selection exactly while reducing this operation to O(S log S).
 */
typedef struct MiniLdFileOrder {
    uint64_t laddr;
    uint64_t vaddr;
    size_t index;
} MiniLdFileOrder;

static int minild_compare_file_order(const void *lhs, const void *rhs) {
    const MiniLdFileOrder *a = lhs;
    const MiniLdFileOrder *b = rhs;
    if (a->laddr != b->laddr) {
        return a->laddr < b->laddr ? -1 : 1;
    }
    if (a->vaddr != b->vaddr) {
        return a->vaddr < b->vaddr ? -1 : 1;
    }
    if (a->index != b->index) {
        return a->index < b->index ? -1 : 1;
    }
    return 0;
}

static bool static_build_script_layout(MiniLdState *state,
                                       const MiniLdScript *script,
                                       MiniLdLayout *layout,
                                       size_t shared_headers_size) {
    MiniLdScriptLayoutState script_state;
    MiniLdFileOrder *file_order = NULL;
    bool *command_deferred = NULL;
    size_t i;
    size_t alloc_count = 0U;
    uint64_t *owner_max_align = NULL;
    size_t *owner_remaining = NULL;
    size_t storage_count;
    size_t file_cursor;
    size_t header_size;
    bool ok = false;

    memset(layout, 0, sizeof(*layout));
    memset(&script_state, 0, sizeof(script_state));
    /* Reserve one additional generated section per script command. */
    if (script->command_count > SIZE_MAX - state->work.count) {
        fprintf(state->diagnostics,
                "minic-ld: script-section-count-overflow\n");
        return false;
    }
    storage_count = state->work.count + script->command_count;
    if (storage_count == 0U) {
        storage_count = 1U;
    }

    layout->section_vaddr =
        calloc(storage_count,
               sizeof(*layout->section_vaddr));
    layout->section_laddr =
        calloc(storage_count,
               sizeof(*layout->section_laddr));
    layout->section_file_offset =
        malloc((storage_count) *
               sizeof(*layout->section_file_offset));
    layout->section_discarded =
        calloc(storage_count,
               sizeof(*layout->section_discarded));
    script_state.section_placed =
        calloc(storage_count,
               sizeof(*script_state.section_placed));
    layout->section_owner =
        malloc((storage_count) *
               sizeof(*layout->section_owner));
    layout->script_fill = calloc(script->command_count == 0U ? 1U :
                                 script->command_count, sizeof(*layout->script_fill));
    layout->script_have_fill = calloc(script->command_count == 0U ? 1U :
                                      script->command_count,
                                      sizeof(*layout->script_have_fill));
    layout->script_output_start = calloc(script->command_count == 0U ? 1U :
                                         script->command_count,
                                         sizeof(*layout->script_output_start));
    layout->script_output_end = calloc(script->command_count == 0U ? 1U :
                                       script->command_count,
                                       sizeof(*layout->script_output_end));
    layout->script_output_file_base = malloc((script->command_count == 0U ? 1U :
                                               script->command_count) *
                                              sizeof(*layout->script_output_file_base));
    script_state.output_defined =
        calloc(script->command_count == 0U ? 1U : script->command_count,
               sizeof(*script_state.output_defined));
    command_deferred =
        calloc(script->command_count == 0U ? 1U : script->command_count,
               sizeof(*command_deferred));
    owner_max_align = calloc(script->command_count == 0U ? 1U :
                              script->command_count, sizeof(*owner_max_align));
    owner_remaining = calloc(script->command_count == 0U ? 1U :
                              script->command_count, sizeof(*owner_remaining));

    if (layout->section_vaddr == NULL || layout->section_laddr == NULL ||
        layout->section_file_offset == NULL ||
        layout->section_discarded == NULL ||
        script_state.section_placed == NULL ||
        layout->section_owner == NULL ||
        layout->script_fill == NULL || layout->script_have_fill == NULL ||
        layout->script_output_start == NULL ||
        layout->script_output_end == NULL ||
        layout->script_output_file_base == NULL ||
        script_state.output_defined == NULL ||
        command_deferred == NULL || owner_max_align == NULL ||
        owner_remaining == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:linker-script-layout\n");
        goto done;
    }

    for (i = 0U; i < state->work.count; ++i) {
        layout->section_file_offset[i] = SIZE_MAX;
        layout->section_owner[i] = SIZE_MAX;
    }
    for (i = 0U; i < script->command_count; ++i) {
        layout->script_output_file_base[i] = SIZE_MAX;
    }
    script_state.state = state;
    script_state.layout = layout;
    script_state.script = script;
    script_state.have_headers_size = shared_headers_size != 0U;
    script_state.headers_size = shared_headers_size;

    for (i = 0U; i < script->command_count; ++i) {
        const MiniLdScriptCommand *command = &script->commands[i];
        uint64_t value;

        switch (command->kind) {
        case MINILD_SCRIPT_SET_DOT:
            if (!script_layout_evaluate(&script_state,
                                        command->value.expression,
                                        &value)) {
                goto done;
            }
            if (value < script_state.dot) {
                fprintf(state->diagnostics,
                        "minic-ld: linker-script-top-level-dot-moved-backward\n");
                goto done;
            }
            script_state.dot = value;
            break;
        case MINILD_SCRIPT_DEFINE_SYMBOL:
            if (command->value.symbol.provide) {
                size_t sym = symbol_resolver_lookup(&state->resolver,
                                                command->value.symbol.name);
                if (sym == SIZE_MAX ||
                    state->resolver.entries[sym].section != MINILD_SECTION_UNDEF) {
                    break;
                }
            }
            if (script->expressions[command->value.symbol.expression].kind ==
                    MINILD_SCRIPT_EXPR_SYMBOL &&
                !script_layout_symbol_value(
                    &script_state,
                    script->expressions[command->value.symbol.expression].name,
                    &value)) {
                command_deferred[i] = true;
                break;
            }
            if (!script_layout_evaluate(&script_state,
                                        command->value.symbol.expression,
                                        &value) ||
                !script_layout_define_symbol(&script_state,
                                             command->value.symbol.name,
                                             value)) {
                goto done;
            }
            break;
        case MINILD_SCRIPT_OUTPUT_SECTION:
            if (!script_layout_process_output(&script_state,
                                             &command->value.section,
                                             i)) {
                goto done;
            }
            break;
        }
    }

    for (i = 0U; i < script->command_count; ++i) {
        const MiniLdScriptCommand *command = &script->commands[i];
        uint64_t value;

        if (command->kind != MINILD_SCRIPT_DEFINE_SYMBOL ||
            !command_deferred[i]) {
            continue;
        }
        if (command->value.symbol.provide) {
            size_t sym = symbol_resolver_lookup(&state->resolver,
                                            command->value.symbol.name);
            if (sym == SIZE_MAX ||
                state->resolver.entries[sym].section != MINILD_SECTION_UNDEF) {
                continue;
            }
        }
        if (!script_layout_evaluate(&script_state,
                                    command->value.symbol.expression,
                                    &value) ||
            !script_layout_define_symbol(&script_state,
                                         command->value.symbol.name,
                                         value)) {
            goto done;
        }
    }

    for (i = 0U; i < state->work.count; ++i) {
        MiniLdSection *section = &state->work.sections[i];

        if (script_state.section_placed[i] ||
            layout->section_discarded[i]) {
            continue;
        }
        if ((section->flags & SHF_ALLOC) != 0U && section->size != 0U) {
            fprintf(state->diagnostics,
                    "minic-ld: linker-script-unplaced-alloc-section:%s:size=%zu\n",
                    section->name,
                    section->size);
            goto done;
        }
        layout->section_discarded[i] = true;
        script_state.section_placed[i] = true;
    }

    for (i = 0U; i < state->work.count; ++i) {
        MiniLdSection *section = &state->work.sections[i];
        size_t owner = layout->section_owner[i];

        if (layout->section_discarded[i] ||
            (section->flags & SHF_ALLOC) == 0U ||
            section->size == 0U) {
            continue;
        }
        if (owner != SIZE_MAX && owner < script->command_count &&
            section->align > owner_max_align[owner]) {
            owner_max_align[owner] = section->align;
        }
        if (owner != SIZE_MAX && owner < script->command_count) {
            ++owner_remaining[owner];
        }
        ++alloc_count;
        if ((section->flags & SHF_WRITE) != 0U) {
            layout->have_rw = true;
        } else {
            layout->have_rx = true;
        }
    }

    if (alloc_count > (SIZE_MAX - sizeof(Elf64_Ehdr)) / sizeof(Elf64_Phdr) ||
        !align_up_size(sizeof(Elf64_Ehdr) +
                           alloc_count * sizeof(Elf64_Phdr),
                       4096U,
                       &header_size)) {
        fprintf(state->diagnostics,
                "minic-ld: linker-script-header-overflow\n");
        goto done;
    }
    file_cursor = header_size;
    if (alloc_count > SIZE_MAX / sizeof(*file_order)) {
        fprintf(state->diagnostics,
                "minic-ld: linker-script-file-order-overflow\n");
        goto done;
    }
    file_order = malloc((alloc_count == 0U ? 1U : alloc_count) *
                        sizeof(*file_order));
    if (file_order == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:linker-script-file-order\n");
        goto done;
    }
    {
        size_t count = 0U;
        for (i = 0U; i < state->work.count; ++i) {
            const MiniLdSection *section = &state->work.sections[i];
            if (layout->section_discarded[i] ||
                (section->flags & SHF_ALLOC) == 0U || section->size == 0U) {
                continue;
            }
            file_order[count].laddr = layout->section_laddr[i];
            file_order[count].vaddr = layout->section_vaddr[i];
            file_order[count].index = i;
            ++count;
        }
        if (count != alloc_count) {
            fprintf(state->diagnostics,
                    "minic-ld: linker-script-file-order-count-mismatch\n");
            goto done;
        }
    }
    qsort(file_order, alloc_count, sizeof(*file_order),
          minild_compare_file_order);
    for (i = 0U; i < alloc_count; ++i) {
        size_t best = file_order[i].index;
        const MiniLdSection *section = &state->work.sections[best];
        size_t owner = layout->section_owner[best];
        size_t aligned;

        if (!align_up_size(file_cursor,
                           section->align == 0U ? 1U : section->align,
                           &aligned)) {
            fprintf(state->diagnostics,
                    "minic-ld: linker-script-file-layout-overflow:%s\n",
                    section->name);
            goto done;
        }
        if (owner != SIZE_MAX && owner < script->command_count &&
            layout->script_output_file_base[owner] != SIZE_MAX) {
            uint64_t delta;
            size_t expected;

            if (layout->section_vaddr[best] <
                    layout->script_output_start[owner] ||
                layout->section_vaddr[best] -
                    layout->script_output_start[owner] >
                    SIZE_MAX - layout->script_output_file_base[owner]) {
                fprintf(state->diagnostics,
                        "minic-ld: unsupported-script-output-order:%s\n",
                        section->name);
                goto done;
            }
            delta = layout->section_vaddr[best] -
                    layout->script_output_start[owner];
            expected = layout->script_output_file_base[owner] + (size_t)delta;
            /* An ELF output section has one sh_offset and one sh_addr.
             * Every member therefore needs the SAME file/VMA displacement.
             * Preserve script dot holes as real zero-filled file gaps. */
            if (expected < aligned ||
                expected % (section->align == 0U ? 1U : section->align) != 0U) {
                fprintf(state->diagnostics,
                        "minic-ld: unsupported-script-output-file-order:%s\n",
                        section->name);
                goto done;
            }
            file_cursor = expected;
        } else {
            file_cursor = aligned;
            if (owner != SIZE_MAX && owner < script->command_count) {
                /* Later members may be more strictly aligned than the
                 * first. Match the owner's maximum alignment modulo its
                 * VMA now, so later VMA gaps also have correct file gaps. */
                size_t alignment = (size_t)(owner_max_align[owner] == 0U
                                               ? 1U : owner_max_align[owner]);
                uint64_t leading = layout->section_vaddr[best] -
                                   layout->script_output_start[owner];
                size_t wanted = (size_t)(layout->section_vaddr[best] % alignment);
                size_t have;
                size_t minimum;
                if (leading > SIZE_MAX - file_cursor) {
                    fprintf(state->diagnostics,
                            "minic-ld: script-output-prefix-overflow:%s\n",
                            section->name);
                    goto done;
                }
                minimum = file_cursor + (size_t)leading;
                if (minimum > aligned) {
                    file_cursor = minimum;
                }
                have = file_cursor % alignment;
                size_t padding = wanted >= have ? wanted - have
                                                : alignment - (have - wanted);

                if (!add_size(file_cursor, padding, &file_cursor)) {
                    fprintf(state->diagnostics,
                            "minic-ld: script-output-alignment-overflow:%s\n",
                            section->name);
                    goto done;
                }
                layout->script_output_file_base[owner] =
                    file_cursor - (size_t)leading;
            }
        }
        layout->section_file_offset[best] = file_cursor;
        if (section->type != SHT_NOBITS &&
            !add_size(file_cursor, section->size, &file_cursor)) {
            fprintf(state->diagnostics,
                    "minic-ld: linker-script-file-layout-overflow:%s\n",
                    section->name);
            goto done;
        }
        if (owner != SIZE_MAX && owner < script->command_count &&
            --owner_remaining[owner] == 0U && section->type != SHT_NOBITS) {
            uint64_t span = layout->script_output_end[owner] -
                            layout->script_output_start[owner];
            size_t end;
            if (span > SIZE_MAX - layout->script_output_file_base[owner]) {
                fprintf(state->diagnostics,
                        "minic-ld: script-output-tail-overflow:%s\n",
                        section->name);
                goto done;
            }
            end = layout->script_output_file_base[owner] + (size_t)span;
            if (end > file_cursor) {
                file_cursor = end;
            }
        }
    }

    layout->scripted = true;
    layout->script_command_count = script->command_count;
    ok = true;

done:
    free(file_order);
    free(owner_max_align);
    free(owner_remaining);
    free(command_deferred);
    free(script_state.output_defined);
    free(script_state.section_placed);
    if (!ok) {
        free(layout->section_owner);
        free(layout->script_bytes);
        free(layout->script_output_file_base);
        free(layout->script_output_end);
        free(layout->script_output_start);
        free(layout->script_have_fill);
        free(layout->script_fill);
        free(layout->section_discarded);
        free(layout->section_file_offset);
        free(layout->section_laddr);
        free(layout->section_vaddr);
        memset(layout, 0, sizeof(*layout));
    }
    return ok;
}

/* The fixed vDSO ProgramPlan owns four PHDRs, but linker-script output
 * sections still own their GNU :phdr associations.  Validate those
 * associations against the existing segment policy instead of silently
 * ignoring a script's segment assignment.  The active list inherits from
 * the preceding allocated output section unless explicitly replaced.
 *
 * This intentionally does not implement arbitrary PHDR segment layout:
 * unsupported combinations fail closed before ELF serialization. */
static bool shared_validate_vdso_script_phdr_membership(
    const MiniLdState *state,
    const MiniLdScript *script,
    const MiniLdLayout *layout) {
    enum {
        PHDR_TEXT = 1U << 0U,
        PHDR_DYNAMIC = 1U << 1U,
        PHDR_NOTE = 1U << 2U,
        PHDR_EH_FRAME_HDR = 1U << 3U
    };
    const size_t command_count = script->command_count;
    unsigned char *command_masks = NULL;
    bool *command_has_alloc = NULL;
    unsigned char inherited_mask = 0U;
    size_t i;
    bool ok = false;

    command_masks = calloc(command_count == 0U ? 1U : command_count,
                           sizeof(*command_masks));
    command_has_alloc = calloc(command_count == 0U ? 1U : command_count,
                               sizeof(*command_has_alloc));
    if (command_masks == NULL || command_has_alloc == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:shared-script-PHDR-membership\n");
        goto done;
    }

    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *section = &state->work.sections[i];
        size_t owner;

        if (section->gc_discarded || layout->section_discarded[i] ||
            (section->flags & SHF_ALLOC) == 0U || section->size == 0U) {
            continue;
        }
        owner = layout->section_owner[i];
        if (owner >= command_count ||
            script->commands[owner].kind != MINILD_SCRIPT_OUTPUT_SECTION ||
            script->commands[owner].value.section.discard) {
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-shared-script-PHDR-membership:%s\n",
                    section->output_name != NULL
                        ? section->output_name : section->name);
            goto done;
        }
        command_has_alloc[owner] = true;
    }

    for (i = 0U; i < command_count; ++i) {
        const MiniLdScriptCommand *command = &script->commands[i];
        const MiniLdScriptOutputSection *output;
        size_t j;

        if (command->kind != MINILD_SCRIPT_OUTPUT_SECTION ||
            !command_has_alloc[i]) {
            continue;
        }
        output = &command->value.section;
        if (output->phdr_name_count != 0U) {
            inherited_mask = 0U;
            for (j = 0U; j < output->phdr_name_count; ++j) {
                const char *name = output->phdr_names[j];

                if (strcmp(name, "text") == 0) {
                    inherited_mask |= PHDR_TEXT;
                } else if (strcmp(name, "dynamic") == 0) {
                    inherited_mask |= PHDR_DYNAMIC;
                } else if (strcmp(name, "note") == 0) {
                    inherited_mask |= PHDR_NOTE;
                } else if (strcmp(name, "eh_frame_hdr") == 0) {
                    inherited_mask |= PHDR_EH_FRAME_HDR;
                } else {
                    fprintf(state->diagnostics,
                            "minic-ld: unsupported-shared-script-PHDR-membership:%s\n",
                            output->name);
                    goto done;
                }
            }
        }
        command_masks[i] = inherited_mask;
    }

    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *section = &state->work.sections[i];
        unsigned char required_mask = PHDR_TEXT;
        size_t owner;

        if (section->gc_discarded || layout->section_discarded[i] ||
            (section->flags & SHF_ALLOC) == 0U || section->size == 0U) {
            continue;
        }
        owner = layout->section_owner[i];
        if (section->type == SHT_NOTE) {
            required_mask |= PHDR_NOTE;
        }
        if (section->type == SHT_DYNAMIC &&
            strcmp(section->name, ".dynamic") == 0) {
            required_mask |= PHDR_DYNAMIC;
        }
        if (strcmp(section->name, ".eh_frame_hdr") == 0) {
            required_mask |= PHDR_EH_FRAME_HDR;
        }
        if (command_masks[owner] != required_mask) {
            fprintf(state->diagnostics,
                    "minic-ld: unsupported-shared-script-PHDR-membership:%s\n",
                    script->commands[owner].value.section.name);
            goto done;
        }
    }
    ok = true;

done:
    free(command_has_alloc);
    free(command_masks);
    return ok;
}

/* Shared -T uses the proven static output-section and PROVIDE evaluator.
 * In Linux's four-header single-PT_LOAD layout, the PT_LOAD starts at
 * file offset 0 and VMA 0, and every alloc section must lie after the
 * ELF and PHDR tables with file offset identical to its VMA.
 */
static bool shared_build_vdso_script_layout(MiniLdState *state,
                                            const MiniLdScript *script,
                                            MiniLdLayout *layout) {
    size_t headers_size;
    size_t i;
    size_t end;

    if (script->phdr_count != 4U ||
        strcmp(script->phdrs[0].name, "text") != 0 ||
        strcmp(script->phdrs[1].name, "dynamic") != 0 ||
        strcmp(script->phdrs[2].name, "note") != 0 ||
        strcmp(script->phdrs[3].name, "eh_frame_hdr") != 0 ||
        script->phdrs[1].flags != 4U ||
        !script->phdrs[1].have_flags ||
        script->phdrs[2].flags != 4U ||
        !script->phdrs[2].have_flags ||
        strcmp(script->phdrs[0].type_name, "PT_LOAD") != 0 ||
        !script->phdrs[0].filehdr || !script->phdrs[0].phdrs ||
        !script->phdrs[0].have_flags || script->phdrs[0].flags != 5U ||
        strcmp(script->phdrs[1].type_name, "PT_DYNAMIC") != 0 ||
        strcmp(script->phdrs[2].type_name, "PT_NOTE") != 0 ||
        strcmp(script->phdrs[3].type_name, "PT_GNU_EH_FRAME") != 0) {
        fprintf(state->diagnostics,
                "minic-ld: unsupported-shared-script-PHDRS-layout\n");
        return false;
    }
    headers_size = sizeof(Elf64_Ehdr) +
                   script->phdr_count * sizeof(Elf64_Phdr);
    if (!static_build_script_layout(state, script, layout, headers_size)) {
        return false;
    }
    if (!shared_validate_vdso_script_phdr_membership(state, script, layout)) {
        return false;
    }
    end = headers_size;
    for (i = 0U; i < state->work.count; ++i) {
        MiniLdSection *section = &state->work.sections[i];
        uint64_t address;
        if (layout->section_discarded[i] ||
            (section->flags & SHF_ALLOC) == 0U ||
            section->size == 0U) {
            continue;
        }
        address = layout->section_vaddr[i];
        if (address < headers_size ||
            address > SIZE_MAX ||
            section->size > SIZE_MAX - (size_t)address) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-vdso-script-file-placement:%s\n",
                    section->name);
            return false;
        }
        layout->section_file_offset[i] = (size_t)address;
        if (section->type != SHT_NOBITS &&
            end < (size_t)address + section->size) {
            end = (size_t)address + section->size;
        }
    }
    /* vDSO PT_LOAD includes FILEHDR/PHDRS: its file and virtual bases are
     * both zero. The script allocator's temporary packed file bases are
     * replaced above, so the output owner bases must follow suit. */
    for (i = 0U; i < layout->script_command_count; ++i) {
        if (layout->script_output_file_base[i] != SIZE_MAX) {
            if (layout->script_output_start[i] > SIZE_MAX) {
                fprintf(state->diagnostics,
                        "minic-ld: vdso-output-base-overflow\n");
                return false;
            }
            layout->script_output_file_base[i] =
                (size_t)layout->script_output_start[i];
        }
    }
    layout->have_rx = true;
    layout->have_rw = false;
    layout->rx_file_offset = 0U;
    layout->rx_vaddr = 0U;
    layout->rx_file_size = end;
    return true;
}

static bool static_section_has_prefix(const char *name, const char *prefix) {
    size_t length = strlen(prefix);

    return strcmp(name, prefix) == 0 ||
           (strncmp(name, prefix, length) == 0 && name[length] == '.');
}

static bool static_define_absolute_if_undefined(MiniLdState *state,
                                                const char *name,
                                                uint64_t value) {
    return symbol_resolver_provide_absolute(&state->resolver,
                                             state->diagnostics,
                                             name, value);
}

static bool static_synthesize_array_bound_pair(
    MiniLdState *state,
    const MiniLdLayout *layout,
    const char *section_prefix,
    const char *start_name,
    const char *end_name) {
    uint64_t start = UINT64_MAX;
    uint64_t end = 0U;
    bool found = false;
    size_t i;

    for (i = 0U; i < state->work.count; ++i) {
        MiniLdSection *section = &state->work.sections[i];
        uint64_t section_start;
        uint64_t section_end;

        if ((section->flags & SHF_ALLOC) == 0U ||
            (layout->section_discarded != NULL &&
             layout->section_discarded[i]) ||
            !static_section_has_prefix(section->name, section_prefix)) {
            continue;
        }
        section_start = layout->section_vaddr[i];
        section_end = section_start + (uint64_t)section->size;
        if (!found || section_start < start) {
            start = section_start;
        }
        if (!found || section_end > end) {
            end = section_end;
        }
        found = true;
    }

    if (!found) {
        /*
         * Match the old Python linker: absent constructor/destructor arrays
         * are represented by a zero-length range at the end of the load image.
         */
        if (layout->scripted) {
            start = 0U;
            for (i = 0U; i < state->work.count; ++i) {
                MiniLdSection *section = &state->work.sections[i];
                uint64_t candidate;

                if ((section->flags & SHF_ALLOC) == 0U ||
                    layout->section_discarded[i]) {
                    continue;
                }
                candidate = layout->section_vaddr[i] +
                            (uint64_t)section->size;
                if (candidate > start) {
                    start = candidate;
                }
            }
        } else {
            start = layout->rw_vaddr + (uint64_t)layout->rw_mem_size;
        }
        end = start;
    }
    return static_define_absolute_if_undefined(state, start_name, start) &&
           static_define_absolute_if_undefined(state, end_name, end);
}


static bool static_resolve_symbol(MiniLdState *state,
                                  const MiniLdLayout *layout,
                                  size_t symbol_index,
                                  uint64_t *value_out);

static bool static_fill_got(MiniLdState *state,
                            const MiniLdLayout *layout,
                            const MiniLdStaticGot *got) {
    MiniLdSection *section;
    size_t i;

    if (!got->present) {
        return true;
    }
    if (got->section >= state->work.count) {
        fprintf(state->diagnostics, "minic-ld: invalid-static-got-section\n");
        return false;
    }
    section = &state->work.sections[got->section];

    for (i = 0U; i < got->symbol_count; ++i) {
        uint64_t value;

        if (got->slot_offsets[i] == SIZE_MAX) {
            continue;
        }
        if (!static_resolve_symbol(state, layout, i, &value) ||
            !range_ok(got->slot_offsets[i], 8U, section->size)) {
            return false;
        }
        store_u64le(section->data + got->slot_offsets[i], value);
    }
    return true;
}

/*
 * GNU's unscripted RISC-V ELF64 default linker script PROVIDEs data/BSS
 * range markers and __global_pointer$. These are NOT Linux-specific names.
 * Derive the values from MiniLD's own placed alloc sections; never reuse
 * GNU's absolute addresses, since relaxation changes our section layout.
 * Explicit-script vmlinux links retain the old behavior unchanged.
 */
static bool static_synthesize_gnu_default_boundaries(
    MiniLdState *state,
    const MiniLdLayout *layout) {
    uint64_t end = layout->have_rw
                       ? layout->rw_vaddr + (uint64_t)layout->rw_mem_size
                       : layout->rx_vaddr + (uint64_t)layout->rx_file_size;
    uint64_t data_begin = UINT64_MAX;
    uint64_t sdata_begin = UINT64_MAX;
    uint64_t bss_begin = UINT64_MAX;
    uint64_t bss_end = 0U;
    uint64_t global_pointer;
    size_t i;

    if (layout->scripted) {
        return true;
    }
    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *section = &state->work.sections[i];
        uint64_t address;
        uint64_t section_end;

        if ((section->flags & SHF_ALLOC) == 0U ||
            (layout->section_discarded != NULL &&
             layout->section_discarded[i])) {
            continue;
        }
        address = layout->section_vaddr[i];
        if ((uint64_t)section->size > UINT64_MAX - address) {
            fprintf(state->diagnostics,
                    "minic-ld: default-symbol-address-overflow\n");
            return false;
        }
        section_end = address + (uint64_t)section->size;
        if ((section->flags & SHF_WRITE) != 0U &&
            address < data_begin) {
            data_begin = address;
        }
        if ((static_section_has_prefix(section->name, ".sdata") ||
             static_section_has_prefix(section->name, ".srodata")) &&
            address < sdata_begin) {
            sdata_begin = address;
        }
        if (section->type == SHT_NOBITS &&
            (section->flags & SHF_WRITE) != 0U) {
            if (address < bss_begin) {
                bss_begin = address;
            }
            if (section_end > bss_end) {
                bss_end = section_end;
            }
        }
        if (section_end > end) {
            end = section_end;
        }
    }
    if (data_begin == UINT64_MAX) {
        data_begin = end;
    }
    if (sdata_begin == UINT64_MAX) {
        sdata_begin = data_begin;
    }
    if (bss_begin == UINT64_MAX) {
        bss_begin = end;
        bss_end = end;
    }
    if (sdata_begin > UINT64_MAX - UINT64_C(0x800) ||
        data_begin > UINT64_MAX - UINT64_C(0x800)) {
        fprintf(state->diagnostics,
                "minic-ld: default-global-pointer-overflow\n");
        return false;
    }
    /* GNU RISC-V default linker script:
     * MIN(__SDATA_BEGIN__ + 0x800,
     *     MAX(__DATA_BEGIN__ + 0x800, __BSS_END__ - 0x800)).
     * Saturate subtraction when the image is unusually small. */
    global_pointer = data_begin + UINT64_C(0x800);
    if (bss_end >= UINT64_C(0x800) &&
        bss_end - UINT64_C(0x800) > global_pointer) {
        global_pointer = bss_end - UINT64_C(0x800);
    }
    if (sdata_begin + UINT64_C(0x800) < global_pointer) {
        global_pointer = sdata_begin + UINT64_C(0x800);
    }
    return static_define_absolute_if_undefined(
               state, "__DATA_BEGIN__", data_begin) &&
           static_define_absolute_if_undefined(
               state, "__SDATA_BEGIN__", sdata_begin) &&
           static_define_absolute_if_undefined(
               state, "__BSS_END__", bss_end) &&
           static_define_absolute_if_undefined(
               state, "__bss_start", bss_begin) &&
           static_define_absolute_if_undefined(
               state, "__global_pointer$", global_pointer) &&
           static_define_absolute_if_undefined(
               state, "_edata", bss_begin) &&
           static_define_absolute_if_undefined(
               state, "_end", end);
}

static bool static_need_array_bound_pair(
    const MiniLdState *state,
    const MiniLdLayout *layout,
    const char *section_prefix,
    const char *start_name,
    const char *end_name) {
    size_t i;

    /* Preserve the already certified scripted Linux path exactly. */
    if (layout->scripted ||
        symbol_resolver_lookup(&state->resolver, start_name) != SIZE_MAX ||
        symbol_resolver_lookup(&state->resolver, end_name) != SIZE_MAX) {
        return true;
    }
    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *section = &state->work.sections[i];
        if ((section->flags & SHF_ALLOC) != 0U &&
            (layout->section_discarded == NULL ||
             !layout->section_discarded[i]) &&
            static_section_has_prefix(section->name, section_prefix)) {
            return true;
        }
    }
    /* GNU does not export unused, absent array markers in a plain ET_EXEC. */
    return false;
}

static bool static_synthesize_runtime_boundaries(
    MiniLdState *state,
    const MiniLdLayout *layout) {
    return static_synthesize_gnu_default_boundaries(state, layout) &&
           (!static_need_array_bound_pair(state, layout, ".preinit_array",
                                          "__preinit_array_start",
                                          "__preinit_array_end") ||
            static_synthesize_array_bound_pair(state, layout,
                                                ".preinit_array",
                                                "__preinit_array_start",
                                                "__preinit_array_end")) &&
           (!static_need_array_bound_pair(state, layout, ".init_array",
                                          "__init_array_start",
                                          "__init_array_end") ||
            static_synthesize_array_bound_pair(state, layout,
                                                ".init_array",
                                                "__init_array_start",
                                                "__init_array_end")) &&
           (!static_need_array_bound_pair(state, layout, ".fini_array",
                                          "__fini_array_start",
                                          "__fini_array_end") ||
            static_synthesize_array_bound_pair(state, layout,
                                                ".fini_array",
                                                "__fini_array_start",
                                                "__fini_array_end"));
}

static void minild_layout_destroy(MiniLdLayout *layout) {
    free(layout->section_vaddr);
    free(layout->section_laddr);
    free(layout->section_file_offset);
    free(layout->section_discarded);
    free(layout->section_owner);
    free(layout->script_fill);
    free(layout->script_have_fill);
    free(layout->script_bytes);
    free(layout->script_output_file_base);
    free(layout->script_output_end);
    free(layout->script_output_start);
    layout->section_vaddr = NULL;
    layout->section_laddr = NULL;
    layout->section_file_offset = NULL;
    layout->section_discarded = NULL;
    layout->section_owner = NULL;
    layout->script_fill = NULL;
    layout->script_have_fill = NULL;
    layout->script_bytes = NULL;
    layout->script_output_file_base = NULL;
    layout->script_output_end = NULL;
    layout->script_output_start = NULL;
    layout->script_command_count = 0U;
}

static bool static_resolve_symbol(MiniLdState *state,
                                  const MiniLdLayout *layout,
                                  size_t symbol_index,
                                  uint64_t *value_out) {
    MiniLdResolvedSymbol *symbol;

    if (symbol_index >= state->resolver.count) {
        fprintf(state->diagnostics, "minic-ld: relocation-symbol-out-of-range\n");
        return false;
    }
    symbol = &state->resolver.entries[symbol_index];
    if (symbol->section == MINILD_SECTION_UNDEF) {
        if (ELF64_ST_BIND(symbol->info) == STB_WEAK) {
            *value_out = 0U;
            return true;
        }
        fprintf(state->diagnostics,
                "minic-ld: undefined-reference:%s\n",
                symbol->name);
        return false;
    }
    if (symbol->section == MINILD_SECTION_ABS) {
        *value_out = symbol->value;
        return true;
    }
    if (symbol->section < 0 ||
        (size_t)symbol->section >= state->work.count ||
        (state->work.sections[symbol->section].flags & SHF_ALLOC) == 0U ||
        (layout->section_discarded != NULL &&
         layout->section_discarded[symbol->section])) {
        fprintf(state->diagnostics,
                "minic-ld: unsupported-static-symbol:%s\n",
                symbol->name);
        return false;
    }
    *value_out = layout->section_vaddr[symbol->section] + symbol->value;
    return true;
}

static size_t pcrel_capacity_for(size_t relocation_count) {
    size_t capacity = 64U;
    size_t target;

    if (relocation_count > (SIZE_MAX - 1U) / 2U) {
        return 0U;
    }
    target = relocation_count * 2U + 1U;
    while (capacity < target) {
        if (capacity > SIZE_MAX / 2U) {
            return 0U;
        }
        capacity *= 2U;
    }
    return capacity;
}

static size_t pcrel_hash(size_t section, uint64_t offset, size_t capacity) {
    uint64_t hash = offset ^ (UINT64_C(0x9e3779b97f4a7c15) *
                              (uint64_t)(section + 1U));
    hash ^= hash >> 33U;
    hash *= UINT64_C(0xff51afd7ed558ccd);
    hash ^= hash >> 33U;
    return (size_t)hash & (capacity - 1U);
}

static void pcrel_insert(MiniLdPcrelSlot *slots,
                         size_t capacity,
                         size_t section,
                         uint64_t offset,
                         int64_t delta) {
    size_t position = pcrel_hash(section, offset, capacity);

    while (slots[position].section_plus_one != 0U) {
        position = (position + 1U) & (capacity - 1U);
    }
    slots[position].section_plus_one = section + 1U;
    slots[position].offset = offset;
    slots[position].delta = delta;
}

static bool pcrel_find(const MiniLdPcrelSlot *slots,
                       size_t capacity,
                       size_t section,
                       uint64_t offset,
                       int64_t *delta_out) {
    size_t position = pcrel_hash(section, offset, capacity);

    while (slots[position].section_plus_one != 0U) {
        if (slots[position].section_plus_one == section + 1U &&
            slots[position].offset == offset) {
            *delta_out = slots[position].delta;
            return true;
        }
        position = (position + 1U) & (capacity - 1U);
    }
    return false;
}

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

        case R_RISCV_ADD8:
        case R_RISCV_SUB8: {
            uint8_t current;
            uint8_t operand;

            if (section->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
                !range_ok((size_t)reloc->offset, 1U, section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: R_RISCV_ADD_SUB8-offset-out-of-range\n");
                free(pcrel);
                return false;
            }
            current = load_u8(section->data + (size_t)reloc->offset);
            operand = (uint8_t)(uint64_t)target;
            if (reloc->type == R_RISCV_ADD8) {
                current = (uint8_t)(current + operand);
            } else {
                current = (uint8_t)(current - operand);
            }
            store_u8(section->data + (size_t)reloc->offset, current);
            break;
        }
        case R_RISCV_SUB6: {
            uint8_t current;
            uint8_t low6;
            uint8_t operand;

            if (section->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
                !range_ok((size_t)reloc->offset, 1U, section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: R_RISCV_SUB6-offset-out-of-range\n");
                free(pcrel);
                return false;
            }
            current = load_u8(section->data + (size_t)reloc->offset);
            low6 = (uint8_t)(current & UINT8_C(0x3f));
            operand = (uint8_t)((uint64_t)target & UINT64_C(0x3f));
            low6 = (uint8_t)((low6 - operand) & UINT8_C(0x3f));
            current = (uint8_t)((current & UINT8_C(0xc0)) | low6);
            store_u8(section->data + (size_t)reloc->offset, current);
            break;
        }
        case R_RISCV_SET6: {
            uint8_t current;
            uint8_t encoded;

            if (section->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
                !range_ok((size_t)reloc->offset, 1U, section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: R_RISCV_SET6-offset-out-of-range\n");
                free(pcrel);
                return false;
            }
            current = load_u8(section->data + (size_t)reloc->offset);
            encoded = (uint8_t)((uint64_t)target & UINT64_C(0x3f));
            current = (uint8_t)((current & UINT8_C(0xc0)) | encoded);
            store_u8(section->data + (size_t)reloc->offset, current);
            break;
        }
        case R_RISCV_SET8:
            if (section->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
                !range_ok((size_t)reloc->offset, 1U, section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: R_RISCV_SET8-offset-out-of-range\n");
                free(pcrel);
                return false;
            }
            store_u8(section->data + (size_t)reloc->offset,
                     (uint8_t)(uint64_t)target);
            break;
        case R_RISCV_SET16:
            if (section->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
                !range_ok((size_t)reloc->offset, 2U, section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: R_RISCV_SET16-offset-out-of-range\n");
                free(pcrel);
                return false;
            }
            store_u16le(section->data + (size_t)reloc->offset,
                        (uint16_t)(uint64_t)target);
            break;
        case R_RISCV_SET32:
            if (section->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
                !range_ok((size_t)reloc->offset, 4U, section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: R_RISCV_SET32-offset-out-of-range\n");
                free(pcrel);
                return false;
            }
            store_u32le(section->data + (size_t)reloc->offset,
                        (uint32_t)(uint64_t)target);
            break;
        case R_RISCV_ADD16:
        case R_RISCV_SUB16: {
            uint16_t current;
            uint16_t operand;

            if (section->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
                !range_ok((size_t)reloc->offset, 2U, section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: R_RISCV_ADD_SUB16-offset-out-of-range\n");
                free(pcrel);
                return false;
            }
            current = load_u16le(section->data + (size_t)reloc->offset);
            operand = (uint16_t)(uint64_t)target;
            if (reloc->type == R_RISCV_ADD16) {
                current = (uint16_t)(current + operand);
            } else {
                current = (uint16_t)(current - operand);
            }
            store_u16le(section->data + (size_t)reloc->offset, current);
            break;
        }
        case R_RISCV_ADD32:
        case R_RISCV_SUB32: {
            uint32_t current;
            uint32_t operand;

            if (section->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
                !range_ok((size_t)reloc->offset, 4U, section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: R_RISCV_ADD_SUB32-offset-out-of-range\n");
                free(pcrel);
                return false;
            }
            current = load_u32le(section->data + (size_t)reloc->offset);
            operand = (uint32_t)(uint64_t)target;
            if (reloc->type == R_RISCV_ADD32) {
                current += operand;
            } else {
                current -= operand;
            }
            store_u32le(section->data + (size_t)reloc->offset, current);
            break;
        }
        case R_RISCV_ADD64:
        case R_RISCV_SUB64: {
            uint64_t current = 0U;
            uint64_t operand = (uint64_t)target;

            if (section->type == SHT_NOBITS || reloc->offset > SIZE_MAX ||
                !range_ok((size_t)reloc->offset, 8U, section->size)) {
                fprintf(state->diagnostics,
                        "minic-ld: R_RISCV_ADD_SUB64-offset-out-of-range\n");
                free(pcrel);
                return false;
            }
            current = load_u64le(section->data + (size_t)reloc->offset);
            if (reloc->type == R_RISCV_ADD64) {
                current += operand;
            } else {
                current -= operand;
            }
            store_u64le(section->data + (size_t)reloc->offset, current);
            break;
        }
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

static bool static_entry_address(MiniLdState *state,
                                 const MiniLdLayout *layout,
                                 const char *entry_symbol,
                                 uint64_t *entry_out) {
    size_t index = symbol_resolver_lookup(&state->resolver, entry_symbol);

    if (index == SIZE_MAX) {
        fprintf(state->diagnostics,
                "minic-ld: undefined-entry:%s\n",
                entry_symbol);
        return false;
    }
    return static_resolve_symbol(state, layout, index, entry_out);
}
