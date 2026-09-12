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
        !static_allocate_common(&state) ||
        !minild_gc_prune_static(&state, entry_symbol) ||
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
