/*
 * Static section-GC extension for MiniLD.
 *
 * Keep the mature linker implementation intact in linker_core.inc while the
 * V0 reachability contract is proved against real workloads.  The wrapper
 * only intercepts final static links when the CLI explicitly enables GC;
 * every other linker mode continues through the existing implementation.
 */
#define minild_link_static_elf64_riscv_inputs_options \
    minild_link_static_elf64_riscv_inputs_options_without_gc
#define minild_link_static_elf64_riscv_inputs \
    minild_link_static_elf64_riscv_inputs_without_gc
#include "linker_core.inc"
#undef minild_link_static_elf64_riscv_inputs
#undef minild_link_static_elf64_riscv_inputs_options

static bool minild_gc_sections_enabled;

void minild_cli_enable_gc_sections_v0(void);
int minild_link_static_elf64_riscv_inputs_options(
    const char *output_path,
    const MiniLdInput *inputs,
    size_t input_count,
    const MiniLdStaticOptions *options,
    FILE *diagnostics);
int minild_link_static_elf64_riscv_inputs(const char *output_path,
                                          const MiniLdInput *inputs,
                                          size_t input_count,
                                          const char *entry_symbol,
                                          FILE *diagnostics);

void minild_cli_enable_gc_sections_v0(void) {
    minild_gc_sections_enabled = true;
}

typedef struct MiniLdGcMarker {
    size_t section;
    uint64_t offset;
} MiniLdGcMarker;

typedef struct MiniLdGcFragment {
    uint64_t start;
    size_t section;
} MiniLdGcFragment;

static int minild_gc_compare_marker(const void *lhs, const void *rhs) {
    const MiniLdGcMarker *a = lhs;
    const MiniLdGcMarker *b = rhs;

    if (a->section < b->section) {
        return -1;
    }
    if (a->section > b->section) {
        return 1;
    }
    if (a->offset < b->offset) {
        return -1;
    }
    if (a->offset > b->offset) {
        return 1;
    }
    return 0;
}

