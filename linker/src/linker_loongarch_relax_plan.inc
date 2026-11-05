/* LoongArch static-link relaxation is a whole-image, target-owned plan.
 * GNU BFD settles PCALA shortening before R_LARCH_ALIGN, then performs a
 * size-frozen final pass.  Only the target knows LoongArch instruction bytes;
 * Common reruns the complete script/layout whenever this plan changes size.
 * During PCALA convergence replay from immutable input bytes, symbol values
 * and relocation coordinates, so an earlier shrink cannot be applied twice.
 */
typedef enum MiniLdLoongRelaxPhase {
    MINILD_LOONG_RELAX_PCALA,
    MINILD_LOONG_RELAX_ALIGN,
    MINILD_LOONG_RELAX_CLOSED
} MiniLdLoongRelaxPhase;

typedef struct MiniLdLoongRelaxEntry {
    size_t section;
    uint64_t offset;
    uint32_t type;
    size_t index;
} MiniLdLoongRelaxEntry;

typedef struct MiniLdLoongRelaxPair {
    size_t section;
    size_t offset;
    size_t hi;
    size_t lo;
    size_t hi_marker;
    size_t lo_marker;
    bool shortened;
} MiniLdLoongRelaxPair;

typedef struct MiniLdLoongRelaxSection {
    unsigned char *original;
    size_t original_size;
} MiniLdLoongRelaxSection;

typedef struct MiniLdLoongRelaxSymbol {
    int section;
    uint64_t value;
    uint64_t size;
    bool input_defined;
} MiniLdLoongRelaxSymbol;

struct MiniLdLoongRelaxPlan {
    MiniLdLoongRelaxSection *sections;
    size_t section_count;
    MiniLdLoongRelaxSymbol *symbols;
    size_t symbol_count;
    uint64_t *reloc_offsets;
    uint32_t *reloc_types;
    size_t reloc_count;
    MiniLdLoongRelaxPair *pairs;
    size_t pair_count;
    MiniLdLoongRelaxPhase phase;
};

static int minild_loong_relax_entry_compare(const void *a, const void *b) {
    const MiniLdLoongRelaxEntry *x = a;
    const MiniLdLoongRelaxEntry *y = b;

    if (x->section != y->section) return x->section < y->section ? -1 : 1;
    if (x->offset != y->offset) return x->offset < y->offset ? -1 : 1;
    if (x->type != y->type) return x->type < y->type ? -1 : 1;
    return 0;
}

static const MiniLdLoongRelaxEntry *minild_loong_relax_find(
    const MiniLdLoongRelaxEntry *entries, size_t count, size_t section,
    uint64_t offset, uint32_t type) {
    MiniLdLoongRelaxEntry key = {section, offset, type, 0U};

    return bsearch(&key, entries, count, sizeof(*entries),
                   minild_loong_relax_entry_compare);
}

static void minild_loong_relax_plan_destroy(MiniLdLoongRelaxPlan *plan) {
    size_t i;

    if (plan == NULL) return;
    for (i = 0U; i < plan->section_count; ++i) {
        free(plan->sections[i].original);
    }
    free(plan->sections);
    free(plan->symbols);
    free(plan->reloc_offsets);
    free(plan->reloc_types);
    free(plan->pairs);
    free(plan);
}

