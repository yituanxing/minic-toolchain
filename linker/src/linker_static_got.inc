/* ET_EXEC GOT workspace: pre-layout symbol-slot reservation and post-layout
 * slot content materialization belong to one owner.  The physical layout
 * allocates the section as ordinary working-section bytes. */
typedef struct MiniLdStaticGot {
    size_t section;
    size_t *slot_offsets;
    size_t symbol_count;
    bool present;
} MiniLdStaticGot;

static void static_got_destroy(MiniLdStaticGot *got) {
    free(got->slot_offsets);
    got->slot_offsets = NULL;
    got->symbol_count = 0U;
    got->section = SIZE_MAX;
    got->present = false;
}

#include "linker_static_reloc_scan.inc"

/* Symbol addresses become concrete only after placement.  The definition
 * lives in the shared static/ET_DYN post-layout symbol view. */
static bool minild_layout_resolve_symbol(MiniLdState *state,
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
        if (!minild_layout_resolve_symbol(state, layout, i, &value) ||
            !range_ok(got->slot_offsets[i], 8U, section->size)) {
            return false;
        }
        store_u64le(section->data + got->slot_offsets[i], value);
    }
    return true;
}