static size_t minild_gc_fragment_for(const MiniLdGcFragment *fragments,
                                     size_t count,
                                     uint64_t offset) {
    size_t lo = 0U;
    size_t hi = count;

    while (lo + 1U < hi) {
        size_t mid = lo + (hi - lo) / 2U;

        if (fragments[mid].start <= offset) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return lo;
}

/*
 * The core linker historically concatenates every same-named input section
 * immediately.  That is fine for ordinary linking, but it destroys the input
 * section identity required by --gc-sections.  ET_REL symbol tables retain an
 * STT_SECTION symbol at each original input-section base, so reconstruct the
 * original GC granularity after archive/group selection and before reachability
 * analysis.  This keeps the non-GC linker path byte-for-byte on the mature
 * implementation while giving static GC the semantics it needs for ordinary
 * `.text`/`.data` sections from musl and libgcc as well as MiniC's
 * `.text.<function>` sections.
 */
static bool minild_gc_restore_input_sections(MiniLdState *state) {
    const size_t original_section_count = state->section_count;
    const size_t marker_capacity = original_section_count + state->symbol_count;
    MiniLdGcMarker *markers = NULL;
    MiniLdGcFragment *fragments = NULL;
    size_t *fragment_begin = NULL;
    size_t *fragment_count = NULL;
    size_t marker_count = 0U;
    size_t fragment_used = 0U;
    size_t split_outputs = 0U;
    size_t i;
    bool ok = false;

    markers = malloc((marker_capacity == 0U ? 1U : marker_capacity) *
                     sizeof(*markers));
    fragments = malloc((marker_capacity == 0U ? 1U : marker_capacity) *
                       sizeof(*fragments));
    fragment_begin = malloc((original_section_count == 0U
                                 ? 1U
                                 : original_section_count) *
                            sizeof(*fragment_begin));
    fragment_count = calloc(original_section_count == 0U
                                ? 1U
                                : original_section_count,
                            sizeof(*fragment_count));
    if (markers == NULL || fragments == NULL || fragment_begin == NULL ||
        fragment_count == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:gc-input-sections\n");
        goto done;
    }
    for (i = 0U; i < original_section_count; ++i) {
        fragment_begin[i] = SIZE_MAX;
        if ((state->sections[i].flags & SHF_ALLOC) != 0U &&
            state->sections[i].size != 0U) {
            markers[marker_count].section = i;
            markers[marker_count].offset = 0U;
            ++marker_count;
        }
    }
    for (i = 0U; i < state->symbol_count; ++i) {
        const MiniLdSymbol *symbol = &state->symbols[i];
        size_t section_index;

        if (symbol->section < 0 ||
            ELF64_ST_TYPE(symbol->info) != STT_SECTION) {
            continue;
        }
        section_index = (size_t)symbol->section;
        if (section_index >= original_section_count ||
            (state->sections[section_index].flags & SHF_ALLOC) == 0U ||
            state->sections[section_index].size == 0U ||
            symbol->value >= state->sections[section_index].size) {
            continue;
        }
        if (marker_count >= marker_capacity) {
            fprintf(state->diagnostics,
                    "minic-ld: gc-input-section-marker-overflow\n");
            goto done;
        }
        markers[marker_count].section = section_index;
        markers[marker_count].offset = symbol->value;
        ++marker_count;
    }

    qsort(markers,
          marker_count,
          sizeof(*markers),
          minild_gc_compare_marker);

    i = 0U;
    while (i < marker_count) {
        const size_t old_index = markers[i].section;
        size_t group_end = i;
        size_t unique_count = 0U;
        uint64_t previous = UINT64_MAX;
        size_t j;

        while (group_end < marker_count &&
               markers[group_end].section == old_index) {
            if (markers[group_end].offset != previous) {
                markers[i + unique_count] = markers[group_end];
                previous = markers[group_end].offset;
                ++unique_count;
            }
            ++group_end;
        }

        if (unique_count > 1U) {
            const uint32_t type = state->sections[old_index].type;
            const uint64_t flags = state->sections[old_index].flags;
            const uint64_t align = state->sections[old_index].align;
            const uint64_t entsize = state->sections[old_index].entsize;
            const size_t old_size = state->sections[old_index].size;
            const unsigned char *old_data = state->sections[old_index].data;
            const char *old_name = state->sections[old_index].name;

            fragment_begin[old_index] = fragment_used;
            fragment_count[old_index] = unique_count;
            ++split_outputs;

            for (j = 0U; j < unique_count; ++j) {
                const uint64_t start = markers[i + j].offset;
                const uint64_t end =
                    j + 1U < unique_count ? markers[i + j + 1U].offset
                                         : (uint64_t)old_size;
                size_t new_index;
                MiniLdSection *fragment;
                char *name;
                int needed;

                if (end < start || end > old_size ||
                    start > SIZE_MAX || end - start > SIZE_MAX) {
                    fprintf(state->diagnostics,
                            "minic-ld: gc-input-section-range:%s\n",
                            old_name);
                    goto done;
                }
                if (!ensure_section_capacity(state)) {
                    fprintf(state->diagnostics,
                            "minic-ld: out-of-memory:gc-input-section-state\n");
                    goto done;
                }
                needed = snprintf(NULL,
                                  0,
                                  "%s.__minild_gc_%zu_%zu",
                                  old_name,
                                  old_index,
                                  j);
                if (needed < 0) {
                    fprintf(state->diagnostics,
                            "minic-ld: gc-input-section-name:%s\n",
                            old_name);
                    goto done;
                }
                name = malloc((size_t)needed + 1U);
                if (name == NULL) {
                    fprintf(state->diagnostics,
                            "minic-ld: out-of-memory:gc-input-section-name\n");
                    goto done;
                }
                (void)snprintf(name,
                               (size_t)needed + 1U,
                               "%s.__minild_gc_%zu_%zu",
                               old_name,
                               old_index,
                               j);

                new_index = state->section_count++;
                fragment = &state->sections[new_index];
                memset(fragment, 0, sizeof(*fragment));
                fragment->name = name;
                fragment->type = type;
                fragment->flags = flags;
                fragment->align = align;
                fragment->entsize = entsize;

                if (type == SHT_NOBITS) {
                    if (!section_append_zero(fragment, (size_t)(end - start))) {
                        fprintf(state->diagnostics,
                                "minic-ld: gc-input-section-copy:%s\n",
                                old_name);
                        goto done;
                    }
                } else if (!section_append_data(fragment,
                                                old_data + (size_t)start,
                                                (size_t)(end - start))) {
                    fprintf(state->diagnostics,
                            "minic-ld: gc-input-section-copy:%s\n",
                            old_name);
                    goto done;
                }
                fragments[fragment_used].start = start;
                fragments[fragment_used].section = new_index;
                ++fragment_used;
            }
        }
        i = group_end;
    }

    if (split_outputs != 0U) {
        for (i = 0U; i < state->symbol_count; ++i) {
            MiniLdSymbol *symbol = &state->symbols[i];
            size_t old_index;
            size_t local;
            const MiniLdGcFragment *map;

            if (symbol->section < 0) {
                continue;
            }
            old_index = (size_t)symbol->section;
            if (old_index >= original_section_count ||
                fragment_count[old_index] == 0U) {
                continue;
            }
            map = fragments + fragment_begin[old_index];
            local = minild_gc_fragment_for(map,
                                           fragment_count[old_index],
                                           symbol->value);
            if (symbol->value < map[local].start) {
                fprintf(state->diagnostics,
                        "minic-ld: gc-symbol-before-input-section:%s\n",
                        symbol->name);
                goto done;
            }
            symbol->section = (int)map[local].section;
            symbol->value -= map[local].start;
        }

        for (i = 0U; i < state->reloc_count; ++i) {
            MiniLdReloc *reloc = &state->relocs[i];
            size_t old_index = reloc->section;
            size_t local;
            const MiniLdGcFragment *map;

            if (old_index >= original_section_count ||
                fragment_count[old_index] == 0U) {
                continue;
            }
            map = fragments + fragment_begin[old_index];
            local = minild_gc_fragment_for(map,
                                           fragment_count[old_index],
                                           reloc->offset);
            if (reloc->offset < map[local].start) {
                fprintf(state->diagnostics,
                        "minic-ld: gc-relocation-before-input-section\n");
                goto done;
            }
            --state->sections[old_index].relocation_count;
            reloc->section = map[local].section;
            reloc->offset -= map[local].start;
            ++state->sections[reloc->section].relocation_count;
        }

        for (i = 0U; i < original_section_count; ++i) {
            if (fragment_count[i] != 0U) {
                state->sections[i].size = 0U;
                state->sections[i].relocation_count = 0U;
            }
        }

        {
            size_t index_capacity = 64U;
            size_t target;

            if (state->section_count > (SIZE_MAX - 1U) / 2U) {
                fprintf(state->diagnostics,
                        "minic-ld: gc-section-index-overflow\n");
                goto done;
            }
            target = state->section_count * 2U + 1U;
            while (index_capacity < target) {
                if (index_capacity > SIZE_MAX / 2U) {
                    fprintf(state->diagnostics,
                            "minic-ld: gc-section-index-overflow\n");
                    goto done;
                }
                index_capacity *= 2U;
            }
            if (!rebuild_section_index(state, index_capacity)) {
                fprintf(state->diagnostics,
                        "minic-ld: out-of-memory:gc-section-index\n");
                goto done;
            }
        }
    }

    fprintf(state->diagnostics,
            "minic-ld: gc-input-sections:split=%zu:fragments=%zu\n",
            split_outputs,
            fragment_used);
    ok = true;

done:
    free(fragment_count);
    free(fragment_begin);
    free(fragments);
    free(markers);
    return ok;
}


typedef struct MiniLdGcSectionOrder {
    size_t old_index;
    size_t order;
} MiniLdGcSectionOrder;

static int minild_gc_compare_section_order(const void *lhs,
                                           const void *rhs) {
    const MiniLdGcSectionOrder *a = lhs;
    const MiniLdGcSectionOrder *b = rhs;

    if (a->order < b->order) {
        return -1;
    }
    if (a->order > b->order) {
        return 1;
    }
    if (a->old_index < b->old_index) {
        return -1;
    }
    if (a->old_index > b->old_index) {
        return 1;
    }
    return 0;
}

/*
 * Splitting a historically merged `.text`/`.data` output section is
 * not enough: appending every recovered fragment at the end changes
 * input-section order and can turn a valid R_RISCV_JAL into an
 * artificial out-of-range jump.  STT_SECTION symbols are appended as
 * each ET_REL input is consumed, so their state-symbol order retains
 * the original cross-object input-section ordering.  Restore that
 * order after fragment reconstruction and remap all section indices
 * before layout/GC.
 */
static bool minild_gc_restore_input_order(MiniLdState *state) {
    const size_t count = state->section_count;
    MiniLdGcSectionOrder *entries = NULL;
    MiniLdSection *ordered_sections = NULL;
    size_t *orders = NULL;
    size_t *remap = NULL;
    size_t index_capacity = 64U;
    size_t target;
    size_t ordered_count = 0U;
    size_t i;
    bool installed = false;
    bool ok = false;

    entries = malloc((count == 0U ? 1U : count) * sizeof(*entries));
    ordered_sections =
        malloc((count == 0U ? 1U : count) * sizeof(*ordered_sections));
    orders = malloc((count == 0U ? 1U : count) * sizeof(*orders));
    remap = malloc((count == 0U ? 1U : count) * sizeof(*remap));
    if (entries == NULL || ordered_sections == NULL ||
        orders == NULL || remap == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:gc-input-order\n");
        goto done;
    }
    for (i = 0U; i < count; ++i) {
        orders[i] = SIZE_MAX;
    }

    for (i = 0U; i < state->symbol_count; ++i) {
        const MiniLdSymbol *symbol = &state->symbols[i];
        size_t section_index;

        if (symbol->section < 0 ||
            ELF64_ST_TYPE(symbol->info) != STT_SECTION) {
            continue;
        }
        section_index = (size_t)symbol->section;
        if (section_index >= count) {
            fprintf(state->diagnostics,
                    "minic-ld: gc-input-order-symbol-section\n");
            goto done;
        }
        if (orders[section_index] == SIZE_MAX) {
            orders[section_index] = i;
            ++ordered_count;
        }
    }

    if (count > (SIZE_MAX - 1U) / 2U) {
        fprintf(state->diagnostics,
                "minic-ld: gc-input-order-index-overflow\n");
        goto done;
    }
    target = count * 2U + 1U;
    while (index_capacity < target) {
        if (index_capacity > SIZE_MAX / 2U) {
            fprintf(state->diagnostics,
                    "minic-ld: gc-input-order-index-overflow\n");
            goto done;
        }
        index_capacity *= 2U;
    }

    for (i = 0U; i < count; ++i) {
        entries[i].old_index = i;
        entries[i].order =
            orders[i] == SIZE_MAX ? state->symbol_count + i : orders[i];
    }
    qsort(entries,
          count,
          sizeof(*entries),
          minild_gc_compare_section_order);

    for (i = 0U; i < count; ++i) {
        const size_t old_index = entries[i].old_index;
        ordered_sections[i] = state->sections[old_index];
        remap[old_index] = i;
    }
    for (i = 0U; i < state->symbol_count; ++i) {
        MiniLdSymbol *symbol = &state->symbols[i];

        if (symbol->section < 0) {
            continue;
        }
        if ((size_t)symbol->section >= count) {
            fprintf(state->diagnostics,
                    "minic-ld: gc-input-order-symbol-remap\n");
            goto done;
        }
        symbol->section = (int)remap[symbol->section];
    }
    for (i = 0U; i < state->reloc_count; ++i) {
        if (state->relocs[i].section >= count) {
            fprintf(state->diagnostics,
                    "minic-ld: gc-input-order-reloc-remap\n");
            goto done;
        }
        state->relocs[i].section = remap[state->relocs[i].section];
    }

    free(state->sections);
    state->sections = ordered_sections;
    state->section_capacity = count;
    ordered_sections = NULL;
    installed = true;

    if (!rebuild_section_index(state, index_capacity)) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:gc-input-order-index\n");
        goto done;
    }
    fprintf(state->diagnostics,
            "minic-ld: gc-input-order:sections=%zu:ordered=%zu\n",
            count,
            ordered_count);
    ok = true;

done:
    if (!installed) {
        free(ordered_sections);
    }
    free(remap);
    free(orders);
    free(entries);
    return ok;
}