static MiniLdLoongRelaxPlan *minild_loong_relax_capture(
    const MiniLdState *state) {
    MiniLdLoongRelaxPlan *plan = calloc(1U, sizeof(*plan));
    MiniLdLoongRelaxEntry *entries = NULL;
    size_t entry_count = 0U;
    size_t i;

    if (plan == NULL) return NULL;
    plan->section_count = state->work.count;
    plan->symbol_count = state->resolver.count;
    plan->reloc_count = state->relocations.count;
    if (plan->reloc_count > SIZE_MAX / sizeof(*entries) ||
        plan->reloc_count > SIZE_MAX / sizeof(*plan->reloc_offsets) ||
        plan->reloc_count > SIZE_MAX / sizeof(*plan->reloc_types)) {
        goto error;
    }
    plan->sections = calloc(plan->section_count ? plan->section_count : 1U,
                            sizeof(*plan->sections));
    plan->symbols = calloc(plan->symbol_count ? plan->symbol_count : 1U,
                           sizeof(*plan->symbols));
    plan->reloc_offsets = malloc((plan->reloc_count ? plan->reloc_count : 1U) *
                                 sizeof(*plan->reloc_offsets));
    plan->reloc_types = malloc((plan->reloc_count ? plan->reloc_count : 1U) *
                               sizeof(*plan->reloc_types));
    entries = malloc((plan->reloc_count ? plan->reloc_count : 1U) *
                     sizeof(*entries));
    if (plan->sections == NULL || plan->symbols == NULL ||
        plan->reloc_offsets == NULL || plan->reloc_types == NULL ||
        entries == NULL) goto error;
    for (i = 0U; i < plan->symbol_count; ++i) {
        const MiniLdResolvedSymbol *symbol = &state->resolver.entries[i];
        plan->symbols[i].section = symbol->section;
        plan->symbols[i].value = symbol->value;
        plan->symbols[i].size = symbol->size;
        plan->symbols[i].input_defined = symbol->source_symbol != SIZE_MAX;
    }
    for (i = 0U; i < plan->reloc_count; ++i) {
        const MiniLdReloc *reloc = &state->relocations.entries[i];
        plan->reloc_offsets[i] = reloc->offset;
        plan->reloc_types[i] = reloc->type;
        if (reloc->type == R_LARCH_PCALA_HI20 ||
            reloc->type == R_LARCH_PCALA_LO12 ||
            reloc->type == R_LARCH_RELAX) {
            entries[entry_count].section = reloc->section;
            entries[entry_count].offset = reloc->offset;
            entries[entry_count].type = reloc->type;
            entries[entry_count].index = i;
            ++entry_count;
        }
    }
    qsort(entries, entry_count, sizeof(*entries),
          minild_loong_relax_entry_compare);
    /* An explicit RELAX marker at *both* halves is mandatory.  Unmarked
     * PCALA is a perfectly valid instruction sequence, not an optimization
     * request.  The 8-byte pair must belong to the same input fragment. */
    for (i = 0U; i < entry_count; ++i) {
        const MiniLdLoongRelaxEntry *hi = &entries[i];
        const MiniLdLoongRelaxEntry *lo, *hm, *lm;
        const MiniLdSection *section;
        const MiniLdReloc *high, *low;
        uint32_t pca, add;
        MiniLdLoongRelaxPair *grown;

        if (hi->type != R_LARCH_PCALA_HI20 ||
            hi->section >= state->work.count ||
            hi->offset > UINT64_MAX - 4U || hi->offset > SIZE_MAX) {
            continue;
        }
        lo = minild_loong_relax_find(entries, entry_count, hi->section,
                                    hi->offset + 4U, R_LARCH_PCALA_LO12);
        hm = minild_loong_relax_find(entries, entry_count, hi->section,
                                    hi->offset, R_LARCH_RELAX);
        lm = minild_loong_relax_find(entries, entry_count, hi->section,
                                    hi->offset + 4U, R_LARCH_RELAX);
        if (lo == NULL || hm == NULL || lm == NULL) continue;
        section = &state->work.sections[hi->section];
        high = &state->relocations.entries[hi->index];
        low = &state->relocations.entries[lo->index];
        if (section->type != SHT_PROGBITS ||
            (section->flags & (SHF_ALLOC | SHF_EXECINSTR)) !=
                (SHF_ALLOC | SHF_EXECINSTR) ||
            !range_ok((size_t)hi->offset, 8U, section->size) ||
            high->symbol != low->symbol || high->addend != low->addend) {
            continue;
        }
        pca = load_u32le(section->data + hi->offset);
        add = load_u32le(section->data + hi->offset + 4U);
        if ((pca & UINT32_C(0xfe000000)) != UINT32_C(0x1a000000) ||
            (add & UINT32_C(0xffc00000)) != UINT32_C(0x02c00000) ||
            (pca & UINT32_C(0x1f)) != (add & UINT32_C(0x1f)) ||
            (pca & UINT32_C(0x1f)) !=
                ((add >> 5U) & UINT32_C(0x1f))) continue;
        if (plan->pair_count == SIZE_MAX / sizeof(*plan->pairs)) goto error;
        grown = realloc(plan->pairs,
                        (plan->pair_count + 1U) * sizeof(*plan->pairs));
        if (grown == NULL) goto error;
        plan->pairs = grown;
        plan->pairs[plan->pair_count++] = (MiniLdLoongRelaxPair){
            hi->section, (size_t)hi->offset, hi->index, lo->index,
            hm->index, lm->index, false};
        if (plan->sections[hi->section].original == NULL) {
            MiniLdLoongRelaxSection *saved = &plan->sections[hi->section];
            saved->original = malloc(section->size ? section->size : 1U);
            if (saved->original == NULL) goto error;
            saved->original_size = section->size;
            memcpy(saved->original, section->data, section->size);
        }
    }
    free(entries);
    return plan;
error:
    free(entries);
    minild_loong_relax_plan_destroy(plan);
    return NULL;
}

