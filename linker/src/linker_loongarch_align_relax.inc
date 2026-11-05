/* LoongArch R_LARCH_ALIGN relaxation during physical section placement.
 * GNU assemblers reserve architectural NOPs before each alignment boundary;
 * only the bytes needed at the final VMA survive the link.  Shrinking must
 * adjust every symbol and relocation in this work section before static
 * relocation resolution.  Common owns offset projection, this Target owns
 * the R_LARCH_ALIGN encoding, NOP validation and shrink policy. */

typedef struct MiniLdLoongAlignSite {
    size_t offset;
    size_t reserved;
    size_t alignment;
    size_t skip_limit;
    size_t relocation_index;
} MiniLdLoongAlignSite;

static int minild_loong_align_site_compare(
    const void *left, const void *right) {
    const MiniLdLoongAlignSite *a = left;
    const MiniLdLoongAlignSite *b = right;

    if (a->offset < b->offset) {
        return -1;
    }
    if (a->offset > b->offset) {
        return 1;
    }
    return 0;
}

static bool minild_loong_align_decode(
    const MiniLdReloc *reloc, const MiniLdSection *section,
    MiniLdLoongAlignSite *site) {
    uint64_t bytes, alignment, skip_limit = 0U;

    if (section->type != SHT_PROGBITS ||
        (section->flags & (SHF_ALLOC | SHF_EXECINSTR)) !=
            (SHF_ALLOC | SHF_EXECINSTR) ||
        reloc->offset > SIZE_MAX || reloc->addend < 0) {
        return false;
    }
    if (reloc->symbol != SIZE_MAX) {
        unsigned exponent = (unsigned)((uint64_t)reloc->addend & 0xffU);
        skip_limit = (uint64_t)reloc->addend >> 8U;

        if (exponent < 2U || exponent > 20U) {
            return false;
        }
        alignment = UINT64_C(1) << exponent;
        bytes = alignment - 4U;
    } else {
        bytes = (uint64_t)reloc->addend;
        if (bytes > UINT64_C(0x100000) || (bytes & 3U) != 0U) {
            return false;
        }
        alignment = 4U;
        while (alignment <= bytes) {
            alignment <<= 1U;
        }
    }
    if (bytes > SIZE_MAX || alignment > SIZE_MAX ||
        skip_limit > SIZE_MAX ||
        !range_ok((size_t)reloc->offset, (size_t)bytes, section->size)) {
        return false;
    }
    site->offset = (size_t)reloc->offset;
    site->reserved = (size_t)bytes;
    site->alignment = (size_t)alignment;
    site->skip_limit = (size_t)skip_limit;
    return true;
}

/* Offsets strictly inside a now-removed NOP tail have no surviving byte
 * address.  Do not guess a replacement address for a live symbol or fixup. */
static bool minild_loong_align_live_offset_valid(
    uint64_t offset, const MiniLdLoongAlignSite *sites,
    const size_t *kept, size_t site_count) {
    size_t i;

    if (offset > SIZE_MAX) {
        return false;
    }
    for (i = 0U; i < site_count; ++i) {
        size_t start = sites[i].offset;
        size_t end = start + sites[i].reserved;
        size_t new_end = start + kept[i];

        if ((size_t)offset > start &&
            (size_t)offset >= new_end &&
            (size_t)offset < end) {
            return false;
        }
    }
    return true;
}