/*
 * GNU's default linker script groups executable input sections into
 * the text output region before read-only data.  The GC frontier
 * reconstructs original input-section identity, so preserving one
 * global input order across both code and rodata can incorrectly put
 * megabytes of live rodata between two direct R_RISCV_JAL sites.
 * Stable-partition the default non-script layout into output classes:
 * executable RX first, non-executable RX second, then everything
 * else.  Relative order inside each class remains the recovered input
 * order, so the earlier GC input-order contract is preserved.
 */
static int minild_gc_default_output_class(const MiniLdSection *section) {
    if ((section->flags & SHF_ALLOC) != 0U &&
        (section->flags & SHF_WRITE) == 0U) {
        return (section->flags & SHF_EXECINSTR) != 0U ? 0 : 1;
    }
    return 2;
}

static bool minild_gc_group_default_output_classes(MiniLdState *state) {
    const size_t count = state->section_count;
    MiniLdSection *ordered_sections = NULL;
    size_t *remap = NULL;
    size_t index_capacity = 64U;
    size_t target;
    size_t next = 0U;
    size_t executable_rx = 0U;
    size_t readonly_rx = 0U;
    size_t i;
    int output_class;
    bool installed = false;
    bool ok = false;

    ordered_sections =
        malloc((count == 0U ? 1U : count) * sizeof(*ordered_sections));
    remap = malloc((count == 0U ? 1U : count) * sizeof(*remap));
    if (ordered_sections == NULL || remap == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:gc-output-classes\n");
        goto done;
    }

    for (output_class = 0; output_class < 3; ++output_class) {
        for (i = 0U; i < count; ++i) {
            if (minild_gc_default_output_class(&state->sections[i]) !=
                output_class) {
                continue;
            }
            ordered_sections[next] = state->sections[i];
            remap[i] = next;
            if (output_class == 0) {
                ++executable_rx;
            } else if (output_class == 1) {
                ++readonly_rx;
            }
            ++next;
        }
    }
    if (next != count) {
        fprintf(state->diagnostics,
                "minic-ld: gc-output-class-count-mismatch\n");
        goto done;
    }

    for (i = 0U; i < state->symbol_count; ++i) {
        MiniLdSymbol *symbol = &state->symbols[i];

        if (symbol->section < 0) {
            continue;
        }
        if ((size_t)symbol->section >= count) {
            fprintf(state->diagnostics,
                    "minic-ld: gc-output-class-symbol-remap\n");
            goto done;
        }
        symbol->section = (int)remap[symbol->section];
    }
    for (i = 0U; i < state->reloc_count; ++i) {
        if (state->relocs[i].section >= count) {
            fprintf(state->diagnostics,
                    "minic-ld: gc-output-class-reloc-remap\n");
            goto done;
        }
        state->relocs[i].section = remap[state->relocs[i].section];
    }

    free(state->sections);
    state->sections = ordered_sections;
    state->section_capacity = count;
    ordered_sections = NULL;
    installed = true;

    if (count > (SIZE_MAX - 1U) / 2U) {
        fprintf(state->diagnostics,
                "minic-ld: gc-output-class-index-overflow\n");
        goto done;
    }
    target = count * 2U + 1U;
    while (index_capacity < target) {
        if (index_capacity > SIZE_MAX / 2U) {
            fprintf(state->diagnostics,
                    "minic-ld: gc-output-class-index-overflow\n");
            goto done;
        }
        index_capacity *= 2U;
    }
    if (!rebuild_section_index(state, index_capacity)) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:gc-output-class-index\n");
        goto done;
    }
    fprintf(state->diagnostics,
            "minic-ld: gc-output-classes:exec-rx=%zu:ro-rx=%zu\n",
            executable_rx,
            readonly_rx);
    ok = true;