/* Re-materialize only sections containing selected PCALA pairs.  The
 * immutable offset projection also adjusts input-defined symbol extents and
 * every relocation, even a fixup unrelated to the shortened instruction. */
static bool minild_loong_relax_replay(
    MiniLdState *state, const MiniLdLoongRelaxPlan *plan) {
    size_t section_index, i;

    for (section_index = 0U; section_index < plan->section_count;
         ++section_index) {
        const MiniLdLoongRelaxSection *saved =
            &plan->sections[section_index];
        MiniLdSection *section = &state->work.sections[section_index];
        MiniLdRelaxEvent *events = NULL;
        unsigned char *bytes = NULL;
        size_t selected = 0U, cursor = 0U, size = 0U, deleted = 0U, e = 0U;

        if (saved->original == NULL) continue;
        for (i = 0U; i < plan->pair_count; ++i) {
            if (plan->pairs[i].section == section_index &&
                plan->pairs[i].shortened) ++selected;
        }
        if (selected == 0U) continue;
        events = malloc(selected * sizeof(*events));
        bytes = malloc(saved->original_size ? saved->original_size : 1U);
        if (events == NULL || bytes == NULL) {
            free(events);
            free(bytes);
            return false;
        }
        for (i = 0U; i < plan->pair_count; ++i) {
            const MiniLdLoongRelaxPair *pair = &plan->pairs[i];
            size_t copy;
            uint32_t original;

            if (pair->section != section_index || !pair->shortened) continue;
            if (pair->offset < cursor ||
                !range_ok(pair->offset, 8U, saved->original_size)) {
                free(events);
                free(bytes);
                fprintf(state->diagnostics,
                        "minic-ld: overlapping-loongarch-pcala-relax\n");
                return false;
            }
            copy = pair->offset - cursor;
            memcpy(bytes + size, saved->original + cursor, copy);
            size += copy;
            original = load_u32le(saved->original + pair->offset);
            store_u32le(bytes + size,
                        UINT32_C(0x18000000) | (original & UINT32_C(31)));
            size += 4U;
            cursor = pair->offset + 8U;
            deleted += 4U;
            events[e].raw_end = cursor;
            events[e].cumulative_deleted = deleted;
            ++e;
        }
        memcpy(bytes + size, saved->original + cursor,
               saved->original_size - cursor);
        size += saved->original_size - cursor;
        memcpy(section->data, bytes, size);
        section->size = size;
        for (i = 0U; i < plan->symbol_count; ++i) {
            const MiniLdLoongRelaxSymbol *saved_symbol = &plan->symbols[i];
            MiniLdResolvedSymbol *symbol = &state->resolver.entries[i];
            size_t begin, end;

            if (!saved_symbol->input_defined ||
                saved_symbol->section != (int)section_index ||
                saved_symbol->value > SIZE_MAX) continue;
            begin = static_map_relaxed_offset((size_t)saved_symbol->value,
                                               events, selected);
            symbol->value = begin;
            if (saved_symbol->size <=
                    SIZE_MAX - (size_t)saved_symbol->value) {
                end = static_map_relaxed_offset(
                    (size_t)saved_symbol->value +
                        (size_t)saved_symbol->size, events, selected);
                symbol->size = end >= begin ? end - begin : 0U;
            }
        }
        for (i = 0U; i < plan->reloc_count; ++i) {
            MiniLdReloc *reloc = &state->relocations.entries[i];
            if (reloc->section != section_index ||
                plan->reloc_offsets[i] > SIZE_MAX) continue;
            reloc->offset = static_map_relaxed_offset(
                (size_t)plan->reloc_offsets[i], events, selected);
            reloc->type = plan->reloc_types[i];
        }
        free(events);
        free(bytes);
    }
    for (i = 0U; i < plan->pair_count; ++i) {
        const MiniLdLoongRelaxPair *pair = &plan->pairs[i];
        if (!pair->shortened) continue;
        state->relocations.entries[pair->hi].type = R_LARCH_PCREL20_S2;
        state->relocations.entries[pair->lo].type = R_LARCH_NONE;
        state->relocations.entries[pair->hi_marker].type = R_LARCH_NONE;
        state->relocations.entries[pair->lo_marker].type = R_LARCH_NONE;
    }
    return true;
}

