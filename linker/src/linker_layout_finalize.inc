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


#include "linker_riscv_default_symbols.inc"

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
    /* GCC's static CRT/libc expects a mapped ELF header; its start is a
     * real linker-defined symbol, not an unresolved reference to metadata.
     * Scripted kernels retain their own exact segment/header contract. */
    if (!layout->scripted && state->target->map_static_elf_headers &&
        layout->have_rx &&
        ((uint64_t)layout->rx_file_offset > layout->rx_vaddr ||
         !static_define_absolute_if_undefined(
             state, "__ehdr_start",
             layout->rx_vaddr - (uint64_t)layout->rx_file_offset))) {
        return false;
    }
    return (state->target->synthesize_default_symbols == NULL ||
            state->target->synthesize_default_symbols(state, layout)) &&
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

#include "linker_layout_symbols.inc"

#include "linker_static_reloc_operands.inc"

#include "linker_nonriscv_static_fixup.inc"
#include "linker_x86_static_got.inc"
#include "linker_x86_static_fixup.inc"
#include "linker_i386_static_fixup.inc"
#include "linker_aarch64_static_got.inc"
#include "linker_aarch64_static_fixup.inc"
#include "linker_loongarch_static_fixup.inc"
#include "linker_riscv_static_got.inc"
#include "linker_riscv_static_apply.inc"
#include "linker_static_reloc_apply.inc"