done:
    if (!installed) {
        free(ordered_sections);
    }
    free(remap);
    return ok;
}


typedef struct MiniLdGcOutputAlignmentOrder {
    size_t old_index;
    int output_class;
    uint64_t align;
} MiniLdGcOutputAlignmentOrder;

static int minild_gc_compare_output_alignment(const void *lhs_ptr,
                                              const void *rhs_ptr) {
    const MiniLdGcOutputAlignmentOrder *lhs = lhs_ptr;
    const MiniLdGcOutputAlignmentOrder *rhs = rhs_ptr;

    if (lhs->output_class != rhs->output_class) {
        return lhs->output_class < rhs->output_class ? -1 : 1;
    }
    if (lhs->output_class < 2 && lhs->align != rhs->align) {
        return lhs->align > rhs->align ? -1 : 1;
    }
    if (lhs->old_index != rhs->old_index) {
        return lhs->old_index < rhs->old_index ? -1 : 1;
    }
    return 0;
}

/*
 * BusyBox's GNU reference link uses --sort-section=alignment.
 * Preserve the recovered input order for equal-alignment sections,
 * but sort executable and read-only alloc sections by decreasing
 * alignment inside their default output classes.  This matches the
 * range-relevant GNU wildcard ordering without disturbing writable
 * or metadata section order.
 */