static bool minild_loongarch_replan_static_layout(
    MiniLdState *state, const MiniLdLayout *layout,
    MiniLdStaticTargetPlan *target_plan, bool *changed) {
    MiniLdLoongarchStaticGot *got = target_plan->target_data;
    MiniLdLoongRelaxPlan *plan;
    size_t i;

    if (got == NULL || changed == NULL) return false;
    *changed = false;
    if (got->relax == NULL) {
        got->relax = minild_loong_relax_capture(state);
        if (got->relax == NULL) {
            fprintf(state->diagnostics,
                    "minic-ld: loongarch-relax-snapshot-failed\n");
            return false;
        }
    }
    plan = got->relax;
    if (plan->phase == MINILD_LOONG_RELAX_PCALA) {
        bool any_new = false;

        for (i = 0U; i < plan->pair_count; ++i) {
            MiniLdLoongRelaxPair *pair = &plan->pairs[i];
            const MiniLdReloc *high =
                &state->relocations.entries[pair->hi];
            uint64_t target, place;
            int64_t delta;

            if (pair->shortened || layout->work[pair->section].discarded ||
                high->symbol >= state->resolver.count ||
                !minild_layout_resolve_symbol(
                    state, layout, high->symbol, &target) ||
                layout->work[pair->section].vaddr >
                    UINT64_MAX - high->offset) continue;
            target += (uint64_t)high->addend;
            place = layout->work[pair->section].vaddr + high->offset;
            if (state->resolver.entries[high->symbol].section ==
                    (int)pair->section && target > place) {
                target -= 4U; /* Same-section forward target moves too. */
            }
            delta = (int64_t)(target - place);
            /* PCADDI signed 20-bit scaled by four: [-2MiB,+2MiB-4].
             * Do not speculate across the range edge or misaligned data. */
            if ((target & UINT64_C(3)) != 0U ||
                (delta & INT64_C(3)) != 0 ||
                delta < -2097152 || delta > 2097148) continue;
            pair->shortened = true;
            any_new = true;
        }
        if (any_new) {
            if (!minild_loong_relax_replay(state, plan)) return false;
            *changed = true;
        } else if (!state->script_forward_changed) {
            plan->phase = MINILD_LOONG_RELAX_ALIGN;
            *changed = true; /* Phase transition needs one fresh layout. */
        }
        return true;
    }
    if (plan->phase == MINILD_LOONG_RELAX_ALIGN) {
        for (i = 0U; i < state->work.count; ++i) {
            size_t before = state->work.sections[i].size;
            if ((state->work.sections[i].flags & SHF_ALLOC) != 0U &&
                !layout->work[i].discarded &&
                !minild_loongarch_relax_section(
                    state, i, layout->work[i].vaddr)) return false;
            if (state->work.sections[i].size != before) *changed = true;
        }
        plan->phase = MINILD_LOONG_RELAX_CLOSED;
        *changed = true; /* ALIGN closes all sections for later deletion. */
        return true;
    }
    return true;
}
