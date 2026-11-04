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

#include "linker_target_reloc_semantics.inc"

#include "linker_riscv_fixup.inc"

#include "linker_static_got.inc"

#include "linker_riscv_align_relax.inc"

/* Relaxation is a target machine-byte policy, never a generic relocation
 * number switch. Other targets retain their bytes until their own hook is
 * installed; an unsupported final relocation still fails closed below. */
static bool minild_target_relax_section(MiniLdState *state,
                                        size_t section_index,
                                        uint64_t address) {
    return state->target->relax_section == NULL ||
           state->target->relax_section(state, section_index, address);
}

static bool static_build_layout_at(MiniLdState *state,
                                   MiniLdLayout *layout,
                                   uint64_t base_vaddr) {
    const size_t page = state->target->page_size;
    const size_t header_page = state->target->page_size;
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
        if (!minild_target_relax_section(state,
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
        if (!minild_target_relax_section(state,
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
    return static_build_layout_at(state, layout,
                                  state->target->static_image_base);
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