static bool minild_gc_sort_default_output_alignment(MiniLdState *state) {
    const size_t count = state->section_count;
    MiniLdGcOutputAlignmentOrder *entries = NULL;
    MiniLdSection *ordered_sections = NULL;
    size_t *remap = NULL;
    size_t index_capacity = 64U;
    size_t target;
    size_t i;
    bool installed = false;
    bool ok = false;

    entries = malloc((count == 0U ? 1U : count) * sizeof(*entries));
    ordered_sections =
        malloc((count == 0U ? 1U : count) * sizeof(*ordered_sections));
    remap = malloc((count == 0U ? 1U : count) * sizeof(*remap));
    if (entries == NULL || ordered_sections == NULL || remap == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:gc-output-alignment\n");
        goto done;
    }

    for (i = 0U; i < count; ++i) {
        entries[i].old_index = i;
        entries[i].output_class =
            minild_gc_default_output_class(&state->sections[i]);
        entries[i].align = state->sections[i].align;
    }
    qsort(entries,
          count,
          sizeof(*entries),
          minild_gc_compare_output_alignment);

    for (i = 0U; i < count; ++i) {
        size_t old_index = entries[i].old_index;
        ordered_sections[i] = state->sections[old_index];
        remap[old_index] = i;
    }
    for (i = 0U; i < state->symbol_count; ++i) {
        MiniLdSymbol *symbol = &state->symbols[i];

        if (symbol->section < 0) {
            continue;
        }
        if ((size_t)symbol->section >= count) {
            fprintf(state->diagnostics,
                    "minic-ld: gc-output-alignment-symbol-remap\n");
            goto done;
        }
        symbol->section = (int)remap[symbol->section];
    }
    for (i = 0U; i < state->reloc_count; ++i) {
        if (state->relocs[i].section >= count) {
            fprintf(state->diagnostics,
                    "minic-ld: gc-output-alignment-reloc-remap\n");
            goto done;
        }
        state->relocs[i].section = remap[state->relocs[i].section];
    }

    free(state->sections);
    state->sections = ordered_sections;
    state->section_capacity = count;
    ordered_sections = NULL;
    installed = true;

    if (count > (SIZE_MAX - 1U) / 2U) {
        fprintf(state->diagnostics,
                "minic-ld: gc-output-alignment-index-overflow\n");
        goto done;
    }
    target = count * 2U + 1U;
    while (index_capacity < target) {
        if (index_capacity > SIZE_MAX / 2U) {
            fprintf(state->diagnostics,
                    "minic-ld: gc-output-alignment-index-overflow\n");
            goto done;
        }
        index_capacity *= 2U;
    }
    if (!rebuild_section_index(state, index_capacity)) {
        fprintf(state->diagnostics,
                "minic-ld: out-of-memory:gc-output-alignment-index\n");
        goto done;
    }
    fprintf(state->diagnostics,
            "minic-ld: gc-output-alignment:sections=%zu\n",
            count);
    ok = true;

done:
    if (!installed) {
        free(ordered_sections);
    }
    free(remap);
    free(entries);
    return ok;
}

