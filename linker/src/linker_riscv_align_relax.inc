/* RISC-V R_RISCV_ALIGN relaxation and final section-offset mapping.
 * This mutates canonical work bytes, relocation offsets and resolved symbols
 * at the exact placement point; generic Layout only supplies section VMA. */

typedef struct MiniLdAlignSite {
    size_t offset;
    size_t max_padding;
} MiniLdAlignSite;

typedef struct MiniLdRelaxEvent {
    size_t raw_end;
    size_t cumulative_deleted;
} MiniLdRelaxEvent;

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

/* Both physical and linker-script layout request target relaxation only
 * after section placement has established the section VMA. */
static bool minild_target_relax_section(MiniLdState *state,
                                       size_t section_index,
                                       uint64_t base_addr) {
    return static_relax_align_section(state, section_index, base_addr);
}
