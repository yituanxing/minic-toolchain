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
            (layout->work != NULL &&
             layout->work[i].discarded) ||
            !static_section_has_prefix(section->name, section_prefix)) {
            continue;
        }
        section_start = layout->work[i].vaddr;
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
                    layout->work[i].discarded) {
                    continue;
                }
                candidate = layout->work[i].vaddr +
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
            (layout->work != NULL &&
             layout->work[i].discarded)) {
            continue;
        }
        address = layout->work[i].vaddr;
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
            (layout->work == NULL ||
             !layout->work[i].discarded) &&
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
    free(layout->work);
    free(layout->script_outputs);
    free(layout->script_bytes);
    memset(layout, 0, sizeof(*layout));
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
        (layout->work != NULL &&
         layout->work[symbol->section].discarded)) {
        fprintf(state->diagnostics,
                "minic-ld: unsupported-static-symbol:%s\n",
                symbol->name);
        return false;
    }
    *value_out = layout->work[symbol->section].vaddr + symbol->value;
    return true;
}


#include "linker_static_reloc_apply.inc"

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