static bool minild_gc_name_has_prefix(const char *name, const char *prefix) {
    size_t length = strlen(prefix);

    return strcmp(name, prefix) == 0 ||
           (strncmp(name, prefix, length) == 0 && name[length] == '.');
}

static bool minild_gc_root_section(const MiniLdSection *section) {
    const char *name = section->name;

    if ((section->flags & SHF_ALLOC) == 0U) {
        return false;
    }
#ifdef SHF_GNU_RETAIN
    if ((section->flags & SHF_GNU_RETAIN) != 0U) {
        return true;
    }
#endif
    return strcmp(name, ".init") == 0 ||
           strcmp(name, ".fini") == 0 ||
           minild_gc_name_has_prefix(name, ".preinit_array") ||
           minild_gc_name_has_prefix(name, ".init_array") ||
           minild_gc_name_has_prefix(name, ".fini_array") ||
           minild_gc_name_has_prefix(name, ".ctors") ||
           minild_gc_name_has_prefix(name, ".dtors");
}

static bool minild_gc_mark_section(const MiniLdState *state,
                                   size_t section_index,
                                   bool *live,
                                   size_t *queue,
                                   size_t *queue_size) {
    if (section_index >= state->section_count || live[section_index] ||
        (state->sections[section_index].flags & SHF_ALLOC) == 0U) {
        return true;
    }
    if (*queue_size >= state->section_count) {
        return false;
    }
    live[section_index] = true;
    queue[(*queue_size)++] = section_index;
    return true;
}

