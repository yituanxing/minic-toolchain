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

/* One mutable physical placement for each working section.  Input-section
 * identity lives in InputGraph/WorkingSections; layout owns only addresses,
 * file placement, script command membership and discard decisions. */
typedef struct MiniLdLayoutWorkPlacement {
    uint64_t vaddr;
    uint64_t laddr;
    size_t file_offset;
    size_t owner_command;
    bool discarded;
} MiniLdLayoutWorkPlacement;

/* One linker-script output-command placement, regardless of how many input
 * fragments it selects.  The OutputPlan later projects these into final ELF
 * OutputSections, without retaining redundant per-field owner arrays. */
typedef struct MiniLdLayoutScriptOutput {
    /* Borrowed from the parsed script; never owned by a working section. */
    const char *name;
    uint64_t start;
    uint64_t end;
    size_t file_base;
    unsigned char fill;
    bool have_fill;
    bool defined;
} MiniLdLayoutScriptOutput;

typedef struct MiniLdLayout {
    MiniLdLayoutWorkPlacement *work;
    MiniLdLayoutScriptOutput *script_outputs;
    size_t script_command_count;
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

#include "linker_riscv_fixup.inc"


static void static_got_destroy(MiniLdStaticGot *got) {
    free(got->slot_offsets);
    got->slot_offsets = NULL;
    got->symbol_count = 0U;
    got->section = SIZE_MAX;
    got->present = false;
}

#include "linker_static_reloc_scan.inc"


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
    layout->work =
        calloc(state->work.count == 0U ? 1U : state->work.count,
               sizeof(*layout->work));
    if (layout->work == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:static-layout\n");
        return false;
    }
    for (i = 0U; i < state->work.count; ++i) {
        layout->work[i].file_offset = SIZE_MAX;
        layout->work[i].owner_command = SIZE_MAX;
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
        layout->work[i].vaddr = layout->rx_vaddr + rx_cursor;
        layout->work[i].laddr = layout->work[i].vaddr;
        layout->work[i].file_offset = layout->rx_file_offset + rx_cursor;
        if (!static_relax_align_section(state,
                                        i,
                                        layout->work[i].vaddr)) {
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
        layout->work[i].vaddr = layout->rw_vaddr + rw_mem_cursor;
        layout->work[i].laddr = layout->work[i].vaddr;
        layout->work[i].file_offset =
            layout->rw_file_offset + rw_file_cursor;
        if (!static_relax_align_section(state,
                                        i,
                                        layout->work[i].vaddr)) {
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
        layout->work[i].vaddr = layout->rw_vaddr + rw_mem_cursor;
        layout->work[i].laddr = layout->work[i].vaddr;
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


/* A top-level location-counter definition carries both its evaluated VMA
 * and its output-section provenance.  Defer the section-index binding until
 * all output fragments exist; forward `foo = .` before the first output is
 * then resolved against that output's real work-section anchor. */
#include "linker_script_layout.inc"

#include "linker_vdso_layout.inc"

#include "linker_layout_finalize.inc"
