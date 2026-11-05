/* Mutable relocation owner for ET_REL, static and shared links.
 *
 * Each entry retains its original stable input-section ID and input offset,
 * while its current work-section ID and offset may change under source
 * materialization, section permutations and R_RISCV_ALIGN relaxation.
 * The optional CSR index is built after work-section permutations and GC;
 * relaxation can then adjust offsets without changing section ownership.
 */

static void relocations_destroy(MiniLdRelocationWorkspace *relocations) {
    free(relocations->entries);
    free(relocations->section_starts);
    free(relocations->section_refs);
    memset(relocations, 0, sizeof(*relocations));
}

static bool relocations_require_editable(
    const MiniLdRelocationWorkspace *relocations,
    FILE *diagnostics,
    const char *phase) {
    if (relocations->section_starts != NULL) {
        fprintf(diagnostics, "minic-ld: relocation-index-frozen:%s\n", phase);
        return false;
    }
    return true;
}

static bool relocations_ensure_capacity(MiniLdRelocationWorkspace *relocations) {
    MiniLdReloc *next;
    size_t capacity;

    if (relocations->count < relocations->capacity) {
        return true;
    }
    capacity = relocations->capacity == 0U ? 64U :
               relocations->capacity * 2U;
    if (capacity < relocations->capacity ||
        capacity > SIZE_MAX / sizeof(*relocations->entries)) {
        return false;
    }
    next = realloc(relocations->entries,
                   capacity * sizeof(*relocations->entries));
    if (next == NULL) {
        return false;
    }
    relocations->entries = next;
    relocations->capacity = capacity;
    return true;
}

static bool relocations_append(MiniLdRelocationWorkspace *relocations,
                               FILE *diagnostics,
                               size_t section,
                               uint64_t offset,
                               uint32_t type,
                               size_t symbol,
                               int64_t addend,
                               size_t input_section,
                               uint64_t input_offset) {
    MiniLdReloc *entry;

    /* The CSR is a section-index snapshot; a later append would leave an
     * incomplete range for its section, silently skipping a relocation. */
    if (!relocations_require_editable(relocations, diagnostics, "append")) {
        return false;
    }
    if (!relocations_ensure_capacity(relocations)) {
        fprintf(diagnostics, "minic-ld: out-of-memory:relocations\n");
        return false;
    }
    entry = &relocations->entries[relocations->count++];
    entry->section = section;
    entry->offset = offset;
    entry->type = type;
    entry->symbol = symbol;
    entry->addend = addend;
    entry->input_section = input_section;
    entry->input_offset = input_offset;
    return true;
}

/* R_RISCV_ALIGN may scan millions of relocations over thousands of sections.
 * A stable CSR structure avoids quadratic rescans. References retain input
 * order within each work section; entries themselves are never re-sorted.
 * The caller must complete section permutations and any GC compaction before
 * requesting this index. Later synthetic sections cannot own relocations. */
static bool relocations_build_section_index(
    MiniLdRelocationWorkspace *relocations,
    size_t work_section_count,
    FILE *diagnostics) {
    size_t *starts = NULL;
    size_t *refs = NULL;
    size_t *positions = NULL;
    size_t i;

    if (relocations->section_starts != NULL) {
        return true;
    }
    if (work_section_count == SIZE_MAX ||
        work_section_count + 1U > SIZE_MAX / sizeof(*starts) ||
        relocations->count > SIZE_MAX / sizeof(*refs)) {
        fprintf(diagnostics, "minic-ld: relocation-index-overflow\n");
        return false;
    }
    starts = calloc(work_section_count + 1U, sizeof(*starts));
    refs = malloc((relocations->count == 0U ? 1U : relocations->count) *
                  sizeof(*refs));
    positions = malloc((work_section_count == 0U ? 1U :
                        work_section_count) * sizeof(*positions));
    if (starts == NULL || refs == NULL || positions == NULL) {
        fprintf(diagnostics, "minic-ld: out-of-memory:relocation-index\n");
        goto fail;
    }
    for (i = 0U; i < relocations->count; ++i) {
        const size_t section = relocations->entries[i].section;

        if (section >= work_section_count ||
            starts[section + 1U] == SIZE_MAX) {
            fprintf(diagnostics,
                    "minic-ld: invalid-relocation-section-index\n");
            goto fail;
        }
        ++starts[section + 1U];
    }
    for (i = 1U; i <= work_section_count; ++i) {
        if (starts[i] > SIZE_MAX - starts[i - 1U]) {
            fprintf(diagnostics, "minic-ld: relocation-prefix-overflow\n");
            goto fail;
        }
        starts[i] += starts[i - 1U];
    }
    for (i = 0U; i < work_section_count; ++i) {
        positions[i] = starts[i];
    }
    for (i = 0U; i < relocations->count; ++i) {
        size_t section = relocations->entries[i].section;
        refs[positions[section]++] = i;
    }
    free(positions);
    relocations->section_starts = starts;
    relocations->section_refs = refs;
    relocations->index_sections = work_section_count;
    return true;

fail:
    free(positions);
    free(refs);
    free(starts);
    return false;
}