static bool minild_gc_prune_static(MiniLdState *state,
                                   const char *entry_symbol) {
    bool *live = NULL;
    size_t *heads = NULL;
    size_t *next = NULL;
    size_t *queue = NULL;
    size_t queue_head = 0U;
    size_t queue_size = 0U;
    size_t old_reloc_count = state->reloc_count;
    size_t write_reloc = 0U;
    size_t discarded = 0U;
    size_t i;
    size_t entry_index;
    bool ok = false;

    live = calloc(state->section_count == 0U ? 1U : state->section_count,
                  sizeof(*live));
    heads = malloc((state->section_count == 0U ? 1U : state->section_count) *
                   sizeof(*heads));
    next = malloc((state->reloc_count == 0U ? 1U : state->reloc_count) *
                  sizeof(*next));
    queue = malloc((state->section_count == 0U ? 1U : state->section_count) *
                   sizeof(*queue));
    if (live == NULL || heads == NULL || next == NULL || queue == NULL) {
        fprintf(state->diagnostics, "minic-ld: out-of-memory:gc-sections\n");
        goto done;
    }
    for (i = 0U; i < state->section_count; ++i) {
        heads[i] = SIZE_MAX;
    }
    for (i = 0U; i < state->reloc_count; ++i) {
        size_t source = state->relocs[i].section;

        next[i] = SIZE_MAX;
        if (source >= state->section_count) {
            fprintf(state->diagnostics,
                    "minic-ld: gc-invalid-relocation-section\n");
            goto done;
        }
        next[i] = heads[source];
        heads[source] = i;
    }

    entry_index = find_global_symbol(state, entry_symbol);
    if (entry_index == SIZE_MAX ||
        state->symbols[entry_index].section < 0 ||
        (size_t)state->symbols[entry_index].section >= state->section_count) {
        fprintf(state->diagnostics,
                "minic-ld: undefined-entry:%s\n",
                entry_symbol);
        goto done;
    }
    if (!minild_gc_mark_section(state,
                                (size_t)state->symbols[entry_index].section,
                                live,
                                queue,
                                &queue_size)) {
        fprintf(state->diagnostics, "minic-ld: gc-root-overflow\n");
        goto done;
    }

    for (i = 0U; i < state->section_count; ++i) {
        if (minild_gc_root_section(&state->sections[i]) &&
            !minild_gc_mark_section(state, i, live, queue, &queue_size)) {
            fprintf(state->diagnostics, "minic-ld: gc-root-overflow\n");
            goto done;
        }
    }

    while (queue_head < queue_size) {
        size_t source = queue[queue_head++];
        size_t relocation_index;

        for (relocation_index = heads[source];
             relocation_index != SIZE_MAX;
             relocation_index = next[relocation_index]) {
            MiniLdReloc *reloc = &state->relocs[relocation_index];
            MiniLdSymbol *target;

            if (reloc->symbol == SIZE_MAX ||
                reloc->symbol >= state->symbol_count) {
                continue;
            }
            target = &state->symbols[reloc->symbol];
            if (target->section < 0 ||
                (size_t)target->section >= state->section_count) {
                continue;
            }
            if (!minild_gc_mark_section(state,
                                        (size_t)target->section,
                                        live,
                                        queue,
                                        &queue_size)) {
                fprintf(state->diagnostics, "minic-ld: gc-queue-overflow\n");
                goto done;
            }
        }
    }

    for (i = 0U; i < state->section_count; ++i) {
        state->sections[i].relocation_count = 0U;
        if ((state->sections[i].flags & SHF_ALLOC) != 0U && !live[i]) {
            state->sections[i].size = 0U;
            ++discarded;
        }
    }

    for (i = 0U; i < state->symbol_count; ++i) {
        MiniLdSymbol *symbol = &state->symbols[i];

        if (symbol->section >= 0 &&
            (size_t)symbol->section < state->section_count &&
            !live[symbol->section]) {
            symbol->section = MINILD_SECTION_UNDEF;
            symbol->value = 0U;
            symbol->size = 0U;
        }
    }

    for (i = 0U; i < old_reloc_count; ++i) {
        MiniLdReloc reloc = state->relocs[i];

        if (reloc.section >= state->section_count ||
            !live[reloc.section]) {
            continue;
        }
        state->relocs[write_reloc++] = reloc;
        ++state->sections[reloc.section].relocation_count;
    }
    state->reloc_count = write_reloc;

    fprintf(state->diagnostics,
            "minic-ld: gc-sections:live=%zu:discarded=%zu:relocs=%zu->%zu\n",
            queue_size,
            discarded,
            old_reloc_count,
            write_reloc);
    ok = true;

done:
    free(queue);
    free(next);
    free(heads);
    free(live);
    return ok;
}