static bool minild_loongarch_relax_section(
    MiniLdState *state, size_t section_index, uint64_t base_addr) {
    MiniLdSection *section = &state->work.sections[section_index];
    MiniLdLoongAlignSite *sites = NULL;
    MiniLdRelaxEvent *events = NULL;
    size_t *kept = NULL;
    unsigned char *output = NULL;
    size_t site_count = 0U;
    size_t output_size = 0U;
    size_t cursor = 0U;
    size_t relocation_begin, relocation_end;
    size_t i, j;
    bool ok = false;

    if (!relocations_build_section_index(&state->relocations,
                                         state->work.count,
                                         state->diagnostics)) {
        return false;
    }
    if (section_index >= state->relocations.index_sections) {
        return true;
    }
    relocation_begin = state->relocations.section_starts[section_index];
    relocation_end = state->relocations.section_starts[section_index + 1U];
    for (i = relocation_begin; i < relocation_end; ++i) {
        if (state->relocations.entries[
                state->relocations.section_refs[i]].type == R_LARCH_ALIGN) {
            ++site_count;
        }
    }
    if (site_count == 0U) {
        return true;
    }
    if (site_count > SIZE_MAX / sizeof(*sites) ||
        site_count > SIZE_MAX / sizeof(*events) ||
        site_count > SIZE_MAX / sizeof(*kept)) {
        fprintf(state->diagnostics,
                "minic-ld: loongarch-align-sites-overflow:%s\n",
                section->name);
        return false;
    }
    sites = malloc(site_count * sizeof(*sites));
    events = malloc(site_count * sizeof(*events));
    kept = malloc(site_count * sizeof(*kept));
    output = malloc(section->size == 0U ? 1U : section->size);
    if (sites == NULL || events == NULL || kept == NULL || output == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:loongarch-align:%s\n",
                section->name);
        goto done;
    }

    site_count = 0U;
    for (i = relocation_begin; i < relocation_end; ++i) {
        size_t reloc_index = state->relocations.section_refs[i];
        const MiniLdReloc *reloc = &state->relocations.entries[reloc_index];
        MiniLdLoongAlignSite *site;

        if (reloc->type != R_LARCH_ALIGN) {
            continue;
        }
        site = &sites[site_count];
        if (!minild_loong_align_decode(reloc, section, site)) {
            fprintf(state->diagnostics,
                    "minic-ld: invalid-loongarch-static-align:%s:%zu\n",
                    section->name, reloc_index);
            goto done;
        }
        site->relocation_index = reloc_index;
        ++site_count;
    }
    qsort(sites, site_count, sizeof(*sites),
          minild_loong_align_site_compare);
    for (i = 0U; i < site_count; ++i) {
        const MiniLdLoongAlignSite *site = &sites[i];
        size_t required, copy;
        uint64_t place;

        if (site->offset < cursor ||
            !range_ok(site->offset, site->reserved, section->size)) {
            fprintf(state->diagnostics,
                    "minic-ld: loongarch-align-overlap:%s:%zu\n",
                    section->name, site->offset);
            goto done;
        }
        copy = site->offset - cursor;
        if (!range_ok(cursor, copy, section->size) ||
            !range_ok(output_size, copy, section->size)) {
            fprintf(state->diagnostics,
                    "minic-ld: loongarch-align-copy-overflow:%s\n",
                    section->name);
            goto done;
        }
        memcpy(output + output_size, section->data + cursor, copy);
        output_size += copy;

        for (j = 0U; j < site->reserved; j += 4U) {
            if (load_u32le(section->data + site->offset + j) !=
                UINT32_C(0x03400000)) {
                fprintf(state->diagnostics,
                        "minic-ld: invalid-loongarch-align-nop:%s:%zu\n",
                        section->name, site->offset + j);
                goto done;
            }
        }
        if (base_addr > UINT64_MAX - (uint64_t)output_size) {
            fprintf(state->diagnostics,
                    "minic-ld: loongarch-align-vma-overflow:%s\n",
                    section->name);
            goto done;
        }
        place = base_addr + (uint64_t)output_size;
        required = (size_t)((-(uint64_t)place) &
                            (uint64_t)(site->alignment - 1U));
        /* GAS p2align's max-skip controls the required padding at this
         * placement, not its conservative NOP reservation.  Exceeding
         * max-skip abandons alignment and consumes the entire reserve. */
        if (site->skip_limit != 0U && required > site->skip_limit) {
            required = 0U;
        }
        if (required > site->reserved || (required & 3U) != 0U ||
            !range_ok(output_size, required, section->size)) {
            fprintf(state->diagnostics,
                    "minic-ld: loongarch-align-unsatisfied:%s:%zu:"
                    "alignment=%zu:reserved=%zu:required=%zu\n",
                    section->name, site->offset,
                    site->alignment, site->reserved, required);
            goto done;
        }
        memcpy(output + output_size,
               section->data + site->offset, required);
        output_size += required;
        kept[i] = required;
        events[i].raw_end = site->offset + site->reserved;
        events[i].cumulative_deleted =
            (i == 0U ? 0U : events[i - 1U].cumulative_deleted) +
            (site->reserved - required);
        cursor = events[i].raw_end;
    }
    if (!range_ok(cursor, section->size - cursor, section->size) ||
        !range_ok(output_size, section->size - cursor, section->size)) {
        fprintf(state->diagnostics,
                "minic-ld: loongarch-align-tail-overflow:%s\n",
                section->name);
        goto done;
    }
    memcpy(output + output_size,
           section->data + cursor, section->size - cursor);
    output_size += section->size - cursor;

    for (i = 0U; i < state->resolver.count; ++i) {
        const MiniLdResolvedSymbol *symbol = &state->resolver.entries[i];

        if (symbol->section == (int)section_index &&
            !minild_loong_align_live_offset_valid(
                symbol->value, sites, kept, site_count)) {
            fprintf(state->diagnostics,
                    "minic-ld: loongarch-align-deleted-symbol:%s\n",
                    symbol->name);
            goto done;
        }
    }
    for (i = relocation_begin; i < relocation_end; ++i) {
        size_t reloc_index = state->relocations.section_refs[i];
        const MiniLdReloc *reloc = &state->relocations.entries[reloc_index];

        if (reloc->type != R_LARCH_ALIGN &&
            !minild_loong_align_live_offset_valid(
                reloc->offset, sites, kept, site_count)) {
            fprintf(state->diagnostics,
                    "minic-ld: loongarch-align-deleted-relocation:%u\n",
                    (unsigned)reloc->type);
            goto done;
        }
    }
    for (i = 0U; i < state->resolver.count; ++i) {
        MiniLdResolvedSymbol *symbol = &state->resolver.entries[i];

        if (symbol->section == (int)section_index) {
            if (symbol->size != 0U) {
                size_t old_start = (size_t)symbol->value;
                size_t old_end;
                size_t new_start, new_end;

                if (symbol->value > section->size ||
                    symbol->size > section->size - old_start) {
                    fprintf(state->diagnostics,
                            "minic-ld: loongarch-align-symbol-span-overflow:%s\n",
                            symbol->name);
                    goto done;
                }
                old_end = old_start + (size_t)symbol->size;
                new_start = static_map_relaxed_offset(
                    old_start, events, site_count);
                new_end = static_map_relaxed_offset(
                    old_end, events, site_count);
                if (new_end < new_start) {
                    fprintf(state->diagnostics,
                            "minic-ld: loongarch-align-symbol-size-underflow:%s\n",
                            symbol->name);
                    goto done;
                }
                symbol->size = (uint64_t)(new_end - new_start);
            }
            symbol->value = (uint64_t)static_map_relaxed_offset(
                (size_t)symbol->value, events, site_count);
        }
    }
    for (i = relocation_begin; i < relocation_end; ++i) {
        MiniLdReloc *reloc = &state->relocations.entries[
            state->relocations.section_refs[i]];

        reloc->offset = (uint64_t)static_map_relaxed_offset(
            (size_t)reloc->offset, events, site_count);
    }
    for (i = 0U; i < site_count; ++i) {
        state->relocations.entries[sites[i].relocation_index].type =
            R_LARCH_NONE;  /* validated and consumed by target relaxation */
    }
    memcpy(section->data, output, output_size);
    section->size = output_size;
    ok = true;

done:
    free(output);
    free(kept);
    free(events);
    free(sites);
    return ok;
}
