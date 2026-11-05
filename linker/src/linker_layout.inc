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
    uint64_t fill;
    unsigned fill_width;
    bool have_fill;
    bool defined;
    /* GNU (INFO): non-alloc section with a meaningful script VMA. */
    bool informational;
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

/* Linker-script holes use a repeating big-endian literal byte sequence,
 * regardless of the selected ELF target's data encoding. */
static unsigned minild_script_fill_width(uint64_t value) {
    unsigned width = 1U;
    while (width < 8U && (value >> (width * 8U)) != 0U) {
        ++width;
    }
    return width;
}

static void minild_script_fill_bytes(unsigned char *bytes, size_t count,
                                     uint64_t value, unsigned width,
                                     size_t offset) {
    size_t i;
    for (i = 0U; i < count; ++i) {
        unsigned index = (unsigned)((offset + i) % width);
        bytes[i] = (unsigned char)(
            (value >> ((width - 1U - index) * 8U)) & UINT64_C(0xff));
    }
}

/* The surviving TLS block has one layout identity for ET_EXEC fixups,
 * .symtab values and PT_TLS.  Do not let targets independently infer where
 * the TLS image starts or ends: x86 TPOFF, for example, must describe the
 * same block that the runtime will install from PT_TLS. */
typedef struct MiniLdTlsProjection {
    bool has_surviving_section;
    bool has_nonempty_section;
    bool has_file_contents;
    bool valid_extents;
    uint64_t block_base;
    uint64_t memory_end;
    uint64_t file_end;
    uint64_t alignment;
} MiniLdTlsProjection;

static void minild_layout_project_tls(const MiniLdState *state,
                                      const MiniLdLayout *layout,
                                      MiniLdTlsProjection *tls) {
    size_t i;

    memset(tls, 0, sizeof(*tls));
    tls->valid_extents = true;
    tls->alignment = 1U;
    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *section = &state->work.sections[i];
        uint64_t start;
        uint64_t end;

        if ((section->flags & (SHF_ALLOC | SHF_TLS)) !=
                (SHF_ALLOC | SHF_TLS) || section->gc_discarded ||
            (layout->scripted && layout->work[i].discarded)) {
            continue;
        }
        start = layout->work[i].vaddr;
        if (!tls->has_surviving_section || start < tls->block_base) {
            tls->block_base = start;
        }
        tls->has_surviving_section = true;
        tls->has_nonempty_section |= section->size != 0U;
        if (section->align > tls->alignment) {
            tls->alignment = section->align;
        }
        if (section->size > UINT64_MAX - start) {
            tls->valid_extents = false;
            continue;
        }
        end = start + (uint64_t)section->size;
        if (end > tls->memory_end) {
            tls->memory_end = end;
        }
        if (section->type != SHT_NOBITS && section->size != 0U) {
            if (layout->work[i].file_offset == SIZE_MAX) {
                tls->valid_extents = false;
            }
            if (!tls->has_file_contents || end > tls->file_end) {
                tls->file_end = end;
            }
            tls->has_file_contents = true;
        }
    }
}

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

#include "linker_align_relax_common.inc"

#include "linker_riscv_align_relax.inc"

#include "linker_loongarch_align_relax.inc"

/* Relaxation is a target machine-byte policy, never a generic relocation
 * number switch. Other targets retain their bytes until their own hook is
 * installed; an unsupported final relocation still fails closed below. */
static bool minild_target_relax_section(MiniLdState *state,
                                        size_t section_index,
                                        uint64_t address) {
    return state->target->relax_section == NULL ||
           state->target->relax_section(state, section_index, address);
}

/* The CRT walks every pointer between __init_array_start/end (and the
 * corresponding preinit/fini symbols).  Grouping only the symbols while
 * leaving input sections interleaved with ordinary .data turns unrelated
 * data into constructor pointers.  GNU BFD first groups .init_array.* and
 * .fini_array.* by numeric priority, followed by the unsuffixed section. */
typedef struct {
    size_t index;
    unsigned bucket;
    uint32_t priority;
} MiniLdStaticRwOrder;

static bool static_section_has_prefix(const char *name, const char *prefix);

static uint32_t static_array_priority(const char *name, const char *prefix) {
    const char *suffix = name + strlen(prefix);
    uint32_t value = 0U;

    if (*suffix++ != '.' || *suffix == '\0') {
        return UINT32_MAX;
    }
    for (; *suffix != '\0'; ++suffix) {
        unsigned digit;
        if (*suffix < '0' || *suffix > '9') {
            return UINT32_MAX;
        }
        digit = (unsigned)(*suffix - '0');
        if (value > (UINT32_MAX - digit) / 10U) {
            return UINT32_MAX;
        }
        value = value * 10U + digit;
    }
    return value;
}