static int minild_link_static_with_gc_v0(
    const char *output_path,
    const MiniLdInput *inputs,
    size_t input_count,
    const MiniLdStaticOptions *options,
    FILE *diagnostics) {
    MiniLdState state;
    MiniLdStaticLayout layout;
    MiniLdStaticGot got;
    const char *entry_symbol;
    uint64_t entry = 0U;
    bool layout_ready = false;
    bool ok = false;

    if (output_path == NULL || inputs == NULL || input_count == 0U ||
        diagnostics == NULL) {
        return 2;
    }
    if (options != NULL && options->script_path != NULL) {
        fprintf(diagnostics,
                "minic-ld: gc-sections-with-linker-script-not-yet-supported\n");
        return 1;
    }

    memset(&state, 0, sizeof(state));
    memset(&layout, 0, sizeof(layout));
    memset(&got, 0, sizeof(got));
    got.section = SIZE_MAX;
    state.diagnostics = diagnostics;
    entry_symbol = options != NULL && options->entry_symbol != NULL
                       ? options->entry_symbol
                       : "_start";

    if (!process_input_sequence(&state, inputs, input_count) ||
        !state.have_input ||
        !minild_gc_restore_input_sections(&state) ||
        !minild_gc_restore_input_order(&state) ||
        !static_allocate_common(&state) ||
        !minild_gc_prune_static(&state, entry_symbol) ||
        !minild_gc_group_default_output_classes(&state) ||
        !minild_gc_sort_default_output_alignment(&state) ||
        !static_build_got(&state, &got) ||
        !static_build_layout(&state, &layout)) {
        goto done;
    }
    layout_ready = true;

    if (!static_synthesize_runtime_boundaries(&state, &layout) ||
        !static_fill_got(&state, &layout, &got) ||
        !static_apply_relocations(&state, &layout, &got) ||
        !static_entry_address(&state, &layout, entry_symbol, &entry) ||
        !static_write_executable(&state, &layout, output_path, entry)) {
        goto done;
    }
    ok = true;

done:
    if (layout_ready) {
        static_layout_destroy(&layout);
    }
    static_got_destroy(&got);
    state_destroy(&state);
    return ok ? 0 : 1;
}

int minild_link_static_elf64_riscv_inputs_options(
    const char *output_path,
    const MiniLdInput *inputs,
    size_t input_count,
    const MiniLdStaticOptions *options,
    FILE *diagnostics) {
    if (!minild_gc_sections_enabled) {
        return minild_link_static_elf64_riscv_inputs_options_without_gc(
            output_path,
            inputs,
            input_count,
            options,
            diagnostics);
    }
    return minild_link_static_with_gc_v0(output_path,
                                         inputs,
                                         input_count,
                                         options,
                                         diagnostics);
}

int minild_link_static_elf64_riscv_inputs(const char *output_path,
                                          const MiniLdInput *inputs,
                                          size_t input_count,
                                          const char *entry_symbol,
                                          FILE *diagnostics) {
    MiniLdStaticOptions options;

    options.entry_symbol = entry_symbol;
    options.script_path = NULL;
    return minild_link_static_elf64_riscv_inputs_options(output_path,
                                                          inputs,
                                                          input_count,
                                                          &options,
                                                          diagnostics);
}