static unsigned static_rw_section_bucket(const MiniLdSection *section) {
    const char *name = section->input_name != NULL
                           ? section->input_name : section->name;

    if (section->type == SHT_NOBITS) {
        return (section->flags & SHF_TLS) != 0U ? 5U : 6U;
    }
    if ((section->flags & SHF_TLS) != 0U) {
        return 4U;
    }
    if (static_section_has_prefix(name, ".preinit_array")) {
        return 1U;
    }
    if (static_section_has_prefix(name, ".init_array")) {
        return 2U;
    }
    if (static_section_has_prefix(name, ".fini_array")) {
        return 3U;
    }
    return 0U;
}

static int static_compare_rw_order(const void *left, const void *right) {
    const MiniLdStaticRwOrder *a = left;
    const MiniLdStaticRwOrder *b = right;

    if (a->bucket != b->bucket) {
        return a->bucket < b->bucket ? -1 : 1;
    }
    if (a->priority != b->priority) {
        return a->priority < b->priority ? -1 : 1;
    }
    return a->index < b->index ? -1 : a->index > b->index ? 1 : 0;
}

static bool static_place_rw_sections(MiniLdState *state,
                                     MiniLdLayout *layout) {
    MiniLdStaticRwOrder *order;
    size_t count = 0U;
    size_t rw_mem_cursor = 0U;
    size_t rw_file_cursor = 0U;
    size_t i;
    bool success = false;

    order = calloc(state->work.count == 0U ? 1U : state->work.count,
                   sizeof(*order));
    if (order == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:static-rw-order\n");
        return false;
    }
    for (i = 0U; i < state->work.count; ++i) {
        const MiniLdSection *section = &state->work.sections[i];
        const char *name = section->input_name != NULL
                               ? section->input_name : section->name;
        MiniLdStaticRwOrder *entry;

        if ((section->flags & (SHF_ALLOC | SHF_WRITE)) !=
            (SHF_ALLOC | SHF_WRITE) || section->gc_discarded) {
            continue;
        }
        entry = &order[count++];
        entry->index = i;
        entry->bucket = static_rw_section_bucket(section);
        entry->priority = UINT32_MAX;
        if (entry->bucket == 1U) {
            entry->priority = static_array_priority(name,
                                                    ".preinit_array");
        } else if (entry->bucket == 2U) {
            entry->priority = static_array_priority(name,
                                                    ".init_array");
        } else if (entry->bucket == 3U) {
            entry->priority = static_array_priority(name,
                                                    ".fini_array");
        }
    }
    qsort(order, count, sizeof(*order), static_compare_rw_order);

    /* Keep the initialized TLS block last, TLS NOBITS immediately after it,
     * and ordinary NOBITS last. The buckets also make every array contiguous
     * in both the memory image and the file image. */
    for (i = 0U; i < count; ++i) {
        size_t index = order[i].index;
        MiniLdSection *section = &state->work.sections[index];
        bool is_nobits = section->type == SHT_NOBITS;
        size_t aligned;

        if (!align_up_size(rw_mem_cursor, section->align, &aligned)) {
            fprintf(state->diagnostics, "minic-ld: static-layout-overflow\n");
            goto done;
        }
        rw_mem_cursor = aligned;
        if (!is_nobits) {
            rw_file_cursor = aligned;
        }
        layout->work[index].vaddr = layout->rw_vaddr + rw_mem_cursor;
        layout->work[index].laddr = layout->work[index].vaddr;
        if (!is_nobits) {
            layout->work[index].file_offset =
                layout->rw_file_offset + rw_file_cursor;
        }
        if (!minild_target_relax_section(state, index,
                                         layout->work[index].vaddr)) {
            goto done;
        }
        if (!add_size(rw_mem_cursor, section->size, &rw_mem_cursor) ||
            (!is_nobits &&
             !add_size(rw_file_cursor, section->size, &rw_file_cursor))) {
            fprintf(state->diagnostics, "minic-ld: static-layout-overflow\n");
            goto done;
        }
        layout->have_rw = true;
    }
    layout->rw_file_size = rw_file_cursor;
    layout->rw_mem_size = rw_mem_cursor;
    success = true;
done:
    free(order);
    return success;
}

static bool static_build_layout_at(MiniLdState *state,
                                   MiniLdLayout *layout,
                                   uint64_t base_vaddr) {
    const size_t page = minild_static_page_size(state);
    const size_t header_page = minild_static_page_size(state);
    size_t rx_cursor = 0U;
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
            (section->flags & SHF_WRITE) != 0U ||
            section->gc_discarded) {
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

    return static_place_rw_sections(state, layout) && layout->have_rx;
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
